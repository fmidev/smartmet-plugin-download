// ======================================================================
/*!
 * \brief OGC API Coverages request handler implementation
 *
 *        Metadata endpoints return JSON conforming to OGC API Common.
 *        The /coverage endpoint translates OGC parameters to the
 *        download plugin's ReqParams via request parameter mapping
 *        and delegates to the shared createStreamer() factory.
 */
// ======================================================================

#include "coverages/Handler.h"
#include "ParamConfig.h"
#include "Query.h"
#include "StreamerFactory.h"
#include "coverages/ApiDefinition.h"
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <engines/querydata/MetaQueryOptions.h>
#include <gis/ProjInfo.h>
#include <json/writer.h>
#include <macgyver/CharsetTools.h>
#include <macgyver/DateTime.h>
#include <macgyver/Exception.h>
#include <macgyver/StringConversion.h>
#include <macgyver/TimeFormatter.h>
#include <macgyver/TimeParser.h>
#include <spine/Convenience.h>
#include <spine/FmiApiKey.h>
#include <spine/HostInfo.h>
#include <algorithm>
#include <cpl_conv.h>
#include <limits>
#include <memory>
#include <ogr_spatialref.h>
#include <optional>
#include <set>
#include <vector>

using namespace std;

namespace SmartMet
{
namespace Plugin
{
namespace Download
{
// ----------------------------------------------------------------------
/*!
 * \brief Split a URL path into segments
 */
// ----------------------------------------------------------------------

namespace
{
vector<string> splitPath(const string &resource)
{
  vector<string> parts;
  boost::split(parts, resource, boost::is_any_of("/"));

  // Remove empty segments (leading slash produces one)
  parts.erase(
      std::remove_if(parts.begin(), parts.end(), [](const string &s) { return s.empty(); }),
      parts.end());

  return parts;
}
}  // namespace

// The root of the OGC API - Coverages resources
static const string COVERAGES_ROOT = "/coverages";

// ----------------------------------------------------------------------
/*!
 * \brief Set a JSON response
 */
// ----------------------------------------------------------------------

namespace
{
void setJsonResponse(Spine::HTTP::Response &theResponse, const Json::Value &doc)
{
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "  ";
  builder["emitUTF8"] = true;
  theResponse.setContent(Json::writeString(builder, doc) + "\n");
  theResponse.setStatus(Spine::HTTP::Status::ok);
  theResponse.setHeader("Content-Type", "application/json");
  theResponse.setHeader("Access-Control-Allow-Origin", "*");
}

// ----------------------------------------------------------------------
/*!
 * \brief Set an RFC 7807 problem details response
 */
// ----------------------------------------------------------------------

void setProblemResponse(Spine::HTTP::Response &theResponse,
                               Spine::HTTP::Status status,
                               const string &title,
                               const string &detail)
{
  Json::Value doc;
  doc["title"] = title;
  doc["status"] = static_cast<int>(status);
  doc["detail"] = detail;
  setJsonResponse(theResponse, doc);
  theResponse.setStatus(status);
  theResponse.setHeader("Content-Type", "application/problem+json");
}

Json::Value makeLink(const string &href,
                            const string &rel,
                            const string &type,
                            const string &title = "")
{
  return Spine::OpenAPI::link(href, rel, type, title);
}

// Redact absolute filesystem paths from a message so internal server paths are not
// leaked to clients through the X-Download-Error response header.
string redactPaths(const string &msg)
{
  vector<string> tokens;
  boost::algorithm::split(tokens, msg, boost::algorithm::is_space());
  for (auto &tok : tokens)
  {
    if (tok.size() > 1 && tok[0] == '/' && tok.find('/', 1) != string::npos)
      tok = "<path>";
  }
  return boost::algorithm::join(tokens, " ");
}

// ----------------------------------------------------------------------
/*!
 * \brief Map OGC output format to download format string
 *
 *        Supports both MIME types (f=application/x-grib2) and short
 *        names (f=GRIB2, f=netcdf) for convenience.
 */
// ----------------------------------------------------------------------

string mapOutputFormat(const string &f)
{
  string fl = Fmi::ascii_tolower_copy(f);

  if (fl == "application/x-grib2" || fl == "grib2")
    return "GRIB2";
  if (fl == "application/x-grib" || fl == "grib1" || fl == "grib")
    return "GRIB1";
  if (fl == "application/netcdf" || fl == "application/x-netcdf" || fl == "netcdf")
    return "NETCDF";
  if (fl == "image/tiff" || fl == "image/geotiff" || fl == "application/x-geotiff" ||
      fl == "geotiff" || fl == "gtiff" || fl == "tiff")
    return "GEOTIFF";
  if (fl == "application/x-fmi-querydata" || fl == "qd" || fl == "querydata")
    return "QD";

  throw Fmi::Exception(BCP, "Unsupported output format: " + f);
}

// ----------------------------------------------------------------------
/*!
 * \brief Split an OGC subset axis value into a low:high range or a single value
 *
 *        Quoted values are split at the colon between the quoted parts. Unquoted
 *        values may also contain colons (ISO times), hence a symmetric range
 *        has an odd number of colons and is split at the middle one, while
 *        a single value has none or an even number of them. The quotes are
 *        removed from the returned values.
 */
// ----------------------------------------------------------------------

vector<string> splitSubsetRange(const string &value)
{
  vector<string> parts;

  if (!value.empty() && (value[0] == '"' || value[0] == '\''))
  {
    const char quote = value[0];
    auto end = value.find(quote, 1);
    if (end == string::npos)
      throw Fmi::Exception(BCP, "Invalid subset value: " + value);
    parts.push_back(value.substr(1, end - 1));

    string rest = value.substr(end + 1);
    boost::trim(rest);
    if (!rest.empty())
    {
      if (rest[0] != ':')
        throw Fmi::Exception(BCP, "Invalid subset value: " + value);
      rest = rest.substr(1);
      boost::trim(rest);
      parts.push_back(rest);
    }
  }
  else
  {
    const auto colons = std::count(value.begin(), value.end(), ':');
    if (colons % 2 == 0)
      parts.push_back(value);
    else
    {
      auto pos = value.find(':');
      for (long i = 0; i < colons / 2; i++)
        pos = value.find(':', pos + 1);

      parts.push_back(value.substr(0, pos));
      parts.push_back(value.substr(pos + 1));
    }
  }

  for (auto &part : parts)
  {
    boost::trim(part);
    if (part.size() >= 2 && (part.front() == '"' || part.front() == '\'') &&
        part.back() == part.front())
      part = part.substr(1, part.size() - 2);
  }

  return parts;
}

// Set the request parameters of a low:high subset range. Empty limits are not set.
void setRangeParameters(Spine::HTTP::Request &dlReq,
                        const vector<string> &range,
                        const string &lowName,
                        const string &highName)
{
  if (!range[0].empty())
    dlReq.setParameter(lowName, range[0]);
  if (!range[1].empty())
    dlReq.setParameter(highName, range[1]);
}

// ----------------------------------------------------------------------
/*!
 * \brief Parse OGC subset parameter
 *
 *        OGC subset syntax: subset=axisName(low:high)
 *        Multiple axes are comma-separated or given as separate
 *        subset parameters.
 *
 *        Recognized axes:
 *          pressure(500:850)     → minlevel/maxlevel
 *          height(0:100)         → minlevel/maxlevel (height levels)
 *          time("t1":"t2")       → starttime/endtime
 *
 *        Quotes around time values are optional.
 */
// ----------------------------------------------------------------------

void parseSubset(const string &subset,
                        Spine::HTTP::Request &dlReq)
{
  // Split on comma for multiple axes: subset=pressure(500:850),time("2024-01-01":"2024-01-02")
  vector<string> axisDefs;
  boost::split(axisDefs, subset, boost::is_any_of(","));

  for (const auto &axisDef : axisDefs)
  {
    // Find axisName(value)
    auto parenOpen = axisDef.find('(');
    auto parenClose = axisDef.rfind(')');

    if (parenOpen == string::npos || parenClose == string::npos || parenClose <= parenOpen)
      throw Fmi::Exception(BCP, "Invalid subset syntax: " + axisDef);

    string axisName = Fmi::ascii_tolower_copy(axisDef.substr(0, parenOpen));
    boost::trim(axisName);
    string value = axisDef.substr(parenOpen + 1, parenClose - parenOpen - 1);
    boost::trim(value);

    // Split the range before removing the optional quotes, since time values contain colons
    const auto range = splitSubsetRange(value);

    if (axisName == "pressure" || axisName == "height" || axisName == "level")
    {
      // Range: low:high or single value
      if (range.size() == 2)
        setRangeParameters(dlReq, range, "minlevel", "maxlevel");
      else
      {
        // Single level
        dlReq.setParameter("level", range[0]);
      }
    }
    else if (axisName == "time")
    {
      // Range: t1:t2 or single value
      if (range.size() == 2)
        setRangeParameters(dlReq, range, "starttime", "endtime");
      else
      {
        dlReq.setParameter("starttime", range[0]);
        dlReq.setParameter("endtime", range[0]);
      }
    }
    else
    {
      throw Fmi::Exception(BCP, "Unsupported subset axis: " + axisName);
    }
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Parse OGC scale-size parameter
 *
 *        OGC syntax: scale-size=Lon(nx),Lat(ny)
 *        Maps to download gridsize=nx,ny
 */
// ----------------------------------------------------------------------

string parseScaleSize(const string &scaleSize)
{
  // Extract numeric values from Lon(nx),Lat(ny) or just nx,ny
  string result;
  string s = scaleSize;

  // Try to parse Axis(value) format
  vector<string> parts;
  boost::split(parts, s, boost::is_any_of(","));

  for (const auto &part : parts)
  {
    string trimmed = part;
    boost::trim(trimmed);

    auto parenOpen = trimmed.find('(');
    auto parenClose = trimmed.rfind(')');

    string val;
    if (parenOpen != string::npos && parenClose != string::npos)
      val = trimmed.substr(parenOpen + 1, parenClose - parenOpen - 1);
    else
      val = trimmed;

    boost::trim(val);

    if (!result.empty())
      result += ",";
    result += val;
  }

  return result;
}

// ----------------------------------------------------------------------
/*!
 * \brief Parse OGC scale-factor parameter
 *
 *        A uniform scale factor. Since the download plugin uses
 *        gridstep (integer stepping), a scale-factor of N maps to
 *        gridstep=N,N.
 */
// ----------------------------------------------------------------------

string parseScaleFactor(const string &scaleFactor)
{
  return scaleFactor + "," + scaleFactor;
}

// ----------------------------------------------------------------------
/*!
 * \brief Parse OGC datetime parameter
 *
 *        Supports:
 *          datetime=2024-01-01T00:00:00Z          → single time
 *          datetime=2024-01-01/2024-01-02          → range
 *          datetime=../2024-01-02                  → open start
 *          datetime=2024-01-01/..                  → open end
 */
// ----------------------------------------------------------------------

void parseDatetime(const string &datetime,
                          Spine::HTTP::Request &dlReq)
{
  auto slashPos = datetime.find('/');

  if (slashPos == string::npos)
  {
    // Single instant: exactly one timestep starting at the given time. The time option
    // parser rejects 'timesteps' together with 'endtime', so only starttime is set.
    dlReq.setParameter("starttime", datetime);
    dlReq.setParameter("timesteps", "1");
  }
  else
  {
    string t1 = datetime.substr(0, slashPos);
    string t2 = datetime.substr(slashPos + 1);

    if (t1 != ".." && !t1.empty())
      dlReq.setParameter("starttime", t1);
    if (t2 != ".." && !t2.empty())
      dlReq.setParameter("endtime", t2);
  }
}

// Test whether the projection uses the WGS84 datum or a spherical earth, and no false
// easting/northing or scaling, as newbase projections assume
bool isNewbaseDatum(const Fmi::ProjInfo &projInfo)
{
  auto datum = projInfo.getString("datum");
  auto ellps = projInfo.getString("ellps");
  auto r = projInfo.getDouble("R");
  auto a = projInfo.getDouble("a");
  auto b = projInfo.getDouble("b");

  bool newbaseDatum = ((datum && (*datum == "WGS84")) || (ellps && (*ellps == "sphere")) ||
                       (r && (*r > 0)) || (a && (*a > 0) && ((!b) || (*a == *b))));

  auto x0 = projInfo.getDouble("x_0");
  auto y0 = projInfo.getDouble("y_0");
  auto k0 = projInfo.getDouble("k") ? projInfo.getDouble("k") : projInfo.getDouble("k_0");

  if ((x0 && (*x0 != 0)) || (y0 && (*y0 != 0)) || (k0 && (*k0 != 1)))
    return false;

  return newbaseDatum;
}

// The newbase projection string equivalent to the projection, if there is one
std::optional<string> newbaseProjection(const Fmi::ProjInfo &projInfo, const string &name)
{
  auto getDouble = [&projInfo](const string &param, double defaultValue)
  {
    auto value = projInfo.getDouble(param);
    return (value ? *value : defaultValue);
  };

  double lon0 = getDouble("lon_0", 0);
  double lat0 = getDouble("lat_0", 0);
  double latts = getDouble("lat_ts", lat0);

  if ((name == "eqc") && (lon0 == 0) && (latts == 0))
    return "latlon";
  if ((name == "merc") && (lon0 == 0) && (latts == 0))
    return "mercator";
  if (name == "stere")
    return "stereographic," + Fmi::to_string(lon0) + "," + Fmi::to_string(lat0) + "," +
           Fmi::to_string(latts);
  if (name == "aeqd")
    return "equidist," + Fmi::to_string(lon0) + "," + Fmi::to_string(lat0);
  if (name == "lcc")
  {
    double lat1 = getDouble("lat_1", lat0);
    double lat2 = getDouble("lat_2", lat1);
    return "lcc," + Fmi::to_string(lon0) + "," + Fmi::to_string(lat0) + "," +
           Fmi::to_string(lat1) + "," + Fmi::to_string(lat2);
  }

  return std::nullopt;
}

// ----------------------------------------------------------------------
/*!
 * \brief Translate an OGC crs parameter to a download projection string
 *
 *        OGC CRS URIs and EPSG codes are translated to epsg:n form.
 *        Any other description must be one GDAL understands (e.g. a
 *        WKT or PROJ string); newbase projection strings are not
 *        accepted.
 *
 *        A GDAL description matching a newbase projection is translated
 *        to the equivalent newbase projection string so that the output
 *        is identical to the legacy /download API's. Newbase projections
 *        are taken to use the WGS84 datum or the legacy spherical earth.
 */
// ----------------------------------------------------------------------

string crsToProjection(const string &crs)
{
  // OGC CRS URIs
  const string epsgPrefix = "http://www.opengis.net/def/crs/EPSG/0/";
  if (crs.find(epsgPrefix) == 0)
    return "epsg:" + crs.substr(epsgPrefix.size());

  // http://www.opengis.net/def/crs/OGC/1.3/CRS84
  if (crs.find("CRS84") != string::npos)
    return "epsg:4326";

  string lcrs = Fmi::ascii_tolower_copy(crs);
  if (lcrs.find("epsg:") == 0)
    return lcrs;

  OGRSpatialReference srs;
  if (srs.SetFromUserInput(crs.c_str()) != OGRERR_NONE)
    throw Fmi::Exception(BCP, "Unsupported crs '" + crs + "'");

  if (!srs.IsProjected())
    return "latlon";

  char *projStrC = nullptr;
  if ((srs.exportToProj4(&projStrC) != OGRERR_NONE) || !projStrC)
  {
    CPLFree(projStrC);
    throw Fmi::Exception(BCP, "Unsupported crs '" + crs + "'");
  }

  string projStr(projStrC);
  CPLFree(projStrC);

  Fmi::ProjInfo projInfo(projStr);

  // Newbase projections assume the WGS84 datum or a spherical earth, and
  // no false easting/northing or scaling

  bool newbaseDatum = isNewbaseDatum(projInfo);

  auto name = projInfo.getString("proj");

  if (newbaseDatum && name)
  {
    auto projection = newbaseProjection(projInfo, *name);
    if (projection)
      return *projection;
  }

  // No newbase equivalent; pass the GDAL description through as such

  return crs;
}

// ----------------------------------------------------------------------
/*!
 * \brief Translate OGC API parameters to download parameters
 *
 *        Creates a copy of the HTTP request with OGC parameters
 *        translated to their download-equivalent parameter names.
 *        The modified request can then be passed to the existing
 *        Query constructor and createStreamer() factory.
 *
 *        OGC parameter mapping:
 *
 *          properties → param          (field selection)
 *          bbox       → bbox           (spatial subsetting, same format)
 *          datetime   → starttime/endtime (temporal subsetting)
 *          subset     → level/starttime/endtime (n-dimensional subsetting)
 *          scale-size → gridsize       (scaling)
 *          scale-factor → gridstep     (uniform scaling)
 *          crs        → projection     (CRS reprojection)
 *          f          → format         (output format)
 */
// ----------------------------------------------------------------------

Spine::HTTP::Request translateRequest(const Spine::HTTP::Request &theRequest,
                                             const string &collectionId)
{
  Spine::HTTP::Request dlReq = theRequest;

  // Collection ID → producer
  dlReq.setParameter("producer", collectionId);

  // Default source is querydata
  if (!dlReq.getParameter("source"))
    dlReq.setParameter("source", "querydata");

  // properties → param (OGC field selection)
  auto properties = dlReq.getParameter("properties");
  if (properties)
  {
    dlReq.setParameter("param", *properties);
    dlReq.removeParameter("properties");
  }

  // f → format (output format negotiation)
  auto f = dlReq.getParameter("f");
  if (f)
  {
    dlReq.setParameter("format", mapOutputFormat(*f));
    dlReq.removeParameter("f");
  }
  else
  {
    // Default to NetCDF for OGC API
    if (!dlReq.getParameter("format"))
      dlReq.setParameter("format", "NETCDF");
  }

  // datetime → starttime/endtime
  auto datetime = dlReq.getParameter("datetime");
  if (datetime)
  {
    parseDatetime(*datetime, dlReq);
    dlReq.removeParameter("datetime");
  }

  // subset → level/starttime/endtime depending on axis
  auto subset = dlReq.getParameter("subset");
  if (subset)
  {
    parseSubset(*subset, dlReq);
    dlReq.removeParameter("subset");
  }

  // crs → projection
  auto crs = dlReq.getParameter("crs");
  if (crs)
  {
    dlReq.setParameter("projection", crsToProjection(*crs));
    dlReq.removeParameter("crs");
  }

  // scale-size → gridsize
  auto scaleSize = dlReq.getParameter("scale-size");
  if (scaleSize)
  {
    dlReq.setParameter("gridsize", parseScaleSize(*scaleSize));
    dlReq.removeParameter("scale-size");
  }

  // scale-factor → gridstep (approximate: integer stepping)
  auto scaleFactor = dlReq.getParameter("scale-factor");
  if (scaleFactor)
  {
    dlReq.setParameter("gridstep", parseScaleFactor(*scaleFactor));
    dlReq.removeParameter("scale-factor");
  }

  // scale-axes → gridresolution (per-axis scaling)
  auto scaleAxes = dlReq.getParameter("scale-axes");
  if (scaleAxes)
  {
    dlReq.setParameter("gridresolution", parseScaleSize(*scaleAxes));
    dlReq.removeParameter("scale-axes");
  }

  // bbox is the same format in both APIs: bbox=minlon,minlat,maxlon,maxlat
  // No translation needed

  return dlReq;
}

// ----------------------------------------------------------------------
/*!
 * \brief Get the MIME type for a download format
 */
// ----------------------------------------------------------------------

string getMimeType(OutputFormat fmt)
{
  switch (fmt)
  {
    case Grib1:
      return "application/x-grib";
    case Grib2:
      return "application/x-grib2";
    case NetCdf:
      return "application/netcdf";
    case GeoTiff:
      return "image/tiff";
    case QD:
      return "application/x-fmi-querydata";
    default:
      return "application/octet-stream";
  }
}

// Parse the data source and check grid data is available if needed
void parseDataSource(const Spine::HTTP::Request &req,
                     ReqParams &reqParams,
                     const Engine::Grid::Engine *gridEngine)
{
  reqParams.source = Spine::optional_string(req.getParameter("source"), "querydata");

  if (reqParams.source == "querydata")
    reqParams.dataSource = QueryData;
  else if (reqParams.source == "gridmapping")
    reqParams.dataSource = GridMapping;
  else if (reqParams.source == "grid" || reqParams.source == "gridcontent")
  {
    reqParams.source = "gridcontent";
    reqParams.dataSource = GridContent;
  }
  else
    throw Fmi::Exception(BCP, "Unknown source: " + reqParams.source);

  if (reqParams.dataSource != QueryData)
  {
    if (!gridEngine)
      throw Fmi::Exception(BCP, "Grid data is not available");
    if (!(gridEngine->isEnabled()))
      throw Fmi::Exception(BCP, "Grid data is disabled");
  }
}

// Parse time parameters
void parseTimeParams(const Spine::HTTP::Request &req, ReqParams &reqParams)
{
  reqParams.startTime = Spine::optional_string(req.getParameter("starttime"), "");
  reqParams.endTime = Spine::optional_string(req.getParameter("endtime"), "");
  reqParams.originTime = Spine::optional_string(req.getParameter("origintime"), "");
  reqParams.timeSteps = Spine::optional_unsigned_long(req.getParameter("timesteps"), 0);
  reqParams.maxTimeSteps = 0;

  string timeStepStr = Spine::optional_string(req.getParameter("timestep"), "");
  reqParams.timeStep = ((timeStepStr != "data")
                            ? Spine::optional_unsigned_long(req.getParameter("timestep"), 0)
                            : 0);

  if (reqParams.startTime == "data")
    reqParams.startTime.clear();
  if (reqParams.endTime == "data")
    reqParams.endTime.clear();
  if (reqParams.originTime == "data")
    reqParams.originTime.clear();
}

// Parse datum
void parseDatum(const Spine::HTTP::Request &req, ReqParams &reqParams)
{
  reqParams.datum = Spine::optional_string(req.getParameter("datum"), "");
  if (!reqParams.datum.empty())
  {
    if (!Datum::parseDatumShift(reqParams.datum, reqParams.datumShift))
      throw Fmi::Exception(BCP, "Invalid datum selected");
  }
  else
  {
    reqParams.datumShift = Datum::DatumShift::None;
  }
}

// Parse projection
void parseProjection(const Spine::HTTP::Request &req, ReqParams &reqParams)
{
  reqParams.projection = Spine::optional_string(req.getParameter("projection"), "");

  if (reqParams.dataSource == QueryData && !reqParams.projection.empty())
  {
    // Simplified projection type detection for EPSG codes
    string proj = Fmi::ascii_tolower_copy(reqParams.projection);
    if (proj.find("epsg:") == 0)
    {
      reqParams.projType = P_Epsg;
      reqParams.epsgCode = Fmi::stoul(proj.substr(5));
      reqParams.areaClassId = A_Native;

      // Check if it's a geographic CRS (treat as latlon for cropping)
      OGRSpatialReference srs;
      if (srs.importFromEPSG(reqParams.epsgCode) == OGRERR_NONE && !srs.IsProjected())
      {
        reqParams.projection = "latlon";
        reqParams.projType = P_LatLon;
        reqParams.areaClassId = A_LatLon;
      }
      else if (reqParams.datumShift == Datum::DatumShift::None)
      {
        reqParams.datumShift = Datum::DatumShift::Fmi;
      }
    }
    else if (proj == "latlon")
    {
      reqParams.projType = P_LatLon;
      reqParams.areaClassId = A_LatLon;
    }
    else
    {
      reqParams.projType = P_Native;
      reqParams.areaClassId = A_Native;
    }
  }
  else
  {
    reqParams.projType = P_Native;
    reqParams.areaClassId = A_Native;
  }
}

// Parse bounding box, grid center, size, resolution and step
void parseGridParams(const Spine::HTTP::Request &req, ReqParams &reqParams)
{
  // Bounding box (same format in both APIs)
  reqParams.bbox = reqParams.origBBox = Spine::optional_string(req.getParameter("bbox"), "");

  if (!reqParams.bbox.empty())
    reqParams.bboxRect = nPairsOfValues<double>(reqParams.bbox, "bbox", 2);

  // Grid center lon,lat and width and height in km; lon,lat,width,height
  reqParams.gridCenter = Spine::optional_string(req.getParameter("gridcenter"), "");

  if (!reqParams.gridCenter.empty())
  {
    if (reqParams.bboxRect)
      throw Fmi::Exception(BCP, "Cannot specify gridcenter and bbox simultaneously");

    reqParams.gridCenterLL = nPairsOfValues<double>(reqParams.gridCenter, "gridcenter", 2);
  }

  // Grid size (translated from scale-size)
  reqParams.gridSize = Spine::optional_string(req.getParameter("gridsize"), "");
  if (!reqParams.gridSize.empty())
    reqParams.gridSizeXY = nPairsOfValues<unsigned int>(reqParams.gridSize, "gridsize", 1);

  // Grid resolution (translated from scale-axes)
  reqParams.gridResolution = Spine::optional_string(req.getParameter("gridresolution"), "");
  if (!reqParams.gridResolution.empty())
  {
    if (reqParams.gridSizeXY)
      throw Fmi::Exception(BCP, "Cannot specify both scale-size and scale-axes");
    reqParams.gridResolutionXY =
        nPairsOfValues<double>(reqParams.gridResolution, "gridresolution", 1);
  }

  // Grid step (translated from scale-factor)
  reqParams.gridStep = Spine::optional_string(req.getParameter("gridstep"), "");
  if (!reqParams.gridStep.empty())
    reqParams.gridStepXY = nPairsOfValues<unsigned int>(reqParams.gridStep, "gridstep", 1);
}

// Parse output format
void parseOutputFormat(const Spine::HTTP::Request &req, ReqParams &reqParams)
{
  reqParams.format = Spine::optional_string(req.getParameter("format"), "NETCDF");
  Fmi::ascii_toupper(reqParams.format);

  if (reqParams.format == "GRIB1")
    reqParams.outputFormat = Grib1;
  else if (reqParams.format == "GRIB2")
    reqParams.outputFormat = Grib2;
  else if (reqParams.format == "NETCDF")
    reqParams.outputFormat = NetCdf;
  else if ((reqParams.format == "GEOTIFF") || (reqParams.format == "GTIFF") ||
           (reqParams.format == "TIFF"))
    reqParams.outputFormat = GeoTiff;
  else if (reqParams.format == "QD")
  {
    if (reqParams.dataSource != QueryData)
      throw Fmi::Exception(BCP, "Querydata format not supported with grid data");
    reqParams.outputFormat = QD;
  }
  else
    throw Fmi::Exception(BCP, "Unsupported output format: " + reqParams.format);
}

// Parse packing type and bitspervalue for grib
void parsePacking(const Spine::HTTP::Request &req, ReqParams &reqParams, const Config &config)
{
  reqParams.packing = Spine::optional_string(req.getParameter("packing"), "");
  Fmi::ascii_tolower(reqParams.packing);

  auto bitsPerValue = Spine::optional_string(req.getParameter("bitspervalue"), "");

  if (reqParams.packing.empty() != bitsPerValue.empty())
    throw Fmi::Exception(BCP, "Both packing and bitspervalue must be given");

  if (!reqParams.packing.empty())
  {
    if ((reqParams.outputFormat != Grib1) && (reqParams.outputFormat != Grib2))
      throw Fmi::Exception(BCP, "Packing can be specified with grib format only")
          .addParameter("packing", reqParams.packing);

    auto msg = config.packingErrorMessage(reqParams.packing);
    if (!msg.empty())
      throw Fmi::Exception(BCP, msg).addParameter("packing", reqParams.packing);
  }

  try
  {
    if (!bitsPerValue.empty())
    {
      auto bpv = Fmi::stoi(bitsPerValue);

      if ((bpv < 0) || (bpv > 32))
        throw Fmi::Exception(BCP, "");

      reqParams.bitsPerValue = bpv;
    }
  }
  catch (...)
  {
    throw Fmi::Exception(BCP, "Invalid packing bitspervalue, must be in range 0-32");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Fill ReqParams from a translated download-style request
 *
 *        This mirrors the getRequestParams() logic in download/Handler.cpp
 *        but simplified for OGC API use: no per-producer disabled
 *        parameters, simplified format validation, etc.
 */
// ----------------------------------------------------------------------

const Producer &fillReqParams(const Spine::HTTP::Request &req,
                                     ReqParams &reqParams,
                                     Config &config,
                                     const Engine::Querydata::Engine &qEngine,
                                     const Engine::Grid::Engine *gridEngine)
{
  try
  {
    // Data source
    static Producer dummyProducer;
    parseDataSource(req, reqParams, gridEngine);

    // Producer (set by translateRequest from collection ID)
    reqParams.producer = Spine::optional_string(req.getParameter("producer"), "");

    if (reqParams.producer.empty())
      reqParams.producer = config.defaultProducerName();

    const Producer &producer = (reqParams.dataSource == QueryData)
                                   ? config.getProducer(reqParams.producer, qEngine)
                                   : dummyProducer;

    if (reqParams.producer.empty())
      throw Fmi::Exception(BCP, "No producer / collection specified");

    // Time parameters (already translated by parseDatetime/parseSubset)
    parseTimeParams(req, reqParams);

    // Levels (already translated by parseSubset)
    reqParams.minLevel = Spine::optional_int(req.getParameter("minlevel"), -1);
    reqParams.maxLevel = Spine::optional_int(req.getParameter("maxlevel"), -1);
    reqParams.minHeight = reqParams.maxHeight = -1;

    // Datum
    parseDatum(req, reqParams);

    // Projection (already translated by translateRequest from crs)
    parseProjection(req, reqParams);

    // Bounding box, grid center, size, resolution and step
    parseGridParams(req, reqParams);

    // Output format
    parseOutputFormat(req, reqParams);

    // Packing type and bitspervalue for grib
    parsePacking(req, reqParams, config);

    // GRIB2 tables version
    reqParams.grib2TablesVersion =
        ((reqParams.outputFormat == Grib2)
             ? Spine::optional_unsigned_long(req.getParameter("tablesversion"),
                                            config.getGrib2TablesVersionDefault())
             : 0);

    // Block sizes and chunk size (not typically used via OGC API)
    reqParams.gridParamBlockSize = 0;
    reqParams.gridTimeBlockSize = 0;
    reqParams.chunkSize = 0;
    reqParams.test = 0;

    return producer;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}
}  // namespace

// ----------------------------------------------------------------------
/*!
 * \brief Initialize handler
 */
// ----------------------------------------------------------------------

void CoveragesHandler::init(Config &config,
                            Engine::Querydata::Engine *qEngine,
                            Engine::Grid::Engine *gridEngine,
                            Engine::Geonames::Engine *geoEngine)
{
  itsConfig = &config;
  itsQEngine = qEngine;
  itsGridEngine = gridEngine;
  itsGeoEngine = geoEngine;

  // The OpenAPI definition lists the configured producers, it is generated once
  auto settings = Spine::OpenAPI::Settings::parse(config.config());
  if (settings.enabled)
  {
    itsApi = std::make_unique<Spine::OpenAPI::Publisher>(std::move(settings));
    itsApi->publish(Coverages::apiDefinition(config, itsApi->settings()));
    itsApi->addToCatalog(
        COVERAGES_ROOT, Coverages::API_TITLE, Coverages::API_DESCRIPTION, "coverages");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Route OGC API Coverages requests
 *
 *   /coverages                              → landing page
 *   /coverages/conformance                  → conformance declaration
 *   /coverages/collections                  → list collections
 *   /coverages/collections/{id}             → collection metadata
 *   /coverages/collections/{id}/schema      → field schema
 *   /coverages/collections/{id}/coverage    → coverage data
 */
// ----------------------------------------------------------------------

// ----------------------------------------------------------------------
/*!
 * \brief 404 Not Found as RFC 7807 problem details
 */
// ----------------------------------------------------------------------

namespace
{
void notFound(const Spine::HTTP::Request &theRequest, Spine::HTTP::Response &theResponse)
{
  setProblemResponse(theResponse,
                     Spine::HTTP::Status::not_found,
                     "Not Found",
                     "Unknown resource: " + theRequest.getResource());
}
}  // namespace

void CoveragesHandler::requestHandler(const Spine::Reactor& /* theReactor */,
                                      const Spine::HTTP::Request &theRequest,
                                      Spine::HTTP::Response &theResponse)
{
  try
  {
    try
    {
      auto parts = splitPath(theRequest.getResource());

      // parts[0] == "coverages"
      // Dispatch based on remaining path segments

      if (parts.size() <= 1)
      {
        // /coverages or /coverages/
        handleLandingPage(theRequest, theResponse);
      }
      else if (parts[1] == "conformance")
      {
        handleConformance(theRequest, theResponse);
      }
      else if (parts[1] == "collections")
      {
        if (parts.size() == 2)
        {
          // /coverages/collections
          handleCollections(theRequest, theResponse);
        }
        else if (parts.size() == 3)
        {
          // /coverages/collections/{collectionId}
          handleCollection(theRequest, theResponse, parts[2]);
        }
        else if (parts.size() == 4)
        {
          if (parts[3] == "schema")
            handleSchema(theRequest, theResponse, parts[2]);
          else if (parts[3] == "outline")
            handleOutline(theRequest, theResponse, parts[2]);
          else if (parts[3] == "coverage")
            handleCoverage(theRequest, theResponse, parts[2]);
          else
            notFound(theRequest, theResponse);
        }
        else
          notFound(theRequest, theResponse);
      }
      else if (parts[1] == "api" && itsApi &&
               Spine::OpenAPI::Publisher::isApiResource(theRequest.getResource(), COVERAGES_ROOT))
      {
        // The OpenAPI definition and its documentation
        itsApi->handle(theRequest,
                       theResponse,
                       COVERAGES_ROOT,
                       Spine::OpenAPI::serverUrl(theRequest, COVERAGES_ROOT));
      }
      else
        notFound(theRequest, theResponse);
    }
    catch (...)
    {
      Fmi::Exception exception(BCP, "Request processing exception!", nullptr);
      exception.addParameter("URI", theRequest.getURI());
      exception.addParameter("ClientIP", theRequest.getClientIP());
      exception.addParameter("HostName",
                             Spine::HostInfo::getHostName(theRequest.getClientIP()));

      const bool check_token = false;
      auto apikey = Spine::FmiApiKey::getFmiApiKey(theRequest, check_token);
      exception.addParameter("Apikey", (apikey ? *apikey : std::string("-")));

      exception.printError();

      std::string msg = exception.what();
      boost::algorithm::replace_all(msg, "\n", " ");
      msg = redactPaths(msg);
      msg = msg.substr(0, 300);

      // OGC API - Common: an RFC 7807 problem details body. The X-Download-Error header is
      // kept for the clients which read the error from it.
      setProblemResponse(theResponse, Spine::HTTP::Status::bad_request, "Bad Request", msg);
      theResponse.setHeader("X-Download-Error", msg);
    }
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief OGC API landing page
 */
// ----------------------------------------------------------------------

void CoveragesHandler::handleLandingPage(const Spine::HTTP::Request &theRequest,
                                         Spine::HTTP::Response &theResponse)
{
  const auto base = Spine::OpenAPI::serverUrl(theRequest, COVERAGES_ROOT);

  Json::Value doc;
  doc["title"] = "SmartMet Server - OGC API Coverages";
  doc["description"] = "OGC API Coverages interface to SmartMet meteorological data";
  auto &links = doc["links"];
  links.append(makeLink(base, "self", "application/json", "This document"));
  if (itsApi)
  {
    links.append(Spine::OpenAPI::serviceDescLink(base));
    if (itsApi->settings().ui)
      links.append(Spine::OpenAPI::serviceDocLink(base));
  }
  links.append(makeLink(base + "/conformance",
                        Spine::OpenAPI::REL_CONFORMANCE,
                        "application/json",
                        "Conformance declaration"));
  links.append(makeLink(base + "/collections", "data", "application/json", "Collections"));

  setJsonResponse(theResponse, doc);
}

// ----------------------------------------------------------------------
/*!
 * \brief OGC API conformance declaration
 */
// ----------------------------------------------------------------------

void CoveragesHandler::handleConformance(const Spine::HTTP::Request & /* theRequest */,
                                         Spine::HTTP::Response &theResponse)
{
  Json::Value doc;
  auto &classes = doc["conformsTo"];
  classes = Json::Value(Json::arrayValue);
  for (const auto &uri : Coverages::conformanceClasses())
    classes.append(uri);
  setJsonResponse(theResponse, doc);
}

// ----------------------------------------------------------------------
/*!
 * \brief The description of a collection
 */
// ----------------------------------------------------------------------

namespace
{
string isoTime(const Fmi::DateTime& t)
{
  return Fmi::date_time::to_iso_extended_string(t) + "Z";
}

// ISO 8601 duration of a time step in minutes
string isoDuration(long minutes)
{
  if (minutes > 0 && minutes % 1440 == 0)
    return "P" + Fmi::to_string(minutes / 1440) + "D";
  if (minutes > 0 && minutes % 60 == 0)
    return "PT" + Fmi::to_string(minutes / 60) + "H";
  return "PT" + Fmi::to_string(minutes) + "M";
}

// The constant step of sorted times in minutes, or 0 if the steps vary
long timeStep(const std::list<Fmi::DateTime>& times)
{
  long step = 0;
  const Fmi::DateTime* previous = nullptr;
  for (const auto& t : times)
  {
    if (previous != nullptr)
    {
      const long minutes = (t - *previous).total_seconds() / 60;
      if (step == 0)
        step = minutes;
      else if (minutes != step)
        return 0;
    }
    previous = &t;
  }
  return step;
}

Json::Value temporalExtent(const std::list<Fmi::DateTime>& times, const Fmi::DateTime& origin)
{
  Json::Value temporal;
  if (times.empty())
    return temporal;
  Json::Value interval(Json::arrayValue);
  interval.append(isoTime(times.front()));
  interval.append(isoTime(times.back()));
  temporal["interval"].append(interval);
  temporal["trs"] = "http://www.opengis.net/def/uom/ISO-8601/0/Gregorian";
  temporal["grid"]["cellsCount"] = static_cast<Json::UInt64>(times.size());
  const auto step = timeStep(times);
  if (step > 0)
    temporal["grid"]["resolution"] = isoDuration(step);
  else
    for (const auto& t : times)
      temporal["grid"]["coordinates"].append(isoTime(t));
  if (!origin.is_not_a_date_time())
    temporal["x-smartmet-origintime"] = isoTime(origin);
  return temporal;
}

Json::Value bboxValue(double xmin, double ymin, double xmax, double ymax)
{
  Json::Value bbox(Json::arrayValue);
  for (double v : {xmin, ymin, xmax, ymax})
    bbox.append(v);
  return bbox;
}

// The extent of querydata. The native grid is described in the x-smartmet-grid extension:
// the WKT of the projection, the grid size and resolution and the corners.
Json::Value querydataExtent(const Engine::Querydata::MetaData& md)
{
  Json::Value extent;
  auto& spatial = extent["spatial"];
  const auto& lon = md.wgs84Envelope.getRangeLon();
  const auto& lat = md.wgs84Envelope.getRangeLat();
  spatial["bbox"].append(bboxValue(lon.getMin(), lat.getMin(), lon.getMax(), lat.getMax()));
  spatial["crs"] = "http://www.opengis.net/def/crs/OGC/1.3/CRS84";

  auto& grid = extent["x-smartmet-grid"];
  grid["wkt"] = md.WKT;
  grid["width"] = md.xNumber;
  grid["height"] = md.yNumber;
  grid["resolution_km"].append(md.xResolution);
  grid["resolution_km"].append(md.yResolution);
  auto& corners = grid["corners"];
  const auto corner = [](double x, double y)
  {
    Json::Value c(Json::arrayValue);
    c.append(x);
    c.append(y);
    return c;
  };
  corners["bottomleft"] = corner(md.bllon, md.bllat);
  corners["bottomright"] = corner(md.brlon, md.brlat);
  corners["topleft"] = corner(md.ullon, md.ullat);
  corners["topright"] = corner(md.urlon, md.urlat);

  extent["temporal"] = temporalExtent(md.times, md.originTime);

  if (md.levels.size() > 1)
  {
    auto& vertical = extent["vertical"];
    double vmin = md.levels.front().value;
    double vmax = vmin;
    for (const auto& level : md.levels)
    {
      vmin = std::min<double>(vmin, level.value);
      vmax = std::max<double>(vmax, level.value);
      vertical["grid"]["coordinates"].append(level.value);
    }
    Json::Value interval(Json::arrayValue);
    interval.append(vmin);
    interval.append(vmax);
    vertical["interval"].append(interval);
    vertical["x-smartmet-leveltype"] = md.levels.front().type;
  }
  return extent;
}

// The extent of grid data: the union of the geometries of the latest generation, each
// described in x-smartmet-grids
Json::Value gridExtent(const std::list<Engine::Grid::MetaData>& metadata)
{
  Json::Value extent;
  if (metadata.empty())
    return extent;

  // The latest generation
  std::string latest;
  for (const auto& md : metadata)
    latest = std::max(latest, md.analysisTime);

  double xmin = 180;
  double ymin = 90;
  double xmax = -180;
  double ymax = -90;
  std::set<std::string> times;
  std::set<int> levels;
  std::set<int> geometries;
  std::string level_name;
  Json::Value grids(Json::arrayValue);
  for (const auto& md : metadata)
  {
    if (md.analysisTime != latest)
      continue;
    // The west edge from the left corners and the east edge from the right ones: taking the
    // extremes of all the corners fails for grids crossing the antimeridian, and for global
    // grids whose corners are at 0 and 359.9 (= -0.1) degrees
    const double west = std::min(md.latlon_bottomLeft.x(), md.latlon_topLeft.x());
    double east = std::max(md.latlon_bottomRight.x(), md.latlon_topRight.x());
    if (east <= west)
      east += 360;
    if (east - west >= 359)
    {
      xmin = -180;
      xmax = 180;
    }
    else
    {
      // A box crossing the antimeridian has minx > maxx, as in OGC API - Common
      xmin = std::min(xmin, west);
      xmax = std::max(xmax, east > 180 ? east - 360 : east);
    }
    for (const auto& c :
         {md.latlon_bottomLeft, md.latlon_bottomRight, md.latlon_topLeft, md.latlon_topRight})
    {
      ymin = std::min(ymin, c.y());
      ymax = std::max(ymax, c.y());
    }
    times.insert(md.times.begin(), md.times.end());
    if (level_name.empty() || level_name == md.levelName)
    {
      level_name = md.levelName;
      levels.insert(md.levels.begin(), md.levels.end());
    }
    if (!geometries.insert(md.geometryId).second)
      continue;
    Json::Value grid;
    grid["geometry"] = md.geometryId;
    grid["projection"] = md.projectionName;
    grid["wkt"] = md.wkt;
    grid["width"] = md.xNumber;
    grid["height"] = md.yNumber;
    // The corners in longitude and latitude, as for querydata
    const auto corner = [](const T::Coordinate& c)
    {
      Json::Value v(Json::arrayValue);
      v.append(c.x());
      v.append(c.y());
      return v;
    };
    grid["corners"]["bottomleft"] = corner(md.latlon_bottomLeft);
    grid["corners"]["bottomright"] = corner(md.latlon_bottomRight);
    grid["corners"]["topleft"] = corner(md.latlon_topLeft);
    grid["corners"]["topright"] = corner(md.latlon_topRight);
    grids.append(grid);
  }
  if (ymin > ymax)
    return extent;

  extent["spatial"]["bbox"].append(bboxValue(xmin, ymin, xmax, ymax));
  extent["spatial"]["crs"] = "http://www.opengis.net/def/crs/OGC/1.3/CRS84";
  extent["x-smartmet-grids"] = grids;

  std::list<Fmi::DateTime> parsed;
  for (const auto& t : times)
  {
    try
    {
      parsed.push_back(Fmi::DateTime::from_iso_string(t));
    }
    catch (...)
    {
    }
  }
  Fmi::DateTime origin;
  try
  {
    origin = Fmi::DateTime::from_iso_string(latest);
  }
  catch (...)
  {
  }
  extent["temporal"] = temporalExtent(parsed, origin);

  if (levels.size() > 1)
  {
    auto& vertical = extent["vertical"];
    Json::Value interval(Json::arrayValue);
    interval.append(*levels.begin());
    interval.append(*levels.rbegin());
    vertical["interval"].append(interval);
    for (int level : levels)
      vertical["grid"]["coordinates"].append(level);
    vertical["x-smartmet-leveltype"] = level_name;
  }
  return extent;
}

Json::Value collectionDescription(const string& base,
                                  const string& id,
                                  const Producer* producer,
                                  const Json::Value& extent = Json::Value())
{
  const auto href = base + "/collections/" + id;

  Json::Value doc;
  doc["id"] = id;
  doc["title"] = id;
  auto &links = doc["links"];
  links.append(makeLink(href, "self", "application/json"));
  links.append(
      makeLink(href + "/coverage", "http://www.opengis.net/def/rel/ogc/1.0/coverage", "application/netcdf"));

  if (producer == nullptr)
    return doc;

  doc["description"] = "Coverage collection for producer " + id;
  links.append(makeLink(href + "/schema", "describedby", "application/json"));
  doc["extent"] = (extent.isObject() ? extent : Json::Value(Json::objectValue));

  // Output formats the producer allows
  auto &formats = doc["outputFormats"];
  std::set<string> media_types;
  for (const auto &format : Coverages::outputFormats())
    if (format.second != "application/x-fmi-querydata" || !producer->disabledReqParam("format"))
      if (media_types.insert(format.second).second)
        formats.append(format.second);

  doc["crs"].append("http://www.opengis.net/def/crs/OGC/1.3/CRS84");
  return doc;
}
}  // namespace

// ----------------------------------------------------------------------
/*!
 * \brief The extent of a collection from the querydata or grid engine metadata
 *
 * Null if neither engine knows the producer.
 */
// ----------------------------------------------------------------------

Json::Value CoveragesHandler::extent(const std::string& collectionId) const
{
  try
  {
    if (itsQEngine != nullptr)
    {
      Engine::Querydata::MetaQueryOptions options;
      options.setProducer(collectionId);
      auto metadata = itsQEngine->getEngineMetadata(options);
      if (!metadata.empty())
        return querydataExtent(metadata.back());
    }
    if (itsGridEngine != nullptr && itsGridEngine->isEnabled())
    {
      auto metadata = itsGridEngine->getEngineMetadata(collectionId.c_str());
      if (!metadata.empty())
        return gridExtent(metadata);
    }
    return {};
  }
  catch (...)
  {
    // Missing metadata must not break the collection listing
    return {};
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief List available collections (producers)
 */
// ----------------------------------------------------------------------

void CoveragesHandler::handleCollections(const Spine::HTTP::Request &theRequest,
                                         Spine::HTTP::Response &theResponse)
{
  const auto base = Spine::OpenAPI::serverUrl(theRequest, COVERAGES_ROOT);

  Json::Value doc;
  doc["links"].append(makeLink(base + "/collections", "self", "application/json"));
  auto &collections = doc["collections"];
  collections = Json::Value(Json::arrayValue);
  for (const auto &entry : itsConfig->getProducers())
    collections.append(
        collectionDescription(base, entry.first, &entry.second, extent(entry.first)));

  setJsonResponse(theResponse, doc);
}

// ----------------------------------------------------------------------
/*!
 * \brief Single collection metadata
 */
// ----------------------------------------------------------------------

void CoveragesHandler::handleCollection(const Spine::HTTP::Request &theRequest,
                                        Spine::HTTP::Response &theResponse,
                                        const std::string &collectionId)
{
  const auto base = Spine::OpenAPI::serverUrl(theRequest, COVERAGES_ROOT);
  const auto &producers = itsConfig->getProducers();
  auto it = producers.find(collectionId);

  // A collection not in the configuration is still allowed (the querydata engine may know
  // the producer even without explicit download.conf configuration), with minimal metadata
  const Producer *producer = (it == producers.end() ? nullptr : &it->second);

  setJsonResponse(theResponse,
                  collectionDescription(base, collectionId, producer, extent(collectionId)));
}

// ----------------------------------------------------------------------
/*!
 * \brief Collection schema (field/parameter descriptions)
 */
// ----------------------------------------------------------------------

// ----------------------------------------------------------------------
/*!
 * \brief The parameters of a collection from the engine metadata
 *
 * By lower case name. Empty if neither engine
 * knows the producer.
 */
// ----------------------------------------------------------------------

std::map<std::string, CoveragesHandler::ParameterInfo> CoveragesHandler::parameters(
    const std::string& collectionId) const
{
  std::map<std::string, ParameterInfo> ret;
  try
  {
    if (itsQEngine != nullptr)
    {
      Engine::Querydata::MetaQueryOptions options;
      options.setProducer(collectionId);
      auto metadata = itsQEngine->getEngineMetadata(options);
      if (!metadata.empty())
      {
        for (const auto& p : metadata.back().parameters)
          ret[boost::algorithm::to_lower_copy(p.name)] = {
              p.name, Fmi::latin1_to_utf8(p.description), ""};
        return ret;
      }
    }
    if (itsGridEngine != nullptr && itsGridEngine->isEnabled())
    {
      for (const auto& md : itsGridEngine->getEngineMetadata(collectionId.c_str()))
        for (const auto& p : md.parameters)
          ret[boost::algorithm::to_lower_copy(p.parameterName)] = {
              p.parameterName, p.parameterDescription, p.parameterUnits};
    }
  }
  catch (...)
  {
    // Missing metadata must not break the schema
  }
  return ret;
}

void CoveragesHandler::handleSchema(const Spine::HTTP::Request& /* theRequest */,
                                    Spine::HTTP::Response& theResponse,
                                    const std::string& collectionId)
{
  // The parameters of the producer, all configured parameters if the engines do not know it
  auto available = parameters(collectionId);

  // Build field schema from the parameter configuration tables.
  // Use the NetCDF table as the primary source since it has CF standard names,
  // long names and units.  Supplement with GRIB table for parameters that
  // only appear there.

  const auto &ncTable = itsConfig->getParamChangeTable(false);  // NetCDF
  const auto &gribTable = itsConfig->getParamChangeTable(true); // GRIB

  // Collect unique parameters by newbase ID to avoid duplicates
  std::map<long, const ParamChangeItem *> params;

  // NetCDF entries first (richer metadata)
  for (const auto &item : ncTable)
  {
    long id = item.itsWantedParam.GetIdent();
    if (id > 0)
      params[id] = &item;
  }

  // GRIB entries for parameters not already covered
  for (const auto &item : gribTable)
  {
    long id = item.itsWantedParam.GetIdent();
    if (id > 0 && params.find(id) == params.end())
      params[id] = &item;
  }

  Json::Value doc;
  doc["$schema"] = "https://json-schema.org/draft/2020-12/schema";
  doc["type"] = "object";
  auto &properties = doc["properties"];
  properties = Json::Value(Json::objectValue);

  for (const auto &entry : params)
  {
    const auto &item = *entry.second;
    string name = item.itsWantedParam.GetName().CharPtr();
    if (name.empty())
      continue;

    if (!available.empty())
    {
      auto pos = available.find(boost::algorithm::to_lower_copy(name));
      if (pos == available.end())
        continue;
      available.erase(pos);
    }

    auto &property = properties[name];
    property["type"] = "number";
    property["x-fmi-id"] = static_cast<Json::Int64>(entry.first);
    if (!item.itsLongName.empty())
      property["title"] = item.itsLongName;
    if (!item.itsStdName.empty())
      property["x-ogc-propertyName"] = item.itsStdName;
    if (!item.itsUnit.empty())
      property["x-ogc-unit"] = item.itsUnit;
  }

  // Parameters of the data without a GRIB or NetCDF mapping
  for (const auto& item : available)
  {
    const auto& info = item.second;
    auto& property = properties[info.name];
    property["type"] = "number";
    if (!info.description.empty())
      property["title"] = info.description;
    if (!info.unit.empty())
      property["x-ogc-unit"] = info.unit;
  }

  setJsonResponse(theResponse, doc);
}

// ----------------------------------------------------------------------
/*!
 * \brief The area and the size of a coverage before it is downloaded
 *
 * /coverages/collections/{id}/outline?bbox=..&crs=..&scale-size=..&datetime=..&properties=..
 * returns a GeoJSON Feature: the polygon (longitude and latitude) the output
 * grid covers, and the rectangle in the output CRS, the grid size, the number
 * of values and their size as 32 bit floats. As in the coverage requests, the
 * bbox gives the corners of the area in longitude and latitude, and the area
 * is the rectangle between them in the output CRS. Sizes not given by
 * scale-size are estimated from the native resolution.
 */
// ----------------------------------------------------------------------

namespace
{
// The OGR spatial reference of a crs parameter or a WKT, in longitude-latitude axis order
std::unique_ptr<OGRSpatialReference> spatialReference(const string& desc)
{
  string d = desc;
  const string epsgPrefix = "http://www.opengis.net/def/crs/EPSG/0/";
  if (d.find(epsgPrefix) == 0)
    d = "EPSG:" + d.substr(epsgPrefix.size());
  else if (d.find("CRS84") != string::npos)
    d = "EPSG:4326";
  auto srs = std::make_unique<OGRSpatialReference>();
  if (srs->SetFromUserInput(d.c_str()) != OGRERR_NONE)
    throw Fmi::Exception(BCP, "Unsupported crs '" + desc + "'");
  srs->SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
  return srs;
}

struct Transformation
{
  std::unique_ptr<OGRCoordinateTransformation> ct;
  bool apply(double& x, double& y) const { return ct && ct->Transform(1, &x, &y) != 0; }
};

Transformation transformation(const OGRSpatialReference& from, const OGRSpatialReference& to)
{
  Transformation t;
  t.ct.reset(OGRCreateCoordinateTransformation(&from, &to));
  if (!t.ct)
    throw Fmi::Exception(BCP, "Failed to create a coordinate transformation");
  return t;
}

// Minutes of an ISO 8601 duration PTnM, PTnH or PnD, 0 if it is none of these
long durationMinutes(const string& d)
{
  try
  {
    if (d.size() > 3 && d.compare(0, 2, "PT") == 0 && d.back() == 'M')
      return std::stol(d.substr(2, d.size() - 3));
    if (d.size() > 3 && d.compare(0, 2, "PT") == 0 && d.back() == 'H')
      return 60 * std::stol(d.substr(2, d.size() - 3));
    if (d.size() > 2 && d[0] == 'P' && d.back() == 'D')
      return 1440 * std::stol(d.substr(1, d.size() - 2));
  }
  catch (...)
  {
  }
  return 0;
}

std::size_t countItems(const string& list)
{
  vector<string> parts;
  boost::split(parts, list, boost::is_any_of(","));
  return std::count_if(
      parts.begin(), parts.end(), [](const string& p) { return !boost::trim_copy(p).empty(); });
}
}  // namespace

void CoveragesHandler::handleOutline(const Spine::HTTP::Request& theRequest,
                                     Spine::HTTP::Response& theResponse,
                                     const std::string& collectionId)
{
  try
  {
    const auto ext = extent(collectionId);
    if (!ext.isObject())
    {
      setProblemResponse(theResponse,
                         Spine::HTTP::Status::not_found,
                         "Not Found",
                         "No metadata for collection '" + collectionId + "'");
      return;
    }

    // The native grid
    Json::Value native;
    if (ext.isMember("x-smartmet-grid"))
      native = ext["x-smartmet-grid"];
    else if (ext["x-smartmet-grids"].isArray() && ext["x-smartmet-grids"].size() > 0)
      native = ext["x-smartmet-grids"][0];

    // The output CRS: the crs option, the native grid, or longitude-latitude
    const auto crs = Spine::optional_string(theRequest.getParameter("crs"), "");
    std::unique_ptr<OGRSpatialReference> target;
    if (!crs.empty())
      target = spatialReference(crs);
    else if (native["wkt"].isString() && !native["wkt"].asString().empty())
      target = spatialReference(native["wkt"].asString());
    else
      target = spatialReference("EPSG:4326");
    auto lonlat = spatialReference("EPSG:4326");
    const auto forward = transformation(*lonlat, *target);
    const auto inverse = transformation(*target, *lonlat);

    // The rectangle in the output CRS
    double x1 = 0;
    double y1 = 0;
    double x2 = 0;
    double y2 = 0;
    const auto bbox = Spine::optional_string(theRequest.getParameter("bbox"), "");
    const auto& corners = native["corners"];
    if (!bbox.empty())
    {
      vector<string> parts;
      boost::split(parts, bbox, boost::is_any_of(","));
      if (parts.size() != 4)
        throw Fmi::Exception(BCP, "bbox must be minlon,minlat,maxlon,maxlat");
      x1 = Fmi::stod(boost::trim_copy(parts[0]));
      y1 = Fmi::stod(boost::trim_copy(parts[1]));
      x2 = Fmi::stod(boost::trim_copy(parts[2]));
      y2 = Fmi::stod(boost::trim_copy(parts[3]));
      if (!forward.apply(x1, y1) || !forward.apply(x2, y2))
        throw Fmi::Exception(BCP, "The bbox cannot be expressed in the output crs");
    }
    else if (crs.empty() && corners.isObject())
    {
      x1 = corners["bottomleft"][0].asDouble();
      y1 = corners["bottomleft"][1].asDouble();
      x2 = corners["topright"][0].asDouble();
      y2 = corners["topright"][1].asDouble();
      if (!forward.apply(x1, y1) || !forward.apply(x2, y2))
        throw Fmi::Exception(BCP, "The native grid cannot be expressed in its own crs");
    }
    else
    {
      // The extent of the native area in the output CRS
      const auto& b = ext["spatial"]["bbox"][0];
      x1 = y1 = std::numeric_limits<double>::max();
      x2 = y2 = std::numeric_limits<double>::lowest();
      const int n = 32;
      for (int i = 0; i <= n; i++)
        for (int edge = 0; edge < 4; edge++)
        {
          const double f = static_cast<double>(i) / n;
          double lon = b[0].asDouble() + f * (b[2].asDouble() - b[0].asDouble());
          double lat = b[1].asDouble() + f * (b[3].asDouble() - b[1].asDouble());
          if (edge == 0)
            lat = b[1].asDouble();
          else if (edge == 1)
            lat = b[3].asDouble();
          else if (edge == 2)
            lon = b[0].asDouble();
          else
            lon = b[2].asDouble();
          if (forward.apply(lon, lat))
          {
            x1 = std::min(x1, lon);
            y1 = std::min(y1, lat);
            x2 = std::max(x2, lon);
            y2 = std::max(y2, lat);
          }
        }
      if (x1 > x2)
        throw Fmi::Exception(BCP, "The area cannot be expressed in the output crs");
    }
    // A global longitude-latitude grid has its corners at 0 and 359.9 (= -0.1) degrees
    if (target->IsGeographic() && bbox.empty() && x2 <= x1)
      x2 += 360;
    if (x1 > x2)
      std::swap(x1, x2);
    if (y1 > y2)
      std::swap(y1, y2);

    // The outline in longitude and latitude
    Json::Value ring(Json::arrayValue);
    const int n = 32;
    for (int edge = 0; edge < 4; edge++)
      for (int i = 0; i < n; i++)
      {
        const double f = static_cast<double>(i) / n;
        double x = (edge == 0   ? x1 + f * (x2 - x1)
                    : edge == 1 ? x2
                    : edge == 2 ? x2 - f * (x2 - x1)
                                : x1);
        double y = (edge == 0   ? y1
                    : edge == 1 ? y1 + f * (y2 - y1)
                    : edge == 2 ? y2
                                : y2 - f * (y2 - y1));
        if (!inverse.apply(x, y))
          continue;
        Json::Value point(Json::arrayValue);
        point.append(x);
        point.append(y);
        ring.append(point);
      }
    if (ring.size() > 0)
      ring.append(ring[0]);

    // The size of the grid: given, or estimated from the native resolution
    Json::Value grid;
    const auto scaleSize = Spine::optional_string(theRequest.getParameter("scale-size"), "");
    if (!scaleSize.empty())
    {
      vector<string> sizes;
      // A named string: boost::split of Boost 1.69 takes no temporaries
      const string parsed = parseScaleSize(scaleSize);
      boost::split(sizes, parsed, boost::is_any_of(","));
      if (sizes.size() == 2)
      {
        grid["width"] = Fmi::stoi(sizes[0]);
        grid["height"] = Fmi::stoi(sizes[1]);
        grid["estimated"] = false;
      }
    }
    else if (crs.empty() && bbox.empty() && native.isMember("width"))
    {
      grid["width"] = native["width"];
      grid["height"] = native["height"];
      grid["estimated"] = false;
    }
    else if (native["resolution_km"].isArray())
    {
      // Kilometres to the units of the output CRS
      double km = native["resolution_km"][0].asDouble();
      double unit = (target->IsGeographic() ? 111.32 : target->GetLinearUnits() / 1000.0);
      if (km > 0 && unit > 0)
      {
        const double step = km / unit;
        grid["width"] = std::max(1, static_cast<int>(std::lround((x2 - x1) / step)) + 1);
        grid["height"] = std::max(1, static_cast<int>(std::lround((y2 - y1) / step)) + 1);
        grid["estimated"] = true;
      }
    }

    // The number of times, parameters and levels
    const auto& temporal = ext["temporal"];
    std::size_t times = temporal["grid"]["cellsCount"].asUInt64();
    const auto datetime = Spine::optional_string(theRequest.getParameter("datetime"), "");
    const long step = durationMinutes(temporal["grid"]["resolution"].asString());
    if (!datetime.empty())
    {
      const auto slash = datetime.find('/');
      if (slash == string::npos)
        times = 1;
      else if (step > 0)
      {
        try
        {
          const auto& interval = temporal["interval"][0];
          auto t1 = datetime.substr(0, slash);
          auto t2 = datetime.substr(slash + 1);
          if (t1 == ".." || t1.empty())
            t1 = interval[0].asString();
          if (t2 == ".." || t2.empty())
            t2 = interval[1].asString();
          const auto start =
              std::max(Fmi::TimeParser::parse(t1), Fmi::TimeParser::parse(interval[0].asString()));
          const auto end =
              std::min(Fmi::TimeParser::parse(t2), Fmi::TimeParser::parse(interval[1].asString()));
          const long minutes = (end - start).total_seconds() / 60;
          times = (minutes < 0 ? 0 : static_cast<std::size_t>(minutes / step) + 1);
        }
        catch (...)
        {
          // The count of the whole data stays as an upper limit
        }
      }
    }
    const auto properties = Spine::optional_string(theRequest.getParameter("properties"), "");
    const std::size_t params =
        (properties.empty() ? parameters(collectionId).size() : countItems(properties));
    const auto levelsOption = Spine::optional_string(theRequest.getParameter("levels"), "");
    std::size_t levels = std::max<std::size_t>(1, ext["vertical"]["grid"]["coordinates"].size());
    if (!levelsOption.empty())
      levels = countItems(levelsOption);

    Json::Value doc;
    doc["type"] = "Feature";
    doc["geometry"]["type"] = "Polygon";
    doc["geometry"]["coordinates"].append(ring);
    auto& props = doc["properties"];
    props["crs"] = (crs.empty() ? (native.isMember("wkt") ? "native" : "EPSG:4326") : crs);
    for (double v : {x1, y1, x2, y2})
      props["bbox"].append(v);
    props["times"] = static_cast<Json::UInt64>(times);
    props["parameters"] = static_cast<Json::UInt64>(params);
    props["levels"] = static_cast<Json::UInt64>(levels);
    if (grid.isObject())
    {
      props["grid"] = grid;
      const double values = grid["width"].asDouble() * grid["height"].asDouble() *
                            static_cast<double>(times * params * levels);
      props["values"] = values;
      props["bytes"] = values * 4;
    }
    setJsonResponse(theResponse, doc);
    theResponse.setHeader("Content-Type", "application/geo+json");
  }
  catch (...)
  {
    // The request handler answers with problem details
    throw Fmi::Exception::Trace(BCP, "Outline request failed");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Coverage data retrieval
 *
 *        Translates OGC API Coverages parameters to the download
 *        plugin's internal format and delegates to createStreamer().
 *
 *        Supported parameters:
 *          properties  → field/parameter selection
 *          bbox        → spatial subsetting (same format)
 *          datetime    → temporal subsetting (ISO 8601 range)
 *          subset      → n-dimensional subsetting (axis ranges)
 *          scale-size  → grid resampling to target cell count
 *          scale-factor → grid stepping (uniform downsampling)
 *          scale-axes  → per-axis resolution
 *          crs         → output CRS (EPSG codes or OGC URIs)
 *          f           → output format (MIME type or short name)
 */
// ----------------------------------------------------------------------

void CoveragesHandler::handleCoverage(const Spine::HTTP::Request &theRequest,
                                      Spine::HTTP::Response &theResponse,
                                      const std::string &collectionId)
{
  try
  {
    // Translate OGC parameters to download-equivalent request
    auto dlReq = translateRequest(theRequest, collectionId);

    // Fill ReqParams from the translated request
    ReqParams reqParams;
    const auto &producer = fillReqParams(dlReq, reqParams, *itsConfig, *itsQEngine, itsGridEngine);

    // Construct Query from the translated request
    auto query = Query(dlReq, itsGridEngine, reqParams.originTime, reqParams.test);

    // Determine start/end times
    Fmi::DateTime startTime;
    Fmi::DateTime endTime;

    if (!reqParams.startTime.empty())
      startTime = query.tOptions.startTime;
    if (!reqParams.endTime.empty())
      endTime = query.tOptions.endTime;

    // Create and initialize the streamer
    string filename;
    theResponse.setContent(createStreamer(dlReq,
                                         *itsConfig,
                                         *itsQEngine,
                                         itsGridEngine,
                                         itsGeoEngine,
                                         reqParams,
                                         producer,
                                         query,
                                         startTime,
                                         endTime,
                                         filename));

    theResponse.setStatus(Spine::HTTP::Status::ok);

    // Set appropriate MIME type based on output format
    theResponse.setHeader("Content-Type", getMimeType(reqParams.outputFormat));
    theResponse.setHeader("Content-Disposition", string("attachment; filename=") + filename);
    theResponse.setHeader("Access-Control-Allow-Origin", "*");

    // Cache headers
    const int expires_seconds = 60;
    Fmi::DateTime t_now = Fmi::SecondClock::universal_time();
    Fmi::DateTime t_expires = t_now + Fmi::Seconds(expires_seconds);
    std::shared_ptr<Fmi::TimeFormatter> tformat(Fmi::TimeFormatter::create("http"));

    theResponse.setHeader("Cache-Control",
                          ("public, max-age=" + Fmi::to_string(expires_seconds)));
    theResponse.setHeader("Expires", tformat->format(t_expires));
    theResponse.setHeader("Last-Modified", tformat->format(t_now));
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "handleCoverage failed!");
  }
}

}  // namespace Download
}  // namespace Plugin
}  // namespace SmartMet

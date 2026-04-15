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
#include "Query.h"
#include "StreamerFactory.h"
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <macgyver/DateTime.h>
#include <macgyver/Exception.h>
#include <macgyver/StringConversion.h>
#include <macgyver/TimeFormatter.h>
#include <spine/Convenience.h>
#include <spine/FmiApiKey.h>
#include <spine/HostInfo.h>
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

static vector<string> splitPath(const string &resource)
{
  vector<string> parts;
  boost::split(parts, resource, boost::is_any_of("/"));

  // Remove empty segments (leading slash produces one)
  parts.erase(
      std::remove_if(parts.begin(), parts.end(), [](const string &s) { return s.empty(); }),
      parts.end());

  return parts;
}

// ----------------------------------------------------------------------
/*!
 * \brief Set JSON response headers
 */
// ----------------------------------------------------------------------

static void setJsonResponse(Spine::HTTP::Response &theResponse,
                            const string &content)
{
  theResponse.setContent(content);
  theResponse.setStatus(Spine::HTTP::Status::ok);
  theResponse.setHeader("Content-Type", "application/json");
  theResponse.setHeader("Access-Control-Allow-Origin", "*");
}

// ----------------------------------------------------------------------
/*!
 * \brief Map OGC output format to download format string
 *
 *        Supports both MIME types (f=application/x-grib2) and short
 *        names (f=GRIB2, f=netcdf) for convenience.
 */
// ----------------------------------------------------------------------

static string mapOutputFormat(const string &f)
{
  string fl = Fmi::ascii_tolower_copy(f);

  if (fl == "application/x-grib2" || fl == "grib2")
    return "GRIB2";
  if (fl == "application/x-grib" || fl == "grib1" || fl == "grib")
    return "GRIB1";
  if (fl == "application/netcdf" || fl == "application/x-netcdf" || fl == "netcdf")
    return "NETCDF";
  if (fl == "application/x-fmi-querydata" || fl == "qd" || fl == "querydata")
    return "QD";

  throw Fmi::Exception(BCP, "Unsupported output format: " + f);
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

static void parseSubset(const string &subset,
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

    // Remove optional quotes from values
    boost::replace_all(value, "\"", "");
    boost::replace_all(value, "'", "");

    if (axisName == "pressure" || axisName == "height" || axisName == "level")
    {
      // Range: low:high or single value
      auto colonPos = value.find(':');
      if (colonPos != string::npos)
      {
        string low = value.substr(0, colonPos);
        string high = value.substr(colonPos + 1);
        boost::trim(low);
        boost::trim(high);

        if (!low.empty())
          dlReq.setParameter("minlevel", low);
        if (!high.empty())
          dlReq.setParameter("maxlevel", high);
      }
      else
      {
        // Single level
        dlReq.setParameter("level", value);
      }
    }
    else if (axisName == "time")
    {
      // Range: t1:t2 or single value
      auto colonPos = value.find(':');
      if (colonPos != string::npos)
      {
        string t1 = value.substr(0, colonPos);
        string t2 = value.substr(colonPos + 1);
        boost::trim(t1);
        boost::trim(t2);

        if (!t1.empty())
          dlReq.setParameter("starttime", t1);
        if (!t2.empty())
          dlReq.setParameter("endtime", t2);
      }
      else
      {
        dlReq.setParameter("starttime", value);
        dlReq.setParameter("endtime", value);
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

static string parseScaleSize(const string &scaleSize)
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

static string parseScaleFactor(const string &scaleFactor)
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

static void parseDatetime(const string &datetime,
                          Spine::HTTP::Request &dlReq)
{
  auto slashPos = datetime.find('/');

  if (slashPos == string::npos)
  {
    // Single instant
    dlReq.setParameter("starttime", datetime);
    dlReq.setParameter("endtime", datetime);
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

static Spine::HTTP::Request translateRequest(const Spine::HTTP::Request &theRequest,
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
    // Map OGC CRS URIs to EPSG codes
    // http://www.opengis.net/def/crs/EPSG/0/4326 → epsg:4326
    string proj = *crs;
    const string epsgPrefix = "http://www.opengis.net/def/crs/EPSG/0/";
    if (proj.find(epsgPrefix) == 0)
      proj = "epsg:" + proj.substr(epsgPrefix.size());

    // Also handle http://www.opengis.net/def/crs/OGC/1.3/CRS84
    if (proj.find("CRS84") != string::npos)
      proj = "epsg:4326";

    dlReq.setParameter("projection", proj);
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

static string getMimeType(OutputFormat fmt)
{
  switch (fmt)
  {
    case Grib1:
      return "application/x-grib";
    case Grib2:
      return "application/x-grib2";
    case NetCdf:
      return "application/netcdf";
    case QD:
      return "application/x-fmi-querydata";
    default:
      return "application/octet-stream";
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

static const Producer &fillReqParams(const Spine::HTTP::Request &req,
                                     ReqParams &reqParams,
                                     Config &config,
                                     const Engine::Querydata::Engine &qEngine,
                                     const Engine::Grid::Engine *gridEngine)
{
  try
  {
    // Data source
    static Producer dummyProducer;
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
      else if (!(gridEngine->isEnabled()))
        throw Fmi::Exception(BCP, "Grid data is disabled");
    }

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

    // Levels (already translated by parseSubset)
    reqParams.minLevel = Spine::optional_int(req.getParameter("minlevel"), -1);
    reqParams.maxLevel = Spine::optional_int(req.getParameter("maxlevel"), -1);
    reqParams.minHeight = reqParams.maxHeight = -1;

    // Datum
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

    // Projection (already translated by translateRequest from crs)
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

    // Bounding box (same format in both APIs)
    reqParams.bbox = reqParams.origBBox = Spine::optional_string(req.getParameter("bbox"), "");

    if (!reqParams.bbox.empty())
      reqParams.bboxRect = nPairsOfValues<double>(reqParams.bbox, "bbox", 2);

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

    // Output format
    reqParams.format = Spine::optional_string(req.getParameter("format"), "NETCDF");
    Fmi::ascii_toupper(reqParams.format);

    if (reqParams.format == "GRIB1")
      reqParams.outputFormat = Grib1;
    else if (reqParams.format == "GRIB2")
      reqParams.outputFormat = Grib2;
    else if (reqParams.format == "NETCDF")
      reqParams.outputFormat = NetCdf;
    else if (reqParams.format == "QD")
    {
      if (reqParams.dataSource != QueryData)
        throw Fmi::Exception(BCP, "Querydata format not supported with grid data");
      reqParams.outputFormat = QD;
    }
    else
      throw Fmi::Exception(BCP, "Unsupported output format: " + reqParams.format);

    // Packing (pass through if given)
    reqParams.packing = Spine::optional_string(req.getParameter("packing"), "");
    Fmi::ascii_tolower(reqParams.packing);

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

void CoveragesHandler::requestHandler(Spine::Reactor & /* theReactor */,
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
          else if (parts[3] == "coverage")
            handleCoverage(theRequest, theResponse, parts[2]);
          else
          {
            theResponse.setStatus(Spine::HTTP::Status::not_found);
            theResponse.setContent("Unknown resource: " + theRequest.getResource());
          }
        }
        else
        {
          theResponse.setStatus(Spine::HTTP::Status::not_found);
          theResponse.setContent("Unknown resource: " + theRequest.getResource());
        }
      }
      else
      {
        theResponse.setStatus(Spine::HTTP::Status::not_found);
        theResponse.setContent("Unknown resource: " + theRequest.getResource());
      }
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

      theResponse.setStatus(Spine::HTTP::Status::bad_request);

      std::string msg = exception.what();
      boost::algorithm::replace_all(msg, "\n", " ");
      msg = msg.substr(0, 300);
      theResponse.setHeader("X-Download-Error", msg.c_str());
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

void CoveragesHandler::handleLandingPage(const Spine::HTTP::Request & /* theRequest */,
                                         Spine::HTTP::Response &theResponse)
{
  // TODO: Generate from configuration; include proper links with server URL

  string json =
      "{\n"
      "  \"title\": \"SmartMet Server - OGC API Coverages\",\n"
      "  \"description\": \"OGC API Coverages interface to SmartMet meteorological data\",\n"
      "  \"links\": [\n"
      "    {\n"
      "      \"href\": \"/coverages\",\n"
      "      \"rel\": \"self\",\n"
      "      \"type\": \"application/json\",\n"
      "      \"title\": \"This document\"\n"
      "    },\n"
      "    {\n"
      "      \"href\": \"/coverages/conformance\",\n"
      "      \"rel\": \"http://www.opengis.net/def/rel/ogc/1.0/conformance\",\n"
      "      \"type\": \"application/json\",\n"
      "      \"title\": \"Conformance declaration\"\n"
      "    },\n"
      "    {\n"
      "      \"href\": \"/coverages/collections\",\n"
      "      \"rel\": \"data\",\n"
      "      \"type\": \"application/json\",\n"
      "      \"title\": \"Collections\"\n"
      "    }\n"
      "  ]\n"
      "}\n";

  setJsonResponse(theResponse, json);
}

// ----------------------------------------------------------------------
/*!
 * \brief OGC API conformance declaration
 */
// ----------------------------------------------------------------------

void CoveragesHandler::handleConformance(const Spine::HTTP::Request & /* theRequest */,
                                         Spine::HTTP::Response &theResponse)
{
  string json =
      "{\n"
      "  \"conformsTo\": [\n"
      "    \"http://www.opengis.net/spec/ogcapi-common-1/1.0/conf/core\",\n"
      "    \"http://www.opengis.net/spec/ogcapi-common-2/1.0/conf/collections\",\n"
      "    \"http://www.opengis.net/spec/ogcapi-coverages-1/1.0/conf/core\",\n"
      "    \"http://www.opengis.net/spec/ogcapi-coverages-1/1.0/conf/subsetting\",\n"
      "    \"http://www.opengis.net/spec/ogcapi-coverages-1/1.0/conf/fieldselection\",\n"
      "    \"http://www.opengis.net/spec/ogcapi-coverages-1/1.0/conf/scaling\",\n"
      "    \"http://www.opengis.net/spec/ogcapi-coverages-1/1.0/conf/crs\"\n"
      "  ]\n"
      "}\n";

  setJsonResponse(theResponse, json);
}

// ----------------------------------------------------------------------
/*!
 * \brief List available collections (producers)
 */
// ----------------------------------------------------------------------

void CoveragesHandler::handleCollections(const Spine::HTTP::Request & /* theRequest */,
                                         Spine::HTTP::Response &theResponse)
{
  // TODO: Build from Config producer list.  Each configured producer
  //       becomes a collection.  For now return a placeholder.

  string json =
      "{\n"
      "  \"links\": [\n"
      "    {\n"
      "      \"href\": \"/coverages/collections\",\n"
      "      \"rel\": \"self\",\n"
      "      \"type\": \"application/json\"\n"
      "    }\n"
      "  ],\n"
      "  \"collections\": []\n"
      "}\n";

  setJsonResponse(theResponse, json);
}

// ----------------------------------------------------------------------
/*!
 * \brief Single collection metadata
 */
// ----------------------------------------------------------------------

void CoveragesHandler::handleCollection(const Spine::HTTP::Request & /* theRequest */,
                                        Spine::HTTP::Response &theResponse,
                                        const std::string &collectionId)
{
  // TODO: Look up producer from Config and build proper extent/CRS/parameter metadata

  string json =
      "{\n"
      "  \"id\": \"" + collectionId + "\",\n"
      "  \"title\": \"" + collectionId + "\",\n"
      "  \"description\": \"Coverage collection for producer " + collectionId + "\",\n"
      "  \"links\": [\n"
      "    {\n"
      "      \"href\": \"/coverages/collections/" + collectionId + "\",\n"
      "      \"rel\": \"self\",\n"
      "      \"type\": \"application/json\"\n"
      "    },\n"
      "    {\n"
      "      \"href\": \"/coverages/collections/" + collectionId + "/coverage\",\n"
      "      \"rel\": \"http://www.opengis.net/def/rel/ogc/1.0/coverage\",\n"
      "      \"type\": \"application/netcdf\"\n"
      "    },\n"
      "    {\n"
      "      \"href\": \"/coverages/collections/" + collectionId + "/schema\",\n"
      "      \"rel\": \"describedby\",\n"
      "      \"type\": \"application/json\"\n"
      "    }\n"
      "  ],\n"
      "  \"extent\": {},\n"
      "  \"crs\": [\n"
      "    \"http://www.opengis.net/def/crs/OGC/1.3/CRS84\"\n"
      "  ]\n"
      "}\n";

  setJsonResponse(theResponse, json);
}

// ----------------------------------------------------------------------
/*!
 * \brief Collection schema (field/parameter descriptions)
 */
// ----------------------------------------------------------------------

void CoveragesHandler::handleSchema(const Spine::HTTP::Request & /* theRequest */,
                                    Spine::HTTP::Response &theResponse,
                                    const std::string & /* collectionId */)
{
  // TODO: Build from parameter configuration (grib.json / netcdf.json)

  string json =
      "{\n"
      "  \"$schema\": \"https://json-schema.org/draft/2020-12/schema\",\n"
      "  \"type\": \"object\",\n"
      "  \"properties\": {}\n"
      "}\n";

  setJsonResponse(theResponse, json);
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
    Fmi::DateTime startTime, endTime;

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
    theResponse.setHeader("Content-Disposition",
                          (string("attachment; filename=") + filename).c_str());
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

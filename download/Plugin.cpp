// ======================================================================
/*!
 * \brief SmartMet download service plugin implementation
 */
// ======================================================================

#include "Plugin.h"
#include "Query.h"

#include "DataStreamer.h"
#include "GribStreamer.h"
#include "NetCdfStreamer.h"
#include "QueryDataStreamer.h"

#include <spine/Exception.h>
#include <spine/Convenience.h>
#include <spine/Table.h>
#include <spine/SmartMet.h>
#include <spine/Reactor.h>

#include <macgyver/StringConversion.h>
#include <macgyver/TimeFormatter.h>
#include <macgyver/TimeZoneFactory.h>

#include <boost/algorithm/string/replace.hpp>
#include <boost/bind.hpp>
#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/foreach.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>

#include <newbase/NFmiQueryData.h>
#include <newbase/NFmiAreaFactory.h>
#include <newbase/NFmiRotatedLatLonArea.h>

#include <macgyver/TimeParser.h>
#include <macgyver/HelmertTransformation.h>

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

#include <gdal/cpl_conv.h>

using namespace std;
using namespace boost::posix_time;
using namespace boost::gregorian;
using namespace boost::local_time;

namespace SmartMet
{
namespace Plugin
{
namespace Download
{
// ----------------------------------------------------------------------
/*!
 * \brief Can the plugin handle the parameter?
 */
// ----------------------------------------------------------------------

bool special(const SmartMet::Spine::Parameter &theParam)
{
  try
  {
    switch (theParam.type())
    {
      case SmartMet::Spine::Parameter::Type::Data:
      case SmartMet::Spine::Parameter::Type::Landscaped:
        return false;
      case SmartMet::Spine::Parameter::Type::DataDerived:
      case SmartMet::Spine::Parameter::Type::DataIndependent:
        return true;
    }
    // ** NOT REACHED **
    return true;
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Return pairs of values from comma separated string
 */
// ----------------------------------------------------------------------

template <typename T>
boost::optional<vector<pair<T, T>>> nPairsOfValues(string &pvs, const char *param, size_t nPairs)
{
  try
  {
    boost::optional<vector<pair<T, T>>> pvalue;
    boost::trim(pvs);

    if (pvs.empty())
      return pvalue;

    try
    {
      std::vector<std::string> flds;
      boost::split(flds, pvs, boost::is_any_of(","));
      size_t nValues = 2 * nPairs;

      if (flds.size() != nValues)
        throw SmartMet::Spine::Exception(
            BCP, string("Invalid value for parameter '") + param + "': '" + pvs + "'");

      size_t n;

      for (n = 0; (n < nValues); n++)
      {
        boost::trim(flds[n]);

        if (flds[n].empty())
          throw SmartMet::Spine::Exception(
              BCP, string("Invalid value for parameter '") + param + "': '" + pvs + "'");
      }

      vector<pair<T, T>> pvv;
      size_t np;

      for (np = 0, n = 0; (n < nValues); np++, n += 2)
        pvv.push_back(
            make_pair<T, T>(boost::lexical_cast<T>(flds[n]), boost::lexical_cast<T>(flds[n + 1])));

      pvalue = pvv;

      return pvalue;
    }
    catch (...)
    {
    }

    throw SmartMet::Spine::Exception(
        BCP, string("Invalid value for parameter '") + param + "': '" + pvs + "'");
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Get projection type
 */
// ----------------------------------------------------------------------

static ProjType getProjectionType(ReqParams &reqParams)
{
  try
  {
    typedef struct
    {
      const char *proj;
      AreaClassId acid;
    } projareas;

    static projareas projections[] = {{"epsg:", A_Native},
                                      {"latlon", A_LatLon},
                                      {"rotlatlon", A_RotLatLon},
                                      {"stereographic", A_PolarStereoGraphic},
                                      {"mercator", A_Mercator},
                                      {NULL, A_Native}};

    // If request datum is 'epsg', check epsg projection for implied datum shift to wgs84.

    bool checkDatum = (reqParams.datumShift == SmartMet::Plugin::Download::Datum::EPSG);

    if (checkDatum)
      reqParams.datumShift = SmartMet::Plugin::Download::Datum::None;

    reqParams.areaClassId = A_Native;

    if (reqParams.projection.empty())
      return P_Native;

    string proj(Fmi::ascii_tolower_copy(reqParams.projection));
    unsigned int i = 0;

    for (; projections[i].proj; i++)
      if (proj.find(projections[i].proj) == 0)
      {
        if (i == 0)
        {
          try
          {
            // epsg:n. Latlon is handled as newbase projection to enable cropping.
            //
            // We do not check other/projected coordsystem's projection parameters to
            // detect if they match querydata's native projection; cropping is not possible.
            //
            OGRSpatialReference srs;
            OGRErr err;

            reqParams.epsgCode =
                boost::lexical_cast<EpsgCode>(proj.substr(strlen(projections[i].proj)));

            if ((err = srs.importFromEPSG(reqParams.epsgCode)) != OGRERR_NONE)
              throw SmartMet::Spine::Exception(BCP,
                                               "srs.importFromEPSG(" +
                                                   boost::lexical_cast<string>(reqParams.epsgCode) +
                                                   ") error " + boost::lexical_cast<string>(err));

            if (checkDatum)
            {
              const char *datum = srs.GetAttrValue("DATUM");

              if (Fmi::ascii_toupper_copy(string(datum ? datum : "")) ==
                  SmartMet::Plugin::Download::Datum::epsgWGS84DatumName)
                reqParams.datumShift = SmartMet::Plugin::Download::Datum::WGS84;
            }

            if (!srs.IsProjected())
            {
              reqParams.projection = "latlon";
              return getProjectionType(reqParams);
            }

            return P_Epsg;
          }
          catch (...)
          {
            break;
          }
        }
        else
        {
          reqParams.areaClassId = projections[i].acid;
          return ProjType(P_Native + i);
        }
      }

    throw SmartMet::Spine::Exception(BCP, "Unsupported projection '" + reqParams.projection + "'");
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Get request parameters.
 */
// ----------------------------------------------------------------------

string getRequestParam(const SmartMet::Spine::HTTP::Request &req,
                       const Producer &producer,
                       const char *urlParam,
                       string defaultValue)
{
  try
  {
    string str = (producer.disabledReqParam(urlParam)
                      ? defaultValue
                      : SmartMet::Spine::optional_string(req.getParameter(urlParam), defaultValue));
    boost::trim(str);
    return str;
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

int getRequestInt(const SmartMet::Spine::HTTP::Request &req,
                  const Producer &producer,
                  const char *urlParam,
                  int defaultValue)
{
  try
  {
    return (producer.disabledReqParam(urlParam)
                ? defaultValue
                : SmartMet::Spine::optional_int(req.getParameter(urlParam), defaultValue));
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

unsigned long getRequestUInt(const SmartMet::Spine::HTTP::Request &req,
                             const Producer &producer,
                             const char *urlParam,
                             uint defaultValue)
{
  try
  {
    return (producer.disabledReqParam(urlParam) ? defaultValue
                                                : SmartMet::Spine::optional_unsigned_long(
                                                      req.getParameter(urlParam), defaultValue));
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

static const Producer &getRequestParams(const SmartMet::Spine::HTTP::Request &req,
                                        ReqParams &reqParams,
                                        Config &config,
                                        const SmartMet::Engine::Querydata::Engine &qEngine)
{
  try
  {
    // Producer is speficied using 'model' or 'producer' keyword.

    string model = getRequestParam(req, config.defaultProducer(), "model", "");
    reqParams.producer = getRequestParam(req, config.defaultProducer(), "producer", "");

    if (!reqParams.producer.empty())
    {
      if ((!model.empty()) && (model != reqParams.producer))
        throw SmartMet::Spine::Exception(BCP, "Cannot specify model and producer simultaneously");
    }
    else
      reqParams.producer = (model.empty() ? config.defaultProducerName() : model);

    const Producer &producer = config.getProducer(reqParams.producer, qEngine);

    if (reqParams.producer.empty())
      throw SmartMet::Spine::Exception(BCP, "No producer");

    // Time related parameters. Detect special value 'data'.

    reqParams.startTime = getRequestParam(req, producer, "starttime", "");
    reqParams.endTime = getRequestParam(req, producer, "endtime", "");
    reqParams.originTime = getRequestParam(req, producer, "origintime", "");
    reqParams.timeSteps = getRequestUInt(req, producer, "timesteps", 0);
    reqParams.maxTimeSteps = getRequestUInt(req, producer, "maxtimesteps", 0);

    string timeStepStr = getRequestParam(req, producer, "timestep", "");
    reqParams.timeStep =
        ((timeStepStr != "data") ? getRequestUInt(req, producer, "timestep", 0) : 0);

    if (reqParams.startTime == "data")
      reqParams.startTime.clear();
    if (reqParams.endTime == "data")
      reqParams.endTime.clear();
    if (reqParams.originTime == "data")
      reqParams.originTime.clear();

    // Level (pressure/hPa or hybrid or height level) and height (meters) ranges/limits
    //
    // Note: height (meters) range query currently not implemented

    reqParams.minLevel = getRequestInt(req, producer, "minlevel", -1);
    reqParams.maxLevel = getRequestInt(req, producer, "maxlevel", -1);

    reqParams.minHeight = reqParams.maxHeight = -1;

    // Datum handling

    reqParams.datum = getRequestParam(req, producer, "datum", "");

    if (!SmartMet::Plugin::Download::Datum::parseDatumShift(reqParams.datum, reqParams.datumShift))
      throw SmartMet::Spine::Exception(BCP, "Invalid datum selected");

    // Projection, bounding and grid size/step

    reqParams.projection = getRequestParam(req, producer, "projection", "");
    reqParams.projType = getProjectionType(reqParams);

    if ((reqParams.projType == P_Epsg) &&
        (reqParams.datumShift == SmartMet::Plugin::Download::Datum::None))
      // gdal/proj4 needed for projection
      //
      reqParams.datumShift = SmartMet::Plugin::Download::Datum::FMI;

    reqParams.bbox = reqParams.origBBox = getRequestParam(req, producer, "bbox", "");
    reqParams.gridCenter = getRequestParam(req, producer, "gridcenter", "");
    reqParams.gridSize = getRequestParam(req, producer, "gridsize", "");
    reqParams.gridResolution = getRequestParam(req, producer, "gridresolution", "");
    reqParams.gridStep = getRequestParam(req, producer, "gridstep", "");

    if (!reqParams.bbox.empty())
      // Bottom left lon,lat and top right lon,lat; bllon,bllat,trlon,trlat
      //
      reqParams.bboxRect = nPairsOfValues<double>(reqParams.bbox, "bbox", 2);

    if (!reqParams.gridCenter.empty())
    {
      // Grid center lon,lat and width and height in km; lon,lat,width,height
      //
      if (reqParams.bboxRect)
        throw SmartMet::Spine::Exception(BCP, "Cannot specify gridcenter and bbox simultaneously");

      reqParams.gridCenterLL = nPairsOfValues<double>(reqParams.gridCenter, "gridcenter", 2);
    }

    if (!reqParams.gridSize.empty())
      // Absolute grid size; nx,ny
      //
      reqParams.gridSizeXY = nPairsOfValues<unsigned int>(reqParams.gridSize, "gridsize", 1);

    if (!reqParams.gridResolution.empty())
    {
      // Grid cell size; width,height in km
      //
      if (reqParams.gridSizeXY)
        throw SmartMet::Spine::Exception(
            BCP, "Cannot specify gridsize and gridresolution simultaneously");

      reqParams.gridResolutionXY =
          nPairsOfValues<double>(reqParams.gridResolution, "gridresolution", 1);
    }

    if (!reqParams.gridStep.empty())
      // Grid step to extract every dx'th/dy'th value; dx,dy
      //
      reqParams.gridStepXY = nPairsOfValues<unsigned int>(reqParams.gridStep, "gridstep", 1);

    // Output format

    reqParams.format = getRequestParam(req, producer, "format", "");
    Fmi::ascii_toupper(reqParams.format);

    if (reqParams.format == "GRIB1")
      reqParams.outputFormat = Grib1;
    else if (reqParams.format == "GRIB2")
      reqParams.outputFormat = Grib2;
    else if (reqParams.format == "NETCDF")
      reqParams.outputFormat = NetCdf;
    else if (reqParams.format == "QD")
      reqParams.outputFormat = QD;
    else if (reqParams.format.empty())
      throw SmartMet::Spine::Exception(BCP, "No format selected");
    else
      throw SmartMet::Spine::Exception(BCP, "Invalid format selected");

    if ((reqParams.outputFormat == QD) && (!reqParams.gridStep.empty()))
      throw SmartMet::Spine::Exception(BCP, "Cannot specify gridstep when using qd format");

    // Packing type for grib. Set to grib as given (converted to lowercase only)

    reqParams.packing = getRequestParam(req, producer, "packing", "");
    Fmi::ascii_tolower(reqParams.packing);

    if ((!reqParams.packing.empty()) && (reqParams.outputFormat != Grib1) &&
        (reqParams.outputFormat != Grib2))
      throw SmartMet::Spine::Exception(BCP, "Packing can be specified with grib format only");

    // Tables version for grib2

    reqParams.grib2TablesVersion =
        ((reqParams.outputFormat == Grib2) ? getRequestUInt(req, producer, "tablesversion", 0) : 0);

    if (reqParams.grib2TablesVersion > 0)
    {
      // Check against valid range. The default range is [0-0] letting all values thru
      //
      auto range = config.getGrib2TablesVersionRange();
      auto grib2TablesVersionMin = range.first, grib2TablesVersionMax = range.second;

      if ((grib2TablesVersionMax > 0) && ((reqParams.grib2TablesVersion < grib2TablesVersionMin) ||
                                          (reqParams.grib2TablesVersion > grib2TablesVersionMax)))
        throw SmartMet::Spine::Exception(BCP,
                                         "'tablesversion' must be between " +
                                             Fmi::to_string(grib2TablesVersionMin) + " and " +
                                             Fmi::to_string(grib2TablesVersionMax));
    }

    // For misc testing
    //

    reqParams.test = getRequestUInt(req, producer, "test", 0);

    return producer;
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Get parameter scale factor and offset
 */
// ----------------------------------------------------------------------

static bool getScaleFactorAndOffset(signed long id,
                                    float *scale,
                                    float *offset,
                                    const ParamChangeTable &ptable)
{
  try
  {
    for (size_t i = 0; i < ptable.size(); ++i)
    {
      // Note: conversion in reverse direction!
      if (id == ptable[i].itsWantedParam.GetIdent())
      {
        *scale = ptable[i].itsConversionScale;
        *offset = ptable[i].itsConversionBase;
        return true;
      }
    }

    return false;
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Check that configuration exists for the requested parameters
 *		and get scale and offset for grib output.
 *
 * 		Unknown (and special) parameters are ignored.
 *
 * 		Returns true if all parameters are known. The known
 * 		parameters are set to knownParams.
 */
// ----------------------------------------------------------------------

static bool getParamConfig(const ParamChangeTable &pTable,
                           const SmartMet::Spine::OptionParsers::ParameterList &reqParams,
                           SmartMet::Spine::OptionParsers::ParameterList &knownParams,
                           Scaling &scaling)
{
  try
  {
    knownParams.clear();

    if (pTable.empty())
      return false;

    std::list<unsigned int> missingParams;
    float scale = 1.0, offset = 0.0;
    unsigned int i = 0;
    bool ok;

    BOOST_FOREACH (SmartMet::Spine::Parameter param, reqParams)
    {
      if ((ok = (!special(param))))
      {
        int id = param.number();

        if (id >= 0)
        {
          unsigned long lid = boost::numeric_cast<unsigned long>(id);
          ok = getScaleFactorAndOffset(lid, &scale, &offset, pTable);
        }
      }

      if (!ok)
        missingParams.push_back(i);
      else
        scaling.push_back(Scaling::value_type(scale, offset));

      i++;
    }

    std::list<unsigned int>::const_iterator itm = missingParams.begin();
    i = 0;

    BOOST_FOREACH (SmartMet::Spine::Parameter param, reqParams)
    {
      if ((itm != missingParams.end()) && (i == *itm))
        itm++;
      else
        knownParams.push_back(param);

      i++;
    }

    return (missingParams.size() == 0);
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Get download file name.
 *
 */
// ----------------------------------------------------------------------

static string getDownloadFileName(const string &producer,
                                  const ptime &originTime,
                                  const ptime &startTime,
                                  const ptime &endTime,
                                  const string &projection,
                                  OutputFormat outputFormat)
{
  try
  {
    string sTime, eTime;

    if (startTime.is_not_a_date_time())
      sTime = "start";
    else
      sTime = Fmi::to_iso_string(startTime);

    if (endTime.is_not_a_date_time())
      eTime = "end";
    else
      eTime = Fmi::to_iso_string(endTime);

    const char *extn;

    if (outputFormat == Grib1)
      extn = ".grb";
    else if (outputFormat == Grib2)
      extn = ".grb2";
    else if (outputFormat == NetCdf)
      extn = ".nc";
    else
      extn = ".sqd";

    return producer + "_" + Fmi::to_iso_string(originTime) + "_" + sTime + "_" + eTime +
           (!projection.empty() ? ("_" + projection) : "") + extn;
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Initialize data streamer for chunking
 *
 */
// ----------------------------------------------------------------------

static boost::shared_ptr<DataStreamer> initializeStreamer(
    const SmartMet::Spine::HTTP::Request &req,
    const SmartMet::Engine::Querydata::Engine &qEngine,
    const SmartMet::Engine::Geonames::Engine *geoEngine,
    Query &query,
    Config &config,
    string &fileName)
{
  try
  {
    // Get request parameters.

    ReqParams reqParams;
    const auto &producer = getRequestParams(req, reqParams, config, qEngine);

    // Create format specific streamer and get scaling information for the requested parameters.
    // Unknown (and special) parameters are ignored.

    boost::shared_ptr<DataStreamer> ds;
    SmartMet::Spine::OptionParsers::ParameterList knownParams;
    Scaling scaling;

    if ((reqParams.outputFormat == Grib1) || (reqParams.outputFormat == Grib2))
    {
      ds = boost::shared_ptr<DataStreamer>(new GribStreamer(
          req, config, producer, reqParams.outputFormat, reqParams.grib2TablesVersion));
      getParamConfig(
          config.getParamChangeTable(), query.pOptions.parameters(), knownParams, scaling);
    }
    else if (reqParams.outputFormat == NetCdf)
    {
      ds = boost::shared_ptr<DataStreamer>(new NetCdfStreamer(req, config, producer));
      getParamConfig(
          config.getParamChangeTable(false), query.pOptions.parameters(), knownParams, scaling);
    }
    else
    {
      ds = boost::shared_ptr<DataStreamer>(new QDStreamer(req, config, producer));

      BOOST_FOREACH (SmartMet::Spine::Parameter param, query.pOptions.parameters())
      {
        knownParams.push_back(param);
      }
    }

    if (knownParams.empty())
      throw SmartMet::Spine::Exception(
          BCP,
          "initStreamer: No known parameters available for producer '" + reqParams.producer + "'");

    if ((reqParams.outputFormat != QD) && (scaling.size() != knownParams.size()))
      throw SmartMet::Spine::Exception(BCP,
                                       "initStreamer: internal: Parameter/scaling data mismatch");

    ds->setParams(knownParams, scaling);

    // Set request parameters

    ds->setRequestParams(reqParams);

    // Set geonames

    ds->setGeonames(geoEngine);

    // Get Q object for the producer/origintime

    ptime originTime, startTime, endTime;

    SmartMet::Engine::Querydata::Q q;

    if (!reqParams.originTime.empty())
    {
      if (reqParams.originTime == "latest" || reqParams.originTime == "newest")
        originTime = boost::posix_time::ptime(boost::date_time::pos_infin);
      else if (reqParams.originTime == "oldest")
        originTime = boost::posix_time::ptime(boost::date_time::neg_infin);
      else
        originTime = Fmi::TimeParser::parse(reqParams.originTime);
      q = qEngine.get(reqParams.producer, originTime);
      originTime = q->originTime();
    }
    else
    {
      q = qEngine.get(reqParams.producer);
    }

    // Overwrite timeparsers's starttime (now --> data), endtime (starttime + 24h --> data) and
    // timestep (60m --> data) defaults.
    // However, if 'now' request parameter is set, use the parsed starttime.

    auto now = getRequestParam(req, producer, "now", "");

    if ((!reqParams.startTime.empty()) || (!now.empty()))
      startTime = query.tOptions.startTime;

    if (!reqParams.endTime.empty())
      endTime = query.tOptions.endTime;

    query.tOptions.startTimeData = startTime.is_not_a_date_time();
    query.tOptions.timeStep = reqParams.timeStep;
    query.tOptions.endTimeData = (reqParams.endTime.empty() && (reqParams.timeStep == 0));

    // Generate list of validtimes for the data to be loaded

    ds->generateValidTimeList(q, query, originTime, startTime, endTime);

    // Set request levels

    ds->setLevels(query);

    // In order to set response status check if (any) data is available for the requested
    // levels, parameters and time range

    if (!ds->hasRequestedData(producer))
      throw SmartMet::Spine::Exception(
          BCP, "initStreamer: No data available for producer '" + reqParams.producer + "'");

    // Download file name

    fileName = getDownloadFileName(reqParams.producer,
                                   originTime,
                                   startTime,
                                   endTime,
                                   reqParams.projection,
                                   reqParams.outputFormat);

    // Set parameter and level iterators etc. to their start positions

    ds->resetDataSet();

    return ds;
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Perform a download service query
 */
// ----------------------------------------------------------------------

void Plugin::query(const SmartMet::Spine::HTTP::Request &req,
                   SmartMet::Spine::HTTP::Response &response)
{
  try
  {
    // asm volatile ("int3;");

    // Options

    Query query(req, itsConfig, itsQEngine);

    // Initialize streamer.

    string filename;
    response.setContent(
        initializeStreamer(req, *itsQEngine, itsGeoEngine, query, itsConfig, filename));

    string mime = "application/octet-stream";
    response.setHeader("Content-type", mime.c_str());
    response.setHeader("Content-Disposition",
                       (string("attachement; filename=") + filename).c_str());
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Main content handler
 */
// ----------------------------------------------------------------------

void Plugin::requestHandler(SmartMet::Spine::Reactor & /* theReactor */,
                            const SmartMet::Spine::HTTP::Request &theRequest,
                            SmartMet::Spine::HTTP::Response &theResponse)
{
  try
  {
    bool isdebug = false;

    try
    {
      const int expires_seconds = 60;
      ptime t_now = second_clock::universal_time();

      // Excecuting the query

      query(theRequest, theResponse);
      theResponse.setStatus(SmartMet::Spine::HTTP::Status::ok);

      // Defining the response header information

      ptime t_expires = t_now + seconds(expires_seconds);
      boost::shared_ptr<Fmi::TimeFormatter> tformat(Fmi::TimeFormatter::create("http"));
      std::string cachecontrol =
          "public, max-age=" + boost::lexical_cast<std::string>(expires_seconds);
      std::string expiration = tformat->format(t_expires);
      std::string modification = tformat->format(t_now);

      theResponse.setHeader("Cache-Control", cachecontrol.c_str());
      theResponse.setHeader("Expires", expiration.c_str());
      theResponse.setHeader("Last-Modified", modification.c_str());

      /* This will need some thought
      if(response.first.size() == 0)
      {
              std::cerr << "Warning: Empty input for request "
                                << theRequest.getOriginalQueryString()
                                << " from "
                                << theRequest.getClientIP()
                                << std::endl;
      }
      */
    }
    catch (...)
    {
      // Catching all exceptions

      SmartMet::Spine::Exception exception(BCP, "Request processing exception!", NULL);
      exception.addParameter("URI", theRequest.getURI());

      if (!exception.stackTraceDisabled())
        std::cerr << exception.getStackTrace();
      else if (!exception.loggingDisabled())
        std::cerr << "Error: " << exception.what() << std::endl;

      if (isdebug)
      {
        // Delivering the exception information as HTTP content
        std::string fullMessage = exception.getHtmlStackTrace();
        theResponse.setContent(fullMessage);
        theResponse.setStatus(SmartMet::Spine::HTTP::Status::ok);
      }
      else
      {
        theResponse.setStatus(SmartMet::Spine::HTTP::Status::bad_request);
      }

      // Adding the first exception information into the response header

      std::string msg = exception.what();
      boost::algorithm::replace_all(msg, "\n", " ");
      msg = msg.substr(0, 300);
      theResponse.setHeader("X-Download-Error", msg.c_str());
    }
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Plugin constructor
 */
// ----------------------------------------------------------------------

Plugin::Plugin(SmartMet::Spine::Reactor *theReactor, const char *theConfig)
    : SmartMetPlugin(), itsModuleName("Download"), itsConfig(theConfig), itsReactor(theReactor)
{
  try
  {
    if (theReactor->getRequiredAPIVersion() != SMARTMET_API_VERSION)
    {
      std::cerr << "*** Download Plugin and Server SmartMet API version mismatch ***" << std::endl;
      return;
    }
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Initializator
 */
// ----------------------------------------------------------------------
void Plugin::init()
{
  try
  {
    /* QEngine */

    auto *engine = itsReactor->getSingleton("Querydata", NULL);
    if (!engine)
      throw SmartMet::Spine::Exception(BCP, "Querydata engine unavailable");
    itsQEngine = reinterpret_cast<SmartMet::Engine::Querydata::Engine *>(engine);

    /* GeoEngine */

    engine = itsReactor->getSingleton("Geonames", NULL);
    if (!engine)
      throw SmartMet::Spine::Exception(BCP, "Geonames engine unavailable");
    itsGeoEngine = reinterpret_cast<SmartMet::Engine::Geonames::Engine *>(engine);

    itsConfig.init(itsQEngine);

    if (!itsReactor->addContentHandler(
            this, "/download", boost::bind(&Plugin::callRequestHandler, this, _1, _2, _3)))
      throw SmartMet::Spine::Exception(BCP, "Failed to register download content handler");
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Shutdown the plugin
 */
// ----------------------------------------------------------------------

void Plugin::shutdown()
{
  std::cout << "  -- Shutdown requested (dls)\n";
}

// ----------------------------------------------------------------------
/*!
 * \brief Destructor
 */
// ----------------------------------------------------------------------

Plugin::~Plugin()
{
}

// ----------------------------------------------------------------------
/*!
 * \brief Return the plugin name
 */
// ----------------------------------------------------------------------

const std::string &Plugin::getPluginName() const
{
  return itsModuleName;
}

// ----------------------------------------------------------------------
/*!
 * \brief Return the required version
 */
// ----------------------------------------------------------------------

int Plugin::getRequiredAPIVersion() const
{
  return SMARTMET_API_VERSION;
}

// ----------------------------------------------------------------------
/*!
 * \brief Performance query implementation.
 */
// ----------------------------------------------------------------------

bool Plugin::queryIsFast(const SmartMet::Spine::HTTP::Request & /* theRequest */) const
{
  return false;
}

}  // namespace Download
}  // namespace Plugin
}  // namespace SmartMet

/*
 * Server knows us through the 'SmartMetPlugin' virtual interface, which
 * the 'Plugin' class implements.
 */

extern "C" SmartMetPlugin *create(SmartMet::Spine::Reactor *them, const char *config)
{
  return new SmartMet::Plugin::Download::Plugin(them, config);
}

extern "C" void destroy(SmartMetPlugin *us)
{
  // This will call 'Plugin::~Plugin()' since the destructor is virtual
  delete us;
}

// ======================================================================

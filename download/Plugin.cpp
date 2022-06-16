// ======================================================================
/*!
 * \brief SmartMet download service plugin implementation
 */
// ======================================================================

#include "Plugin.h"
#include "DataStreamer.h"
#include "GribStreamer.h"
#include "NetCdfStreamer.h"
#include "Query.h"
#include "QueryDataStreamer.h"
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/bind.hpp>
#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/scoped_ptr.hpp>
#include <cmath>
#include <cpl_conv.h>
#include <iostream>
#include <limits>
#include <macgyver/Exception.h>
#include <macgyver/HelmertTransformation.h>
#include <macgyver/StringConversion.h>
#include <macgyver/TimeFormatter.h>
#include <macgyver/TimeParser.h>
#include <macgyver/TimeZoneFactory.h>
#include <newbase/NFmiQueryData.h>
#include <spine/Convenience.h>
#include <spine/Reactor.h>
#include <spine/SmartMet.h>
#include <spine/Table.h>
#include <stdexcept>

#include <cpl_conv.h>

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

bool special(const Spine::Parameter &theParam)
{
  try
  {
    switch (theParam.type())
    {
      case Spine::Parameter::Type::Data:
      case Spine::Parameter::Type::Landscaped:
        return false;
      case Spine::Parameter::Type::DataDerived:
      case Spine::Parameter::Type::DataIndependent:
        return true;
    }
    // ** NOT REACHED **
    return true;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Get projection type
 */
// ----------------------------------------------------------------------

static ProjType getProjectionType(ReqParams &reqParams, bool legacyMode)
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
                                      {"ykj", A_TransverseMercator},
                                      {"lcc", A_LambertConformalConic},
                                      {nullptr, A_Native}};

    // If request datum is 'epsg', check epsg projection for implied datum shift to wgs84.

    bool checkDatum = (reqParams.datumShift == Datum::DatumShift::Epsg);

    if (checkDatum)
      reqParams.datumShift = Datum::DatumShift::None;

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
              throw Fmi::Exception(BCP,
                                     "srs.importFromEPSG(" +
                                         boost::lexical_cast<string>(reqParams.epsgCode) +
                                         ") error " + boost::lexical_cast<string>(err));

            /* Explicit datum shift hasn't been, and should not be used
             *
             * Should check for "World Geodetic System 1984", not "WGS 84", and test if
             * reqParams.datumShift is set by request

            if (checkDatum)
            {
              const char *datum = srs.GetAttrValue("DATUM");

              if (Fmi::ascii_toupper_copy(string(datum ? datum : "")) == Datum::epsgWGS84DatumName)
                reqParams.datumShift = Datum::DatumShift::Wgs84;
            }
            */

            // In legacy mode geographic epsg projections (e.g. epsg:4326) are handled as
            // newbase latlon (just to enable cropping)

            if (true /*legacyMode*/ && (!srs.IsProjected()))
            {
              reqParams.projection = "latlon";
              return getProjectionType(reqParams, legacyMode);
            }

            return P_Epsg;
          }
          catch (...)
          {
            throw;
          }
        }
        else
        {
          reqParams.areaClassId = projections[i].acid;
          return ProjType(P_Native + i);
        }
      }

    throw Fmi::Exception(BCP, "Unsupported projection '" + reqParams.projection + "'");
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Get request parameters.
 */
// ----------------------------------------------------------------------

string getRequestParam(const Spine::HTTP::Request &req,
                       const Producer &producer,
                       const char *urlParam,
                       string defaultValue)
{
  try
  {
    string str = (producer.disabledReqParam(urlParam)
                      ? defaultValue
                      : Spine::optional_string(req.getParameter(urlParam), defaultValue));
    boost::trim(str);
    return str;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

int getRequestInt(const Spine::HTTP::Request &req,
                  const Producer &producer,
                  const char *urlParam,
                  int defaultValue)
{
  try
  {
    return (producer.disabledReqParam(urlParam)
                ? defaultValue
                : Spine::optional_int(req.getParameter(urlParam), defaultValue));
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

unsigned long getRequestUInt(const Spine::HTTP::Request &req,
                             const Producer &producer,
                             const char *urlParam,
                             uint defaultValue)
{
  try
  {
    return (producer.disabledReqParam(urlParam)
                ? defaultValue
                : Spine::optional_unsigned_long(req.getParameter(urlParam), defaultValue));
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

static const Producer &getRequestParams(const Spine::HTTP::Request &req,
                                        ReqParams &reqParams,
                                        Config &config,
                                        const Engine::Querydata::Engine &qEngine,
                                        const Engine::Grid::Engine *gridEngine)
{
  try
  {
    // Data source

    static Producer dummyProducer;
    reqParams.source = getRequestParam(req, dummyProducer, "source", "querydata");

    if (reqParams.source == "querydata")
      reqParams.dataSource = QueryData;
    else if (reqParams.source == "grid")
      reqParams.dataSource = Grid;
    else
      throw Fmi::Exception(BCP, "Unknown source '" + reqParams.source +
                             "', 'querydata' or 'grid' expected");

    if (reqParams.dataSource == Grid)
    {
      if (!gridEngine)
        throw Fmi::Exception(BCP, "Grid data is not available");
      else if (!(gridEngine->isEnabled()))
        throw Fmi::Exception(BCP, "Grid data is disabled");
    }

    // Producer is speficied using 'model' or 'producer' keyword.

    string model = getRequestParam(req, config.defaultProducer(), "model", "");
    reqParams.producer = getRequestParam(req, config.defaultProducer(), "producer", "");

    if (!reqParams.producer.empty())
    {
      if ((!model.empty()) && (model != reqParams.producer))
        throw Fmi::Exception(BCP, "Cannot specify model and producer simultaneously");
    }
    else
      reqParams.producer = (model.empty() ? config.defaultProducerName() : model);

    const Producer &producer = config.getProducer(reqParams.producer, qEngine);

    /*
    TODO: no qEngine dependency with grid data

    const Producer &producer = (reqParams.dataSource == QueryData)
      ? config.getProducer(reqParams.producer, qEngine)
      : config.getProducer(reqParams.producer, qEngine);
//    : dummyProducer;
    */

    if (reqParams.producer.empty())
      throw Fmi::Exception(BCP, "No producer");

    // For misc testing

    reqParams.test = getRequestUInt(req, producer, "test", 0);

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

    if (!Datum::parseDatumShift(reqParams.datum, reqParams.datumShift))
      throw Fmi::Exception(BCP, "Invalid datum selected");

    // Projection, bounding and grid size/step

    reqParams.projection = getRequestParam(req, producer, "projection", "");
    if (reqParams.dataSource == QueryData)
      reqParams.projType = getProjectionType(reqParams, config.getLegacyMode());

    if ((reqParams.projType == P_Epsg) && (reqParams.datumShift == Datum::DatumShift::None))
      // gdal/proj4 needed for projection
      //
      reqParams.datumShift = Datum::DatumShift::Fmi;

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
        throw Fmi::Exception(BCP, "Cannot specify gridcenter and bbox simultaneously");

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
        throw Fmi::Exception(BCP, "Cannot specify gridsize and gridresolution simultaneously");

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
    {
      if (reqParams.dataSource == Grid)
        throw Fmi::Exception(BCP, "Querydata format not supported with grid data");

      reqParams.outputFormat = QD;
    }
    else if (reqParams.format.empty())
      throw Fmi::Exception(BCP, "No format selected");
    else
      throw Fmi::Exception(BCP, "Invalid format selected");

    if ((reqParams.outputFormat == QD) && (!reqParams.gridStep.empty()))
      throw Fmi::Exception(BCP, "Cannot specify gridstep when using qd format");

    // Packing type for grib. Set to grib as given (converted to lowercase only)

    reqParams.packing = getRequestParam(req, producer, "packing", "");
    Fmi::ascii_tolower(reqParams.packing);

    if (!reqParams.packing.empty())
    {
      if ((reqParams.outputFormat != Grib1) && (reqParams.outputFormat != Grib2))
        throw Fmi::Exception(BCP, "Packing can be specified with grib format only")
            .addParameter("packing", reqParams.packing);

      auto msg = config.packingErrorMessage(reqParams.packing);
      if (!msg.empty())
        throw Fmi::Exception(BCP, msg).addParameter("packing", reqParams.packing);
    }

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
        throw Fmi::Exception(BCP,
                               "'tablesversion' must be between " +
                                   Fmi::to_string(grib2TablesVersionMin) + " and " +
                                   Fmi::to_string(grib2TablesVersionMax));
    }

    return producer;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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
                           const TimeSeries::OptionParsers::ParameterList &reqParams,
                           TimeSeries::OptionParsers::ParameterList &knownParams,
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

    BOOST_FOREACH (Spine::Parameter param, reqParams)
    {
      // We allow special params too if they have a number (WindUMS and WindVMS)
      bool ok = (param.number() > 0);

      if (ok)
      {
        int id = param.number();
        unsigned long lid = boost::numeric_cast<unsigned long>(id);
        ok = getScaleFactorAndOffset(lid, &scale, &offset, pTable);
      }

      if (!ok)
        missingParams.push_back(i);
      else
        scaling.push_back(Scaling::value_type(scale, offset));

      i++;
    }

    std::list<unsigned int>::const_iterator itm = missingParams.begin();
    i = 0;

    BOOST_FOREACH (Spine::Parameter param, reqParams)
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
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Initialize data streamer for chunking
 *
 */
// ----------------------------------------------------------------------

static boost::shared_ptr<DataStreamer> initializeStreamer(const Spine::HTTP::Request &req,
                                                          const Engine::Querydata::Engine &qEngine,
                                                          const Engine::Grid::Engine *gridEngine,
                                                          const Engine::Geonames::Engine *geoEngine,
                                                          Query &query,
                                                          Config &config,
                                                          string &fileName)
{
  try
  {
    // Get request parameters.

    ReqParams reqParams;
    const auto &producer = getRequestParams(req, reqParams, config, qEngine, gridEngine);

    // Create format specific streamer and get scaling information for the requested parameters.
    // Unknown (and special) parameters are ignored.

    boost::shared_ptr<DataStreamer> ds;
    TimeSeries::OptionParsers::ParameterList knownParams;
    Scaling scaling;

    if ((reqParams.outputFormat == Grib1) || (reqParams.outputFormat == Grib2))
    {
      ds = boost::shared_ptr<DataStreamer>(new GribStreamer(req, config, producer, reqParams));
      getParamConfig(
          config.getParamChangeTable(), query.pOptions.parameters(), knownParams, scaling);
    }
    else if (reqParams.outputFormat == NetCdf)
    {
      ds = boost::shared_ptr<DataStreamer>(new NetCdfStreamer(req, config, producer, reqParams));
      getParamConfig(
          config.getParamChangeTable(false), query.pOptions.parameters(), knownParams, scaling);
    }
    else
    {
      ds = boost::shared_ptr<DataStreamer>(new QDStreamer(req, config, producer, reqParams));

      BOOST_FOREACH (Spine::Parameter param, query.pOptions.parameters())
      {
        knownParams.push_back(param);
      }
    }

    if (knownParams.empty())
      throw Fmi::Exception(
          BCP,
          "initStreamer: No known parameters available for producer '" + reqParams.producer + "'");

    if ((reqParams.outputFormat != QD) && (scaling.size() != knownParams.size()))
      throw Fmi::Exception(BCP, "initStreamer: internal: Parameter/scaling data mismatch");

    ds->setParams(knownParams, scaling);

    // Set engines

    ds->setEngines(&qEngine, gridEngine, geoEngine);

    // Get Q object for the producer/origintime

    ptime originTime, startTime, endTime;

    Engine::Querydata::Q q;

    if (reqParams.dataSource == QueryData)
    {
      ds->setMultiFile(qEngine.getProducerConfig(reqParams.producer).ismultifile);

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
    }
    else
      ds->setMultiFile(producer.multiFile);

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

    if (reqParams.dataSource == QueryData)
    {
      // Generate list of validtimes for the data to be loaded.
      // For grid data validtimes are generated after checking data availability

      ds->generateValidTimeList(q, query, originTime, startTime, endTime);

      // Set request levels.
      // For grid data levels are set after checking data availability

      ds->setLevels(query);
    }

    // In order to set response status check if (any) data is available for the requested
    // levels, parameters and time range

    if (!ds->hasRequestedData(producer, query, originTime, startTime, endTime))
      throw Fmi::Exception(
          BCP, "initStreamer: No data available for producer '" + reqParams.producer + "'");

    // Download file name

    string projection = boost::algorithm::replace_all_copy(reqParams.projection, " ", "_");
    boost::algorithm::replace_all(projection, ",", ":");

    fileName = getDownloadFileName(reqParams.producer,
                                   originTime,
                                   startTime,
                                   endTime,
                                   projection,
                                   reqParams.outputFormat);

    // Set parameter and level iterators etc. to their start positions

    ds->resetDataSet();

    return ds;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Perform a download service query
 */
// ----------------------------------------------------------------------

void Plugin::query(const Spine::HTTP::Request &req, Spine::HTTP::Response &response)
{
  try
  {
    // asm volatile ("int3;");

    // Options

    Query query(req, itsConfig, itsQEngine);

    // Initialize streamer.

    string filename;
    response.setContent(
        initializeStreamer(req, *itsQEngine, itsGridEngine, itsGeoEngine, query, itsConfig, filename));

    string mime = "application/octet-stream";
    response.setHeader("Content-type", mime.c_str());
    response.setHeader("Content-Disposition",
                       (string("attachement; filename=") + filename).c_str());
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Main content handler
 */
// ----------------------------------------------------------------------

void Plugin::requestHandler(Spine::Reactor & /* theReactor */,
                            const Spine::HTTP::Request &theRequest,
                            Spine::HTTP::Response &theResponse)
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
      theResponse.setStatus(Spine::HTTP::Status::ok);

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

      Fmi::Exception exception(BCP, "Request processing exception!", nullptr);
      exception.addParameter("URI", theRequest.getURI());
      exception.addParameter("ClientIP", theRequest.getClientIP());
      exception.printError();

      if (isdebug)
      {
        // Delivering the exception information as HTTP content
        std::string fullMessage = exception.getHtmlStackTrace();
        theResponse.setContent(fullMessage);
        theResponse.setStatus(Spine::HTTP::Status::ok);
      }
      else
      {
        theResponse.setStatus(Spine::HTTP::Status::bad_request);
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
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Plugin constructor
 */
// ----------------------------------------------------------------------

Plugin::Plugin(Spine::Reactor *theReactor, const char *theConfig)
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
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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
    /* Dont't let the NetCDF library crash the server */
    ncopts = NC_VERBOSE;

    /* QEngine */

    auto *engine = itsReactor->getSingleton("Querydata", nullptr);
    if (!engine)
      throw Fmi::Exception(BCP, "Querydata engine unavailable");
    itsQEngine = reinterpret_cast<Engine::Querydata::Engine *>(engine);

    /* GridEngine */

    engine = itsReactor->getSingleton("grid", nullptr);
    itsGridEngine = reinterpret_cast<Engine::Grid::Engine *>(engine);

    /* GeoEngine */

    engine = itsReactor->getSingleton("Geonames", nullptr);
    if (!engine)
      throw Fmi::Exception(BCP, "Geonames engine unavailable");
    itsGeoEngine = reinterpret_cast<Engine::Geonames::Engine *>(engine);

    itsConfig.init(itsQEngine);

    if (!itsReactor->addContentHandler(
            this, "/download", boost::bind(&Plugin::callRequestHandler, this, _1, _2, _3)))
      throw Fmi::Exception(BCP, "Failed to register download content handler");
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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

Plugin::~Plugin() {}

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

bool Plugin::queryIsFast(const Spine::HTTP::Request & /* theRequest */) const
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

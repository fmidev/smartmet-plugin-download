// ======================================================================
/*!
 * \brief Download API request handler implementation
 */
// ======================================================================

#include "download/Handler.h"
#include "Query.h"
#include "StreamerFactory.h"
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/lexical_cast.hpp>
#include <macgyver/DateTime.h>
#include <macgyver/Exception.h>
#include <macgyver/StringConversion.h>
#include <macgyver/TimeFormatter.h>
#include <spine/Convenience.h>
#include <spine/FmiApiKey.h>
#include <spine/HostInfo.h>
#include <cpl_conv.h>

using namespace std;

namespace SmartMet
{
namespace Plugin
{
namespace Download
{
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

static string getRequestParam(const Spine::HTTP::Request &req,
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

static int getRequestInt(const Spine::HTTP::Request &req,
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

static unsigned long getRequestUInt(const Spine::HTTP::Request &req,
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
    else if (reqParams.source == "gridmapping")
      reqParams.dataSource = GridMapping;
    else if ((reqParams.source == "grid") || (reqParams.source == "gridcontent"))
    {
      reqParams.source = "gridcontent";
      reqParams.dataSource = GridContent;
    }
    else
      throw Fmi::Exception(BCP,
                           "Unknown source '" + reqParams.source +
                               "', 'querydata', 'gridmapping' or 'gridcontent' expected");

    if (reqParams.dataSource != QueryData)
    {
      if (!gridEngine)
        throw Fmi::Exception(BCP, "Grid data is not available");
      else if (!(gridEngine->isEnabled()))
        throw Fmi::Exception(BCP, "Grid data is disabled");
    }

    // Producer is speficied using 'model' or 'producer' keyword.

    string model = getRequestParam(req, config.defaultProducer(), "model", "");
    reqParams.producer = getRequestParam(req, config.defaultProducer(), "producer", "");

    auto gId = getRequestInt(req, config.defaultProducer(), "geometryid", -1);
    (void) gId;
    reqParams.geometryId = getRequestParam(req, config.defaultProducer(), "geometryid", "");

    if (reqParams.dataSource == GridContent)
    {
      // Common producer name is not used by data query, just setting some nonempty value.
      // Name is later taken from 1'st radon parameter name to be used in output file name

      if ((!model.empty()) || (!reqParams.producer.empty()))
        throw Fmi::Exception(BCP, "Cannot specify producer option with grid content data");

      reqParams.producer = "gridcontent";
    }
    else if (!reqParams.geometryId.empty())
      throw Fmi::Exception(BCP, "Cannot specify geometryid option with non grid content data");
    else if (!reqParams.producer.empty())
    {
      if ((!model.empty()) && (model != reqParams.producer))
        throw Fmi::Exception(BCP, "Cannot specify model and producer simultaneously");
    }
    else
      reqParams.producer = (model.empty() ? config.defaultProducerName() : model);

    const Producer &producer = (reqParams.dataSource == QueryData)
                                   ? config.getProducer(reqParams.producer, qEngine)
                                   : dummyProducer;

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
    if ((! reqParams.projection.empty()) && (! reqParams.geometryId.empty()))
      throw Fmi::Exception(BCP, "Cannot specify projection and geometryid simultaneously");

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

    if (
        (! reqParams.geometryId.empty()) &&
        (!
         (
          reqParams.bbox.empty() &&
          reqParams.gridCenter.empty() &&
          reqParams.gridSize.empty() &&
          reqParams.gridResolution.empty()
         )
        )
       )
      throw Fmi::Exception(BCP, "Cannot specify grid bounding or resolution with geometryid");

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
      if (reqParams.dataSource != QueryData)
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

    // Packing bitspervalue for grib

    auto bitsPerValue = getRequestParam(req, producer, "bitspervalue", "");

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

    // Tables version for grib2

    reqParams.grib2TablesVersion =
        ((reqParams.outputFormat == Grib2)
             ? getRequestUInt(req, producer, "tablesversion", config.getGrib2TablesVersionDefault())
             : 0);

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

    // Number of grid data parameters for given time instant or timesteps for given parameter
    // fetched as a block, and minimum chunk length returned

    reqParams.gridParamBlockSize = getRequestUInt(req, producer, "gridparamblocksize", 0);
    reqParams.gridTimeBlockSize = getRequestUInt(req, producer, "gridtimeblocksize", 0);
    reqParams.chunkSize = getRequestUInt(req, producer, "chunksize", 0);

    if ((reqParams.gridParamBlockSize > 0) || (reqParams.gridTimeBlockSize > 0))
    {
      if (reqParams.dataSource != GridContent)
        throw Fmi::Exception(
            BCP, "Cannot specify gridparamblocksize or gridtimeblocksize unless source=grid");

      if ((reqParams.gridParamBlockSize > 0) && (reqParams.gridTimeBlockSize > 0))
        throw Fmi::Exception(
            BCP, "Cannot specify gridparamblocksize and gridtimeblocksize simultaneously");

      if ((reqParams.outputFormat == NetCdf) &&
          ((reqParams.gridParamBlockSize > 0) || (reqParams.gridTimeBlockSize > 1)))
        throw Fmi::Exception(
            BCP, "Cannot specify gridparamblocksize or gridtimeblocksize with netcdf output");
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
 * \brief Initialize handler
 */
// ----------------------------------------------------------------------

void DownloadHandler::init(Config &config,
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
 * \brief Handle a /download request
 */
// ----------------------------------------------------------------------

void DownloadHandler::requestHandler(Spine::Reactor & /* theReactor */,
                                     const Spine::HTTP::Request &theRequest,
                                     Spine::HTTP::Response &theResponse)
{
  try
  {
    bool isdebug = false;

    try
    {
      const int expires_seconds = 60;
      Fmi::DateTime t_now = Fmi::SecondClock::universal_time();

      // Parse download-specific request parameters

      ReqParams reqParams;
      const auto &producer =
          getRequestParams(theRequest, reqParams, *itsConfig, *itsQEngine, itsGridEngine);

      auto query = Query(theRequest, itsGridEngine, reqParams.originTime, reqParams.test);

      // Determine start/end times from parsed request parameters

      auto now = getRequestParam(theRequest, producer, "now", "");

      Fmi::DateTime startTime, endTime;

      if ((!reqParams.startTime.empty()) || (!now.empty()))
        startTime = query.tOptions.startTime;

      if (!reqParams.endTime.empty())
        endTime = query.tOptions.endTime;

      // Create and initialize the streamer

      string filename;
      theResponse.setContent(createStreamer(theRequest,
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

      string mime = "application/octet-stream";
      theResponse.setHeader("Content-type", mime.c_str());
      theResponse.setHeader("Content-Disposition",
                            (string("attachement; filename=") + filename).c_str());

      // Defining the response header information

      Fmi::DateTime t_expires = t_now + Fmi::Seconds(expires_seconds);
      std::shared_ptr<Fmi::TimeFormatter> tformat(Fmi::TimeFormatter::create("http"));
      std::string cachecontrol =
          "public, max-age=" + boost::lexical_cast<std::string>(expires_seconds);
      std::string expiration = tformat->format(t_expires);
      std::string modification = tformat->format(t_now);

      theResponse.setHeader("Cache-Control", cachecontrol.c_str());
      theResponse.setHeader("Expires", expiration.c_str());
      theResponse.setHeader("Last-Modified", modification.c_str());
    }
    catch (...)
    {
      // Catching all exceptions

      Fmi::Exception exception(BCP, "Request processing exception!", nullptr);
      exception.addParameter("URI", theRequest.getURI());
      exception.addParameter("ClientIP", theRequest.getClientIP());
      exception.addParameter("HostName",
                             Spine::HostInfo::getHostName(theRequest.getClientIP()));

      const bool check_token = false;
      auto apikey = Spine::FmiApiKey::getFmiApiKey(theRequest, check_token);
      exception.addParameter("Apikey", (apikey ? *apikey : std::string("-")));

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

}  // namespace Download
}  // namespace Plugin
}  // namespace SmartMet

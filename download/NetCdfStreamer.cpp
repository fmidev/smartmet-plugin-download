// ======================================================================
/*!
 * \brief SmartMet download service plugin; netcdf streaming
 */
// ======================================================================

#include "NetCdfStreamer.h"
#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/foreach.hpp>
#include <boost/format.hpp>
#include <gis/ProjInfo.h>
#include <macgyver/StringConversion.h>
#include <newbase/NFmiMetTime.h>
#include <newbase/NFmiQueryData.h>
#include <macgyver/Exception.h>
#include <spine/Thread.h>

#ifndef WGS84
#include <newbase/NFmiStereographicArea.h>
#endif

namespace
{
// NcFile::Open does not seem to be thread safe
SmartMet::Spine::MutexType myFileOpenMutex;
}  // namespace

using namespace std;

using namespace boost::gregorian;
using namespace boost::posix_time;

namespace SmartMet
{
namespace Plugin
{
namespace Download
{
NetCdfStreamer::NetCdfStreamer(const Spine::HTTP::Request &req,
                               const Config &config,
                               const Producer &producer,
                               const ReqParams &reqParams)
    : DataStreamer(req, config, producer, reqParams),
      itsError(NcError::verbose_nonfatal),
      itsFilename(config.getTempDirectory() + "/dls_" + boost::lexical_cast<string>((int)getpid()) +
                  "_" + boost::lexical_cast<string>(boost::this_thread::get_id())),
      itsLoadedFlag(false)
{
}

NetCdfStreamer::~NetCdfStreamer()
{
  if (itsStream.is_open())
    itsStream.close();

  unlink(itsFilename.c_str());
}

void NetCdfStreamer::requireNcFile()
{
  // Require a started NetCDF file
  if (itsFile)
    return;

  itsFile.reset(
      new NcFile(itsFilename.c_str(), NcFile::Replace, nullptr, 0, NcFile::Offset64Bits));
}

// ----------------------------------------------------------------------
/*!
 * \brief Get next chunk of data. Called from SmartMet server code
 *
 */
// ----------------------------------------------------------------------

std::string NetCdfStreamer::getChunk()
{
  try
  {
    try
    {
      string chunk;

      if (!itsDoneFlag)
      {
        if (!itsLoadedFlag)
        {
          // The data is first loaded into a netcdf file (memory mapped filesystem assumed).
          //
          // Note: the data is loaded from 'itsGridValues'; 'chunk' serves only as 'end of data'
          // indicator.
          //
          do
          {
            extractData(chunk);

            if (chunk.empty())
              itsLoadedFlag = true;
            else
              storeParamValues();
          } while (!itsLoadedFlag);

          // Then outputting the file/data in chunks

          itsFile->close();

          itsStream.open(itsFilename, ifstream::in | ifstream::binary);

          if (!itsStream)
            throw Fmi::Exception(BCP, "Unable to open file stream");
        }

        if (!itsStream.eof())
        {
          std::unique_ptr<char[]> mesg(new char[itsChunkLength]);

          itsStream.read(mesg.get(), itsChunkLength);
          streamsize mesg_len = itsStream.gcount();

          if (mesg_len > 0)
            chunk = string(mesg.get(), mesg_len);
        }

        if (chunk.empty())
          itsDoneFlag = true;
      }

      if (itsDoneFlag)
        setStatus(ContentStreamer::StreamerStatus::EXIT_OK);

      return chunk;
    }
    catch (...)
    {
      Fmi::Exception exception(BCP, "Request processing exception!", nullptr);
      exception.addParameter("URI", itsRequest.getURI());

      std::cerr << exception.getStackTrace();
    }

    setStatus(ContentStreamer::StreamerStatus::EXIT_ERROR);

    itsDoneFlag = true;
    return "";
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

void dimDeleter(NcDim * /* dim */) {}

// ----------------------------------------------------------------------
/*!
 * \brief Add dimension.
 *
 *    Note: Dimensions are owned by the netcdf file object
 */
// ----------------------------------------------------------------------

boost::shared_ptr<NcDim> NetCdfStreamer::addDimension(const string &dimName, long dimSize)
{
  try
  {
    auto dim = boost::shared_ptr<NcDim>(itsFile->add_dim(dimName.c_str(), dimSize), dimDeleter);

    if (dim)
      return dim;

    throw Fmi::Exception(BCP, "Failed to add dimension ('" + dimName + "')");
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

void varDeleter(NcVar * /* var */) {}

// ----------------------------------------------------------------------
/*!
 * \brief Add variable
 *
 *    Note: Variables are owned by the netcdf file object
 */
// ----------------------------------------------------------------------

boost::shared_ptr<NcVar> NetCdfStreamer::addVariable(const string &varName,
                                                     NcType dataType,
                                                     NcDim *dim1,
                                                     NcDim *dim2,
                                                     NcDim *dim3,
                                                     NcDim *dim4,
                                                     NcDim *dim5)
{
  try
  {
    auto var = boost::shared_ptr<NcVar>(
        itsFile->add_var(varName.c_str(), dataType, dim1, dim2, dim3, dim4, dim5), varDeleter);

    if (var)
      return var;

    throw Fmi::Exception(BCP, "Failed to add variable ('" + varName + "')");
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Add coordinate variable
 *
 */
// ----------------------------------------------------------------------

boost::shared_ptr<NcVar> NetCdfStreamer::addCoordVariable(const string &dimName,
                                                          long dimSize,
                                                          NcType dataType,
                                                          string stdName,
                                                          string unit,
                                                          string axisType,
                                                          boost::shared_ptr<NcDim> &dim)
{
  try
  {
    dim = addDimension(dimName, dimSize);

    auto var = addVariable(dimName, dataType, &(*dim));
    addAttribute(&(*var), "standard_name", stdName.c_str());
    addAttribute(&(*var), "units", unit.c_str());

    if (!axisType.empty())
      addAttribute(&(*var), "axis", axisType.c_str());

    return var;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Add attribute
 *
 */
// ----------------------------------------------------------------------

template <typename T1, typename T2>
void NetCdfStreamer::addAttribute(T1 resource, string attrName, T2 attrValue)
{
  try
  {
    if (!((resource)->add_att(attrName.c_str(), attrValue)))
      throw Fmi::Exception(BCP, "Failed to add attribute ('" + attrName + "')");
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

template <typename T1, typename T2>
void NetCdfStreamer::addAttribute(T1 resource, string attrName, int nValues, T2 *attrValues)
{
  try
  {
    if (!((resource)->add_att(attrName.c_str(), nValues, attrValues)))
      throw Fmi::Exception(BCP, "Failed to add attribute ('" + attrName + "')");
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Get time offset as number of timesteps of given length
 *
 */
// ----------------------------------------------------------------------

int getTimeOffset(const ptime &t1, const ptime t2, long timeStep)
{
  try
  {
    if (timeStep < DataStreamer::minutesInDay)
    {
      time_duration td(t1 - t2);

      if ((timeStep < 60) || (timeStep % 60))
        return (td.hours() * 60) + td.minutes();

      return ((td.hours() * 60) + td.minutes()) / 60;
    }
    else if (timeStep == DataStreamer::minutesInDay)
    {
      date_duration dd(t1.date() - t2.date());
      return dd.days();
    }
    else if (timeStep == DataStreamer::minutesInMonth)
    {
      date d1(t1.date()), d2(t2.date());
      return (12 * (d1.year() - d2.year())) + (d1.month() - d2.month());
    }
    else if (timeStep == DataStreamer::minutesInYear)
    {
      return t1.date().year() - t2.date().year();
    }

    throw Fmi::Exception(BCP, "Invalid time step length " + boost::lexical_cast<string>(timeStep));
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Add time dimension
 *
 */
// ----------------------------------------------------------------------

void NetCdfStreamer::addTimeDimension()
{
  try
  {
    // Determine time unit.
    //
    // Supported units are minutes, hours, days, months and years ('common_years').
    //
    // Note: NetCDF Climate and Forecast (CF) Metadata Conventions (Version 1.6, 5 December, 2011):
    //
    // 		 "We recommend that the unit year be used with caution. The Udunits package defines
    // a
    // year to be exactly
    //		  365.242198781 days (the interval between 2 successive passages of the sun through
    // vernal equinox).
    //		  It is not a calendar year. Udunits includes the following definitions for years: a
    // common_year is 365 days,
    // 		  a leap_year is 366 days, a Julian_year is 365.25 days, and a Gregorian_year is
    // 365.2425
    // days.
    //		  For similar reasons the unit month, which is defined in udunits.dat to be exactly
    // year/12, should also be used with caution."

    string timeUnit;
    long timeStep = ((itsReqParams.timeStep > 0) ? itsReqParams.timeStep : itsDataTimeStep);

    if ((timeStep == 60) || ((timeStep > 0) && (timeStep < minutesInDay) && ((timeStep % 60) == 0)))
      timeUnit = "hours";
    else if (timeStep == minutesInDay)
      timeUnit = "days";
    else if (timeStep == minutesInMonth)
      timeUnit = "months";
    else if (timeStep == minutesInYear)
      timeUnit = "common_years";
    else if ((timeStep > 0) && (timeStep < minutesInDay))
    {
      timeUnit = "minutes";
      timeStep = 1;
    }
    else
      throw Fmi::Exception(BCP,
                             "Invalid data timestep " + boost::lexical_cast<string>(timeStep) +
                                 " for producer '" + itsReqParams.producer + "'");

    Spine::TimeSeriesGenerator::LocalTimeList::const_iterator timeIter = itsDataTimes.begin();
    ptime startTime = itsDataTimes.front().utc_time();
    size_t timeSize = 0;
    int times[itsDataTimes.size()];

    for (; (timeIter != itsDataTimes.end()); timeIter++, timeSize++)
    {
      long period = getTimeOffset(timeIter->utc_time(), startTime, timeStep);

      if ((timeSize > 0) && (times[timeSize - 1] >= period))
        throw Fmi::Exception(BCP,
                               "Invalid time offset " + boost::lexical_cast<string>(period) + "/" +
                                   boost::lexical_cast<string>(times[timeSize - 1]) +
                                   " (validtime " + Fmi::to_iso_string(timeIter->utc_time()) +
                                   " timestep " + boost::lexical_cast<string>(timeStep) +
                                   ") for producer '" + itsReqParams.producer + "'");

      times[timeSize] = period;
    }

    date d(startTime.date());
    greg_month gm(d.month());
    time_duration td(startTime.time_of_day());

    ostringstream os;

    os << d.year() << "-" << boost::format("%02d-%02d") % gm.as_number() % d.day()
       << boost::format(" %02d:%02d:%02d") % td.hours() % td.minutes() % td.seconds();

    string timeUnitDef = timeUnit + " since " + os.str();

    itsTimeDim = addDimension("time", timeSize);

    itsTimeVar = addVariable("time", ncInt, &(*itsTimeDim));
    addAttribute(itsTimeVar, "long_name", "time");
    addAttribute(itsTimeVar, "calendar", "gregorian");
    addAttribute(itsTimeVar, "units", timeUnitDef.c_str());

    if (!itsTimeVar->put(times, timeSize))
      throw Fmi::Exception(BCP, "Failed to store validtimes");
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Get time period name
 *
 */
// ----------------------------------------------------------------------

string getPeriodName(long periodLengthInMinutes)
{
  try
  {
    if (periodLengthInMinutes < 60)
      return boost::lexical_cast<string>(periodLengthInMinutes) + "min";
    else if (periodLengthInMinutes == 60)
      return "h";
    else if ((periodLengthInMinutes < DataStreamer::minutesInDay) &&
             ((DataStreamer::minutesInDay % periodLengthInMinutes) == 0))
      return boost::lexical_cast<string>(periodLengthInMinutes / 60) + "h";
    else if (periodLengthInMinutes == DataStreamer::minutesInDay)
      return "d";
    else if (periodLengthInMinutes == DataStreamer::minutesInMonth)
      return "mon";
    else if (periodLengthInMinutes == DataStreamer::minutesInYear)
      return "y";

    return boost::lexical_cast<string>(periodLengthInMinutes);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Add ensemble dimension
 *
 */
// ----------------------------------------------------------------------

void NetCdfStreamer::addEnsembleDimension()
{
  try
  {
    // Create dimension only if ensembe is applicable

    if (itsGridMetaData.gridEnsemble < 0)
      return;

    auto ensembleVar =
        addCoordVariable("ensemble", 1, ncShort, "ensemble", "", "Ensemble", itsEnsembleDim);
    //      addCoordVariable("ensemble", 1, ncShort, "realization", "", "E", itsEnsembleDim);

    addAttribute(ensembleVar, "long_name", "Ensemble");

    if (!ensembleVar->put(&itsGridMetaData.gridEnsemble, 1))
      throw Fmi::Exception(BCP, "Failed to store ensemble");
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Add time period length specific time dimension
 *		by copying the 'time' dimension
 */
// ----------------------------------------------------------------------

boost::shared_ptr<NcDim> NetCdfStreamer::addTimeDimension(long periodLengthInMinutes,
                                                          boost::shared_ptr<NcVar> &tVar)
{
  try
  {
    string name("time_" + getPeriodName(periodLengthInMinutes));

    auto tDim = addDimension(name, itsTimeDim->size());
    tVar = addVariable(name, ncInt, &(*tDim));

    int times[itsTimeDim->size()];
    itsTimeVar->get(times, itsTimeDim->size());

    if (!tVar->put(times, itsTimeDim->size()))
      throw Fmi::Exception(BCP, "Failed to store validtimes");

    addAttribute(tVar, "long_name", "time");
    addAttribute(tVar, "calendar", "gregorian");

    boost::shared_ptr<NcAtt> uAtt(itsTimeVar->get_att("units"));
    if (!uAtt)
      throw Fmi::Exception(BCP, "Failed to get time unit attribute");

    boost::shared_ptr<NcValues> uVal(uAtt->values());
    char *u;
    int uLen;

    if ((!uVal) || (!(u = (char *)uVal->base())) ||
        ((uLen = (uVal->num() * uVal->bytes_for_one())) < 1))
      throw Fmi::Exception(BCP, "Failed to get time unit attribute value");

    string unit(u, uLen);
    addAttribute(tVar, "units", unit.c_str());

    return tDim;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Add level dimension
 *
 */
// ----------------------------------------------------------------------

void NetCdfStreamer::addLevelDimension()
{
  try
  {
    if (isSurfaceLevel(itsLevelType))
      return;

    string name, positive;

    if (isPressureLevel(itsLevelType))
    {
      name = "pressure";
      positive = "down";
    }
    else if (isHybridLevel(itsLevelType))
    {
      name = "hybrid";
      positive = "up";
    }
    else if (isHeightLevel(itsLevelType, 0))
    {
      name = "height";
      positive = "up";
    }
    else
    {
      name = "depth";

      if (itsLevelType != itsNativeLevelType)
        // kFmiHeight with negative levels
        //
        positive = "up";
      else
        positive = (itsPositiveLevels ? "down" : "up");
    }

    auto levelVar =
        addCoordVariable(name, itsDataLevels.size(), ncFloat, "level", "", "Z", itsLevelDim);

    addAttribute(levelVar, "long_name", (string(levelVar->name()) + " level").c_str());
    addAttribute(levelVar, "positive", positive.c_str());

    float levels[itsDataLevels.size()];
    int i = 0;

    for (auto level = itsDataLevels.begin(); (level != itsDataLevels.end()); level++, i++)
    {
      levels[i] = (float)*level;
    }

    if (!levelVar->put(levels, itsDataLevels.size()))
      throw Fmi::Exception(BCP, "Failed to store levels");
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Set latlon projection metadata
 *
 */
// ----------------------------------------------------------------------

void NetCdfStreamer::setLatLonGeometry(const NFmiArea * /* area */,
                                       const boost::shared_ptr<NcVar> &crsVar)
{
  try
  {
    addAttribute(crsVar, "grid_mapping_name", "latitude_longitude");

    //	OGRSpatialReference * geometrySRS = itsResources.getGeometrySRS();
    //
    //	if (geometrySRS) {
    //	}
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Set rotated latlon projection metadata
 *
 */
// ----------------------------------------------------------------------

void NetCdfStreamer::setRotatedLatlonGeometry(const boost::shared_ptr<NcVar> &crsVar)
{
  try
  {
    // Note: grid north pole longitude (0 +) 180 works for longitude 0 atleast

    addAttribute(crsVar, "grid_mapping_name", "rotated_latitude_longitude");
    addAttribute(crsVar, "grid_north_pole_latitude", 0 - itsGridMetaData.southernPoleLat);
    addAttribute(crsVar, "grid_north_pole_longitude", itsGridMetaData.southernPoleLon + 180);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Set stereographic projection metadata
 *
 */
// ----------------------------------------------------------------------

void NetCdfStreamer::setStereographicGeometry(const NFmiArea *area,
                                              const boost::shared_ptr<NcVar> &crsVar)
{
  try
  {
    OGRSpatialReference *geometrySRS = itsResources.getGeometrySRS();
    double lon_0, lat_0, lat_ts;

    if (!geometrySRS)
    {
#ifdef WGS84
      auto opt_lon_0 = area->ProjInfo().getDouble("lon_0");
      auto opt_lat_0 = area->ProjInfo().getDouble("lat_0");
      auto opt_lat_ts = area->ProjInfo().getDouble("lat_ts");
      lon_0 = (opt_lon_0 ? *opt_lon_0 : 0);
      lat_0 = (opt_lat_0 ? *opt_lat_0 : 90);
      lat_ts = (opt_lat_ts ? *opt_lat_ts : 90);
#else
      const NFmiStereographicArea &a = *(dynamic_cast<const NFmiStereographicArea *>(area));

      lon_0 = a.CentralLongitude();
      lat_0 = a.CentralLatitude();
      lat_ts = a.TrueLatitude();
#endif
    }
    else
    {
      lon_0 = getProjParam(*geometrySRS, SRS_PP_CENTRAL_MERIDIAN);
      lat_ts = getProjParam(*geometrySRS, SRS_PP_LATITUDE_OF_ORIGIN);
      lat_0 = (lat_ts > 0) ? 90 : -90;
    }

    addAttribute(crsVar, "grid_mapping_name", "polar_stereographic");
    addAttribute(crsVar, "straight_vertical_longitude_from_pole", lon_0);
    addAttribute(crsVar, "latitude_of_projection_origin", lat_0);
    addAttribute(crsVar, "standard_parallel", lat_ts);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Set mercator projection metadata
 *
 */
// ----------------------------------------------------------------------

void NetCdfStreamer::setMercatorGeometry(const boost::shared_ptr<NcVar> &crsVar)
{
  try
  {
    OGRSpatialReference *geometrySRS = itsResources.getGeometrySRS();

    if (!geometrySRS)
      throw Fmi::Exception(BCP, "SRS is not set");

    double lon_0 = getProjParam(*geometrySRS, SRS_PP_CENTRAL_MERIDIAN);

    addAttribute(crsVar, "grid_mapping_name", "mercator");
    addAttribute(crsVar, "longitude_of_projection_origin", lon_0);

    if (geometrySRS->FindProjParm(SRS_PP_STANDARD_PARALLEL_1) >= 0)
    {
      double lat_ts = getProjParam(*geometrySRS, SRS_PP_STANDARD_PARALLEL_1);
      addAttribute(crsVar, "standard_parallel", lat_ts);
    }
    else
    {
      double scale_factor = getProjParam(*geometrySRS, SRS_PP_SCALE_FACTOR);
      addAttribute(crsVar, "scale_factor_at_projection_origin", scale_factor);
    }
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Set lcc projection metadata
 *
 */
// ----------------------------------------------------------------------

void NetCdfStreamer::setLambertConformalGeometry(const boost::shared_ptr<NcVar> &crsVar)
{
  try
  {
    OGRSpatialReference *geometrySRS = itsResources.getGeometrySRS();

    if (!geometrySRS)
      throw Fmi::Exception(BCP, "SRS is not set");

    double lon_0 = getProjParam(*geometrySRS, SRS_PP_CENTRAL_MERIDIAN);
    double lat_0 = getProjParam(*geometrySRS, SRS_PP_LATITUDE_OF_ORIGIN);
    double latin1 = getProjParam(*geometrySRS, SRS_PP_STANDARD_PARALLEL_1);

    addAttribute(crsVar, "grid_mapping_name", "lambert_conformal_conic");
    addAttribute(crsVar, "longitude_of_central_meridian", lon_0);
    addAttribute(crsVar, "latitude_of_projection_origin", lat_0);

    if (EQUAL(itsGridMetaData.projection.c_str(), SRS_PT_LAMBERT_CONFORMAL_CONIC_2SP))
    {
      // http://cfconventions.org/Data/cf-conventions/cf-conventions-1.8/cf-conventions.html
      // #table-grid-mapping-attributes
      //
      // .. with the additional convention that the standard parallel nearest the pole
      // (N or S) is provided first

      double latin2 = getProjParam(*geometrySRS, SRS_PP_STANDARD_PARALLEL_2);
      double sp1 = latin1, sp2 = latin2;

      if (((latin1 >= 0) && (latin2 >= 0) && (latin1 < latin2)) ||
          ((latin1 <= 0) && (latin2 <= 0) && (latin1 > latin2)))
      {
        sp1 = latin2;
        sp2 = latin1;
      }

      double sp[] = {sp1, sp2};

      addAttribute(crsVar, "standard_parallel", 2, sp);
    }
    else
      addAttribute(crsVar, "standard_parallel", latin1);

    addAttribute(crsVar, "grid_mapping_name", "lambert_conformal_conic");
    addAttribute(crsVar, "longitude_of_central_meridian", lon_0);
    addAttribute(crsVar, "latitude_of_projection_origin", lat_0);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Set metadata
 *
 */
// ----------------------------------------------------------------------

void NetCdfStreamer::setGeometry(Engine::Querydata::Q q, const NFmiArea *area, const NFmiGrid *grid)
{
  try
  {
    // Conventions

    addAttribute(itsFile.get(), "Conventions", "CF-1.6");
    addAttribute(itsFile.get(), "title", "<title>");
    addAttribute(itsFile.get(), "institution", "fmi.fi");
    addAttribute(itsFile.get(), "source", "<producer>");

    // Time dimension

    addTimeDimension();

    // Level dimension

    addLevelDimension();

    // Set projection

    auto crsVar = addVariable("crs", ncShort);

    int classId = (itsReqParams.areaClassId != A_Native)
        ? (int) itsReqParams.areaClassId
        : (area->ClassId() == kNFmiProjArea) ? area->DetectClassId() : area->ClassId();

    switch (classId)
    {
      case kNFmiLatLonArea:
        setLatLonGeometry(area, crsVar);
        break;
      case kNFmiStereographicArea:
        setStereographicGeometry(area, crsVar);
        break;
      default:
        throw Fmi::Exception(BCP, "Unsupported projection in input data");
    }

    // Store y/x and/or lat/lon dimensions and coordinate variables, cropping the grid if manual
    // cropping is set

    bool projected = (classId != kNFmiLatLonArea);

    size_t x0 = (itsCropping.cropped ? itsCropping.bottomLeftX : 0),
           y0 = (itsCropping.cropped ? itsCropping.bottomLeftY : 0);
    size_t xN = (itsCropping.cropped ? (x0 + itsCropping.gridSizeX) : itsReqGridSizeX),
           yN = (itsCropping.cropped ? (y0 + itsCropping.gridSizeY) : itsReqGridSizeY);
    size_t xStep = (itsReqParams.gridStepXY ? (*(itsReqParams.gridStepXY))[0].first : 1),
           yStep = (itsReqParams.gridStepXY ? (*(itsReqParams.gridStepXY))[0].second : 1), x, y, n;
    size_t nLat = (projected ? (itsNY * itsNX) : itsNY),
           nLon = (projected ? (itsNY * itsNX) : itsNX);
    std::unique_ptr<double[]> latPtr(new double[nLat]), lonPtr(new double[nLon]);
    double *lat = latPtr.get(), *lon = lonPtr.get();

    boost::shared_ptr<NcVar> latVar, lonVar;

    if (!grid)
      grid = &q->grid();

    if (projected)
    {
      // Store y, x and 2d (y,x) lat/lon coordinates.
      //
      // Note: NetCDF Climate and Forecast (CF) Metadata Conventions (Version 1.6, 5 December,
      // 2011):
      //
      // "T(k,j,i) is associated with the coordinate values lon(j,i), lat(j,i), and
      // lev(k). The vertical coordinate is represented by the coordinate variable lev(lev) and
      // the latitude and longitude coordinates are represented by the auxiliary coordinate
      // variables lat(yc,xc) and lon(yc,xc) which are identified by the coordinates attribute.
      //
      // Note that coordinate variables are also defined for the xc and yc dimensions. This
      // faciliates processing of this data by generic applications that don't recognize the
      // multidimensional latitude and longitude coordinates."

      auto yVar =
          addCoordVariable("y", itsNY, ncFloat, "projection_y_coordinate", "m", "Y", itsYDim);
      auto xVar =
          addCoordVariable("x", itsNX, ncFloat, "projection_x_coordinate", "m", "X", itsXDim);

      NFmiPoint p0 =
          ((itsReqParams.datumShift == Datum::DatumShift::None) ? grid->GridToWorldXY(x0, y0)
                                                                : itsTargetWorldXYs(x0, y0));
      NFmiPoint pN = ((itsReqParams.datumShift == Datum::DatumShift::None)
                          ? grid->GridToWorldXY(xN - 1, yN - 1)
                          : itsTargetWorldXYs(xN - 1, yN - 1));

      double worldY[itsNY], worldX[itsNX];
      double wY = p0.Y(), wX = p0.X();
      double stepY = yStep * ((itsNY > 1) ? ((pN.Y() - p0.Y()) / (yN - y0 - 1)) : 0.0);
      double stepX = xStep * ((itsNX > 1) ? ((pN.X() - p0.X()) / (xN - x0 - 1)) : 0.0);

      for (y = 0; (y < itsNY); wY += stepY, y++)
        worldY[y] = wY;
      for (x = 0; (x < itsNX); wX += stepX, x++)
        worldX[x] = wX;

      if (!yVar->put(worldY, itsNY))
        throw Fmi::Exception(BCP, "Failed to store y -coordinates");

      if (!xVar->put(worldX, itsNX))
        throw Fmi::Exception(BCP, "Failed to store x -coordinates");

      latVar = addVariable("lat", ncFloat, &(*itsYDim), &(*itsXDim));
      lonVar = addVariable("lon", ncFloat, &(*itsYDim), &(*itsXDim));

      for (y = y0, n = 0; (y < yN); y += yStep)
        for (x = x0; (x < xN); x += xStep, n++)
        {
          const NFmiPoint p =
              ((itsReqParams.datumShift == Datum::DatumShift::None) ? grid->GridToLatLon(x, y)
                                                                    : itsTargetLatLons(x, y));

          lat[n] = p.Y();
          lon[n] = p.X();
        }

      if (!latVar->put(lat, itsNY, itsNX))
        throw Fmi::Exception(BCP, "Failed to store latitude(y,x) coordinates");
      if (!lonVar->put(lon, itsNY, itsNX))
        throw Fmi::Exception(BCP, "Failed to store longitude(y,x) coordinates");
    }
    else
    {
      // latlon, grid defined as cartesian product of latitude and longitude axes
      //
      latVar = addCoordVariable("lat", itsNY, ncFloat, "latitude", "degrees_north", "Y", itsLatDim);
      lonVar = addCoordVariable("lon", itsNX, ncFloat, "longitude", "degrees_east", "X", itsLonDim);

      for (y = y0, n = 0; (y < yN); y += yStep, n++)
        lat[n] =
            ((itsReqParams.datumShift == Datum::DatumShift::None) ? grid->GridToLatLon(0, y).Y()
                                                                  : itsTargetLatLons.y(0, y));

      for (x = x0, n = 0; (x < xN); x += xStep, n++)
        lon[n] =
            ((itsReqParams.datumShift == Datum::DatumShift::None) ? grid->GridToLatLon(x, 0).X()
                                                                  : itsTargetLatLons.x(x, 0));

      if (!latVar->put(lat, itsNY))
        throw Fmi::Exception(BCP, "Failed to store latitude coordinates");

      if (!lonVar->put(lon, itsNX))
        throw Fmi::Exception(BCP, "Failed to store longitude coordinates");
    }

    addAttribute(latVar, "standard_name", "latitude");
    addAttribute(latVar, "long_name", "latitude");
    addAttribute(latVar, "units", "degrees_north");
    addAttribute(lonVar, "standard_name", "longitude");
    addAttribute(lonVar, "long_name", "longitude");
    addAttribute(lonVar, "units", "degrees_east");

    if (Datum::isDatumShiftToWGS84(itsReqParams.datumShift))
    {
      addAttribute(crsVar, "semi_major", Datum::NetCdf::WGS84_semiMajor);
      addAttribute(crsVar, "inverse_flattening", Datum::NetCdf::WGS84_invFlattening);
    }
    else if (projected)
      addAttribute(crsVar, "earth_radius", Datum::NetCdf::Fmi_6371220m);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

void NetCdfStreamer::setGridGeometry(const QueryServer::Query &gridQuery)
{
  try
  {
    // Conventions

    addAttribute(itsFile.get(), "Conventions", "CF-1.6");
    addAttribute(itsFile.get(), "title", "<title>");
    addAttribute(itsFile.get(), "institution", "fmi.fi");
    addAttribute(itsFile.get(), "source", "<producer>");

    // Ensemble dimension

    addEnsembleDimension();

    // Time dimension

    addTimeDimension();

    // Level dimension

    addLevelDimension();

    // Set projection

    auto crsVar = addVariable("crs", ncShort);

    addAttribute(crsVar, "crs_wkt", itsGridMetaData.crs.c_str());

    switch (itsGridMetaData.projType)
    {
      case T::GridProjectionValue::LatLon:
        setLatLonGeometry(nullptr, crsVar);
        break;
      case T::GridProjectionValue::RotatedLatLon:
        setRotatedLatlonGeometry(crsVar);
        break;
      case T::GridProjectionValue::PolarStereographic:
        setStereographicGeometry(nullptr, crsVar);
        break;
      case T::GridProjectionValue::Mercator:
        setMercatorGeometry(crsVar);
        break;
      case T::GridProjectionValue::LambertConformal:
        setLambertConformalGeometry(crsVar);
        break;
      default:
        throw Fmi::Exception(BCP, "Unsupported projection in input data");
    }

    // Store y/x and/or lat/lon dimensions and coordinate variables

    bool projected = ((itsGridMetaData.projType != T::GridProjectionValue::LatLon) &&
                      (itsGridMetaData.projType != T::GridProjectionValue::RotatedLatLon));

    size_t x0 = 0, y0 = 0;
    size_t xN = itsReqGridSizeX, yN = itsReqGridSizeY;
    size_t xStep = (itsReqParams.gridStepXY ? (*(itsReqParams.gridStepXY))[0].first : 1),
           yStep = (itsReqParams.gridStepXY ? (*(itsReqParams.gridStepXY))[0].second : 1), x, y, n;
    size_t nLat = (projected ? (itsNY * itsNX) : itsNY),
           nLon = (projected ? (itsNY * itsNX) : itsNX);
    std::unique_ptr<double[]> latPtr(new double[nLat]), lonPtr(new double[nLon]);
    double *lat = latPtr.get(), *lon = lonPtr.get();

    boost::shared_ptr<NcVar> latVar, lonVar;

    auto coords = gridQuery.mQueryParameterList.front().mCoordinates;

    if (coords.size() != (itsReqGridSizeX * itsReqGridSizeY))
      throw Fmi::Exception(BCP,
                           "Number of coordinates (" + Fmi::to_string(coords.size()) +
                               ") and grid size (" + Fmi::to_string(itsReqGridSizeX) + "/" +
                               Fmi::to_string(itsReqGridSizeY) + ") mismatch");

    if (projected)
    {
      // Store y, x and 2d (y,x) lat/lon coordinates.
      //
      // Note: NetCDF Climate and Forecast (CF) Metadata Conventions (Version 1.6, 5 December,
      // 2011):
      //
      //	 "T(k,j,i) is associated with the coordinate values lon(j,i), lat(j,i), and lev(k).
      //	  The vertical coordinate is represented by the coordinate variable lev(lev) and
      //	  the latitude and longitude coordinates are represented by the auxiliary coordinate
      //	  variables lat(yc,xc) and lon(yc,xc) which are identified by the coordinates
      //	  attribute.
      //
      //	  Note that coordinate variables are also defined for the xc and yc dimensions. This
      //	  faciliates processing of this data by generic applications that don't recognize
      //	  the multidimensional latitude and longitude coordinates."

      auto inputSRS = itsResources.getGeometrySRS();
      OGRSpatialReference llSRS;
      llSRS.CopyGeogCSFrom(inputSRS);

      OGRCoordinateTransformation *ct = itsResources.getCoordinateTransformation(&llSRS, inputSRS);

      double xc[] = {coords[0].x(), coords[coords.size() - 1].x()};
      double yc[] = {coords[0].y(), coords[coords.size() - 1].y()};
      int pabSuccess[2];

      int status = ct->Transform(2, xc, yc, nullptr, pabSuccess);

      if (!(status && pabSuccess[0] && pabSuccess[1]))
        throw Fmi::Exception(BCP, "Failed to transform llbbox to bbox: " + itsGridMetaData.crs);

      auto yVar =
          addCoordVariable("y", itsNY, ncFloat, "projection_y_coordinate", "m", "Y", itsYDim);
      auto xVar =
          addCoordVariable("x", itsNX, ncFloat, "projection_x_coordinate", "m", "X", itsXDim);

      NFmiPoint p0(xc[0], yc[0]);
      NFmiPoint pN(xc[1], yc[1]);

      double worldY[itsNY], worldX[itsNX];
      double wY = p0.Y(), wX = p0.X();
      double stepY = yStep * ((itsNY > 1) ? ((pN.Y() - p0.Y()) / (yN - y0 - 1)) : 0.0);
      double stepX = xStep * ((itsNX > 1) ? ((pN.X() - p0.X()) / (xN - x0 - 1)) : 0.0);

      for (y = 0; (y < itsNY); wY += stepY, y++)
        worldY[y] = wY;
      for (x = 0; (x < itsNX); wX += stepX, x++)
        worldX[x] = wX;

      if (!yVar->put(worldY, itsNY))
        throw Fmi::Exception(BCP, "Failed to store y -coordinates");

      if (!xVar->put(worldX, itsNX))
        throw Fmi::Exception(BCP, "Failed to store x -coordinates");

      latVar = addVariable("lat", ncFloat, &(*itsYDim), &(*itsXDim));
      lonVar = addVariable("lon", ncFloat, &(*itsYDim), &(*itsXDim));

      addAttribute(latVar, "standard_name", "latitude");
      addAttribute(latVar, "units", "degrees_north");
      addAttribute(lonVar, "standard_name", "longitude");
      addAttribute(lonVar, "units", "degrees_east");

      for (y = 0, n = 0; (y < yN); y += yStep)
        for (x = 0; (x < xN); x += xStep, n++)
        {
          auto c = (y * xN) + x;
          const NFmiPoint p(coords[c].x(), coords[c].y());

          lat[n] = p.Y();
          lon[n] = p.X();
        }

      if (!latVar->put(lat, itsNY, itsNX))
        throw Fmi::Exception(BCP, "Failed to store latitude(y,x) coordinates");
      if (!lonVar->put(lon, itsNY, itsNX))
        throw Fmi::Exception(BCP, "Failed to store longitude(y,x) coordinates");
    }
    else
    {
      // latlon or rotlatlon, grid defined as cartesian product of latitude and longitude axes

      auto latCoord = (itsGridMetaData.projType == T::GridProjectionValue::LatLon)
                          ? "latitude"
                          : "grid_latitude";
      auto latUnit = (itsGridMetaData.projType == T::GridProjectionValue::LatLon) ? "degrees_north"
                                                                                  : "degrees";
      auto lonCoord = (itsGridMetaData.projType == T::GridProjectionValue::LatLon)
                          ? "longitude"
                          : "grid_longitude";
      auto lonUnit =
          (itsGridMetaData.projType == T::GridProjectionValue::LatLon) ? "degrees_east" : "degrees";

      latVar = addCoordVariable("lat", itsNY, ncFloat, latCoord, latUnit, "Lat", itsLatDim);
      lonVar = addCoordVariable("lon", itsNX, ncFloat, lonCoord, lonUnit, "Lon", itsLonDim);

      if (itsGridMetaData.projType == T::GridProjectionValue::LatLon)
      {
        for (y = 0, n = 0; (y < yN); y += yStep, n++)
          lat[n] = coords[y * xN].y();

        for (x = 0, n = 0; (x < xN); x += xStep, n++)
          lon[n] = coords[x].x();
      }
      else
      {
        auto rotLat = itsGridMetaData.rotLatitudes.get();
        auto rotLon = itsGridMetaData.rotLongitudes.get();

        for (y = 0, n = 0; (y < yN); y += yStep, n++)
          lat[n] = rotLat[y * xN];

        for (x = 0, n = 0; (x < xN); x += xStep, n++)
          lon[n] = rotLon[x];
      }

      if (!latVar->put(lat, itsNY))
        throw Fmi::Exception(BCP, "Failed to store latitude coordinates");

      if (!lonVar->put(lon, itsNX))
        throw Fmi::Exception(BCP, "Failed to store longitude coordinates");
    }

    addAttribute(latVar, "long_name", "latitude");
    addAttribute(lonVar, "long_name", "longitude");

    if (itsGridMetaData.flatteningStr.size() > 0)
    {
      addAttribute(crsVar, "semi_major", itsGridMetaData.earthRadiusOrSemiMajorInMeters);
      addAttribute(crsVar, "inverse_flattening", *itsGridMetaData.flattening);
    }
    else
      addAttribute(crsVar, "earth_radius", itsGridMetaData.earthRadiusOrSemiMajorInMeters);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Get time period start time
 *
 */
// ----------------------------------------------------------------------

ptime getPeriodStartTime(const ptime &vt, long periodLengthInMinutes)
{
  try
  {
    date d = vt.date();
    time_duration td = vt.time_of_day();
    long minutes = (td.hours() * 60) + td.minutes();

    if (((periodLengthInMinutes > 0) && (periodLengthInMinutes < 60) &&
         ((60 % periodLengthInMinutes) == 0)) ||
        (periodLengthInMinutes == 60) ||
        ((periodLengthInMinutes < DataStreamer::minutesInDay) &&
         ((DataStreamer::minutesInDay % periodLengthInMinutes) == 0)))
      if (minutes == 0)
        return ptime(d, time_duration(0, -periodLengthInMinutes, 0));
      else if ((minutes % periodLengthInMinutes) != 0)
        return ptime(
            d, time_duration(0, ((minutes / periodLengthInMinutes) * periodLengthInMinutes), 0));
      else
        return ptime(d, time_duration(0, minutes - periodLengthInMinutes, 0));
    else if (periodLengthInMinutes == DataStreamer::minutesInDay)
      if (minutes == 0)
        return ptime(ptime(d, time_duration(-1, 0, 0)).date());
      else
        return ptime(d);
    else if (periodLengthInMinutes == DataStreamer::minutesInMonth)
    {
      if ((d.day() == 1) && (minutes == 0))
        d = ptime(d, time_duration(-1, 0, 0)).date();

      return ptime(date(d.year(), d.month(), 1));
    }
    else if (periodLengthInMinutes == DataStreamer::minutesInYear)
    {
      if ((d.month() == 1) && (d.day() == 1) && (minutes == 0))
        d = ptime(d, time_duration(-1, 0, 0)).date();

      return ptime(date(d.year(), 1, 1));
    }

    throw Fmi::Exception(
        BCP, "Invalid time period length " + boost::lexical_cast<string>(periodLengthInMinutes));
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Add time bounds for aggregate data.
 *
 */
// ----------------------------------------------------------------------

boost::shared_ptr<NcDim> NetCdfStreamer::addTimeBounds(long periodLengthInMinutes,
                                                       string &timeDimName)
{
  try
  {
    string pName(getPeriodName(periodLengthInMinutes));

    timeDimName = "time_" + pName;

    boost::shared_ptr<NcDim> tDim(itsFile->get_dim(timeDimName.c_str()), dimDeleter);

    if (tDim)
      return tDim;

    // Add aggregate period length specific time dimension and variable

    boost::shared_ptr<NcVar> tVar;
    tDim = addTimeDimension(periodLengthInMinutes, tVar);

    // Add time bounds dimension

    if (!itsTimeBoundsDim)
      itsTimeBoundsDim = addDimension("time_bounds", 2);

    // Determine and store time bounds

    Spine::TimeSeriesGenerator::LocalTimeList::const_iterator timeIter = itsDataTimes.begin();
    ptime startTime = itsDataTimes.front().utc_time(), vt;
    int bounds[2 * itsTimeDim->size()];
    size_t i = 0;

    for (; (timeIter != itsDataTimes.end()); timeIter++)
    {
      // Period start time offset
      //
      vt = timeIter->utc_time();

      bounds[i] =
          getTimeOffset(getPeriodStartTime(vt, periodLengthInMinutes), startTime, itsDataTimeStep);
      i++;

      // Validtime's offset

      bounds[i] = getTimeOffset(vt, startTime, itsDataTimeStep);
      i++;
    }

    string name("time_bounds_" + pName);

    auto timeBoundsVar = addVariable(name, ncInt, &(*tDim), &(*itsTimeBoundsDim));

    if (!timeBoundsVar->put(bounds, itsTimeDim->size(), 2))
      throw Fmi::Exception(BCP, "Failed to store time bounds");

    // Connect the bounds to the time variable

    addAttribute(tVar, "bounds", name.c_str());

    return tDim;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Add parameters
 *
 */
// ----------------------------------------------------------------------

void NetCdfStreamer::addParameters(bool relative_uv)
{
  try
  {
    NcDim &yOrLat = (itsYDim ? *itsYDim : *itsLatDim);
    NcDim &xOrLon = (itsYDim ? *itsXDim : *itsLonDim);

    NcDim *dimensions[] = {
        itsEnsembleDim ? &(*itsEnsembleDim) : nullptr,  // Ensemble
        nullptr,                                        // Time dimension
        itsLevelDim ? &(*itsLevelDim) : &yOrLat,        // Level or Y/lat
        itsLevelDim ? &yOrLat : &xOrLon,                // Y/lat or X/lon dimension
        itsLevelDim ? &xOrLon : nullptr                 // X dimension or n/a
    };

    boost::shared_ptr<NcDim> tDim;

    for (auto it = itsDataParams.begin(); (it != itsDataParams.end()); it++)
    {
      NFmiParam theParam(it->number());
      const ParamChangeTable &pTable = itsCfg.getParamChangeTable(false);
      string paramName, stdName, longName, unit, timeDimName = "time";
      size_t i, j;

      dimensions[1] = &(*itsTimeDim);

      signed long usedParId = theParam.GetIdent();

      for (i = j = 0; i < pTable.size(); ++i)
        if (usedParId == pTable[i].itsWantedParam.GetIdent())
        {
          if (relative_uv == (pTable[i].itsGridRelative ? *pTable[i].itsGridRelative : false))
            break;
          else if (j == 0)
            j = i + 1;
          else
            throw Fmi::Exception(BCP,
                                   "Missing gridrelative configuration for parameter " +
                                       boost::lexical_cast<string>(usedParId));
        }

      if ((i >= pTable.size()) && (j > 0))
        i = j - 1;

      if (i < pTable.size())
      {
        paramName = pTable[i].itsWantedParam.GetName();
        stdName = pTable[i].itsStdName;
        longName = pTable[i].itsLongName;
        unit = pTable[i].itsUnit;

        if ((!pTable[i].itsStepType.empty()) || (pTable[i].itsPeriodLengthMinutes > 0))
        {
          // Add aggregate period length specific time dimension and time bounds.
          // Use data period length if aggregate period length is not given.
          //
          tDim = addTimeBounds((pTable[i].itsPeriodLengthMinutes > 0)
                                   ? pTable[i].itsPeriodLengthMinutes
                                   : itsDataTimeStep,
                               timeDimName);
          dimensions[1] = &(*tDim);
        }
      }
      else
        paramName = theParam.GetName();

      NcDim **dim = dimensions;

      if (!itsEnsembleDim)
        dim++;

      NcDim *dim1 = *(dim++);
      NcDim *dim2 = *(dim++);
      NcDim *dim3 = *(dim++);
      NcDim *dim4 = *(dim++);
      NcDim *dim5 = (itsEnsembleDim ? *dim : nullptr);

      auto dataVar = addVariable(paramName + "_" + boost::lexical_cast<string>(usedParId),
                                 ncFloat,
                                 dim1,
                                 dim2,
                                 dim3,
                                 dim4,
                                 dim5);

      float missingValue =
          (itsReqParams.dataSource == QueryData) ? kFloatMissing : gribMissingValue;

      addAttribute(dataVar, "units", unit.c_str());
      addAttribute(dataVar, "_FillValue", missingValue);
      addAttribute(dataVar, "missing_value", missingValue);
      addAttribute(dataVar, "grid_mapping", "crs");

      if (!stdName.empty())
        addAttribute(dataVar, "standard_name", stdName.c_str());

      if (!longName.empty())
        addAttribute(dataVar, "long_name", longName.c_str());

      if ((i < pTable.size()) && (!pTable[i].itsStepType.empty()))
        // Cell method for aggregate data
        //
        addAttribute(dataVar, "cell_methods", (timeDimName + ": " + pTable[i].itsStepType).c_str());

      if (itsYDim)
        addAttribute(dataVar, "coordinates", "lat lon");

      itsDataVars.push_back(dataVar.get());
    }

    itsVarIterator = itsDataVars.begin();
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Store current parameter's/grid's values.
 *
 */
// ----------------------------------------------------------------------

void NetCdfStreamer::storeParamValues()
{
  try
  {
    // Load scaled values into a continuous buffer, cropping the grid/values if manual cropping is
    // set
    //
    // Note: Using heap because buffer size might exceed stack size

    bool cropxy = (itsCropping.cropped && itsCropping.cropMan);
    size_t x0 = (cropxy ? itsCropping.bottomLeftX : 0), y0 = (cropxy ? itsCropping.bottomLeftY : 0);
    size_t xN = (itsCropping.cropped ? (x0 + itsCropping.gridSizeX) : itsReqGridSizeX),
           yN = (itsCropping.cropped ? (y0 + itsCropping.gridSizeY) : itsReqGridSizeY);
    size_t xStep = (itsReqParams.gridStepXY ? (*(itsReqParams.gridStepXY))[0].first : 1),
           yStep = (itsReqParams.gridStepXY ? (*(itsReqParams.gridStepXY))[0].second : 1), x, y;

    boost::shared_ptr<float[]> values(new float[itsNY * itsNX]);

    int i = 0;

    if (itsReqParams.dataSource == QueryData)
    {
      for (y = y0; (y < yN); y += yStep)
        for (x = x0; (x < xN); x += xStep, i++)
        {
          auto value = itsGridValues[x][y];

          if (value != kFloatMissing)
            values[i] = (value + itsScalingIterator->second) / itsScalingIterator->first;
          else
            values[i] = value;
        }
    }
    else
    {
      auto dataValues = itsGridQuery.mQueryParameterList.front().mValueList.front()->mValueVector;

      for (y = y0; (y < yN); y += yStep)
        for (x = x0; (x < xN); x += xStep, i++)
        {
          auto c = (y * xN) + x;
          auto value = dataValues[c];

          if (value != ParamValueMissing)
            values[i] = (value + itsScalingIterator->second) / itsScalingIterator->first;
          else
            values[i] = gribMissingValue;
        }
    }

    // Store the values for current parameter/[level/]validtime.
    //
    // First skip variables for missing parameters if any.
    //
    // Note: with querydata timeIndex was incremented after getting the data
    //       and ensemble dimension is not used

    auto timeIndex = itsTimeIndex - ((itsReqParams.dataSource == QueryData) ? 1 : 0);

    if ((itsVarIterator == itsDataVars.begin()) && (itsParamIterator != itsDataParams.begin()))
      for (auto it_p = itsDataParams.begin(); (it_p != itsParamIterator); it_p++)
      {
        itsVarIterator++;

        if (itsVarIterator == itsDataVars.end())
          throw Fmi::Exception(BCP, "storeParamValues: internal: No more netcdf variables");
      }

    long nX = (long)itsNX, nY = (long)itsNY;
    long edgeLengths[] = {
        1,                      // Ensemble dimension, edge length 1
        1,                      // Time dimension, edge length 1
        itsLevelDim ? 1 : nY,   // Level (edge length 1) or Y dimension
        itsLevelDim ? nY : nX,  // Y or X dimension
        itsLevelDim ? nX : -1   // X dimension or n/a
    };
    long *edge = edgeLengths;
    uint nEdges = (itsLevelDim ? 5 : 4);

    if (!itsEnsembleDim)
    {
      if (!(*itsVarIterator)->set_cur(timeIndex, itsLevelDim ? itsLevelIndex : -1))
        throw Fmi::Exception(BCP, "Failed to set active netcdf time/level");

      edge++;
      nEdges--;
    }
    else if (!(*itsVarIterator)->set_cur(0, timeIndex, itsLevelDim ? itsLevelIndex : -1))
      throw Fmi::Exception(BCP, "Failed to set active netcdf ensemble/time/level");

    long edge1 = *(edge++);
    long edge2 = *(edge++);
    long edge3 = *(edge++);
    long edge4 = *(edge++);
    long edge5 = (itsEnsembleDim ? *edge : -1);

    // It seems there can be at max 1 missing (-1) edge at the end of parameter edges
    //
    // e.g.
    // n,n,n and n,n,n,-1 are both valid for surface data without ensemble
    // n,n,n,n and n,n,n,n,-1 are both valid (level data without ensemble or surface data with it)

    if (((nEdges == 3) && (!(*itsVarIterator)->put(values.get(), edge1, edge2, edge3))) ||
        ((nEdges == 4) && (!(*itsVarIterator)->put(values.get(), edge1, edge2, edge3, edge4))) ||
        ((nEdges == 5) &&
         (!(*itsVarIterator)->put(values.get(), edge1, edge2, edge3, edge4, edge5))))
      throw Fmi::Exception(BCP, "Failed to store netcdf variable values");
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Handle change of parameter
 *
 */
// ----------------------------------------------------------------------

void NetCdfStreamer::paramChanged(size_t nextParamOffset)
{
  try
  {
    // Note: Netcdf varibles are created when first nonmissing querydata parameter is encountered

    if (itsDataVars.size() > 0)
    {
      for (size_t n = 0; (n < nextParamOffset); n++)
      {
        if (itsVarIterator != itsDataVars.end())
          itsVarIterator++;

        if ((itsVarIterator == itsDataVars.end()) && (itsParamIterator != itsDataParams.end()))
          throw Fmi::Exception(BCP, "paramChanged: internal: No more netcdf variables");
      }
    }
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Load chunk of data; called by DataStreamer to get format specific chunk.
 *
 */
// ----------------------------------------------------------------------

void NetCdfStreamer::getDataChunk(Engine::Querydata::Q q,
                                  const NFmiArea *area,
                                  NFmiGrid *grid,
                                  int /* level */,
                                  const NFmiMetTime & /* mt */,
                                  NFmiDataMatrix<float> & /* values */,
                                  string &chunk)
{
  try
  {
    if (itsMetaFlag)
    {
      // NcFile metadata generation is not thread safe

      Spine::WriteLock lock(myFileOpenMutex);

      requireNcFile();

      // Set geometry and dimensions

      setGeometry(q, area, grid);

      // Add parameters

      addParameters(q->isRelativeUV());

      itsMetaFlag = false;
    }

    // Data is loaded from 'values'; set nonempty chunk to indicate data is available.

    chunk = " ";
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Load chunk of grid data; called by DataStreamer to get format specific chunk.
 *
 */
// ----------------------------------------------------------------------

void NetCdfStreamer::getGridDataChunk(const QueryServer::Query &gridQuery,
                                      int,
                                      const NFmiMetTime &,
                                      string &chunk)
{
  try
  {
    if (itsMetaFlag)
    {
      // NcFile metadata generation is not thread safe

      Spine::WriteLock lock(myFileOpenMutex);

      requireNcFile();

      // Set geometry and dimensions

      setGridGeometry(gridQuery);

      // Add parameters

      addParameters(false);

      itsMetaFlag = false;
    }

    // Data is loaded from 'values'; set nonempty chunk to indicate data is available.

    chunk = " ";
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

}  // namespace Download
}  // namespace Plugin
}  // namespace SmartMet

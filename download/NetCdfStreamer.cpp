// ======================================================================
/*!
 * \brief SmartMet download service plugin; netcdf streaming
 */
// ======================================================================

#include "NetCdfStreamer.h"

#include <macgyver/StringConversion.h>

#include <spine/Exception.h>

#include <newbase/NFmiMetTime.h>
#include <newbase/NFmiQueryData.h>
#include <newbase/NFmiStereographicArea.h>

#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/foreach.hpp>
#include <boost/format.hpp>

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
                               const Producer &producer)
    : DataStreamer(req, config, producer),
      ncError(NcError::verbose_nonfatal),
      file(config.getTempDirectory() + "/dls_" + boost::lexical_cast<string>((int)getpid()) + "_" +
           boost::lexical_cast<string>(boost::this_thread::get_id())),
      ncFile(file.c_str(), NcFile::Replace, nullptr, 0, NcFile::Netcdf4Classic),
      isLoaded(false)
{
}

NetCdfStreamer::~NetCdfStreamer()
{
  try
  {
    if (ioStream.is_open())
      ioStream.close();

    unlink(file.c_str());
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
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

      if (!isDone)
      {
        if (!isLoaded)
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
              isLoaded = true;
            else
              storeParamValues();
          } while (!isLoaded);

          // Then outputting the file/data in chunks

          ncFile.close();

          ioStream.open(file, ifstream::in | ifstream::binary);

          if (!ioStream)
            throw Spine::Exception(BCP, "Unable to open file stream");
        }

        if (!ioStream.eof())
        {
          char mesg[itsChunkLength];

          ioStream.read(mesg, sizeof(mesg));
          streamsize mesg_len = ioStream.gcount();

          if (mesg_len > 0)
            chunk = string(mesg, mesg_len);
        }

        if (chunk.empty())
          isDone = true;
      }

      if (isDone)
        setStatus(ContentStreamer::StreamerStatus::EXIT_OK);

      return chunk;
    }
    catch (...)
    {
      Spine::Exception exception(BCP, "Request processing exception!", nullptr);
      exception.addParameter("URI", itsRequest.getURI());

      std::cerr << exception.getStackTrace();
    }

    setStatus(ContentStreamer::StreamerStatus::EXIT_ERROR);

    isDone = true;
    return "";
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
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

boost::shared_ptr<NcDim> NetCdfStreamer::addDimension(string dimName, long dimSize)
{
  try
  {
    auto dim = boost::shared_ptr<NcDim>(ncFile.add_dim(dimName.c_str(), dimSize), dimDeleter);

    if (dim)
      return dim;

    throw Spine::Exception(BCP, "Failed to add dimension ('" + dimName + "')");
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
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

boost::shared_ptr<NcVar> NetCdfStreamer::addVariable(
    string varName, NcType dataType, NcDim *dim1, NcDim *dim2, NcDim *dim3, NcDim *dim4)
{
  try
  {
    auto var = boost::shared_ptr<NcVar>(
        ncFile.add_var(varName.c_str(), dataType, dim1, dim2, dim3, dim4), varDeleter);

    if (var)
      return var;

    throw Spine::Exception(BCP, "Failed to add variable ('" + varName + "')");
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Add coordinate variable
 *
 */
// ----------------------------------------------------------------------

boost::shared_ptr<NcVar> NetCdfStreamer::addCoordVariable(string dimName,
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
    throw Spine::Exception::Trace(BCP, "Operation failed!");
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
      throw Spine::Exception(BCP, "Failed to add attribute ('" + attrName + "')");
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
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

    throw Spine::Exception(BCP,
                           "Invalid time step length " + boost::lexical_cast<string>(timeStep));
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
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
      throw Spine::Exception(BCP,
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
        throw Spine::Exception(BCP,
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

    timeDim = addDimension("time", timeSize);

    timeVar = addVariable("time", ncInt, &(*timeDim));
    addAttribute(timeVar, "long_name", "time");
    addAttribute(timeVar, "calendar", "gregorian");
    addAttribute(timeVar, "units", timeUnitDef.c_str());

    if (!timeVar->put(times, timeSize))
      throw Spine::Exception(BCP, "Failed to store validtimes");
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
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
    throw Spine::Exception::Trace(BCP, "Operation failed!");
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

    auto tDim = addDimension(name, timeDim->size());
    tVar = addVariable(name, ncInt, &(*tDim));

    int times[timeDim->size()];
    timeVar->get(times, timeDim->size());

    if (!tVar->put(times, timeDim->size()))
      throw Spine::Exception(BCP, "Failed to store validtimes");

    addAttribute(tVar, "long_name", "time");
    addAttribute(tVar, "calendar", "gregorian");

    boost::shared_ptr<NcAtt> uAtt(timeVar->get_att("units"));
    if (!uAtt)
      throw Spine::Exception(BCP, "Failed to get time unit attribute");

    boost::shared_ptr<NcValues> uVal(uAtt->values());
    char *u;
    int uLen;

    if ((!uVal) || (!(u = (char *)uVal->base())) ||
        ((uLen = (uVal->num() * uVal->bytes_for_one())) < 1))
      throw Spine::Exception(BCP, "Failed to get time unit attribute value");

    string unit(u, uLen);
    addAttribute(tVar, "units", unit.c_str());

    return tDim;
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
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
    if (isSurfaceLevel(levelType))
      return;

    string name, positive;

    if (isPressureLevel(levelType))
    {
      name = "pressure";
      positive = "down";
    }
    else if (isHybridLevel(levelType))
    {
      name = "hybrid";
      positive = "up";
    }
    else if (isHeightLevel(levelType, 0))
    {
      name = "height";
      positive = "up";
    }
    else
    {
      name = "depth";

      if (levelType != nativeLevelType)
        // kFmiHeight with negative levels
        //
        positive = "up";
      else
        positive = (itsPositiveLevels ? "down" : "up");
    }

    auto levelVar =
        addCoordVariable(name, itsDataLevels.size(), ncFloat, "level", "", "Z", levelDim);

    addAttribute(levelVar, "long_name", (string(levelVar->name()) + " level").c_str());
    addAttribute(levelVar, "positive", positive.c_str());

    float levels[itsDataLevels.size()];
    int i = 0;

    for (auto level = itsDataLevels.begin(); (level != itsDataLevels.end()); level++, i++)
    {
      levels[i] = (float)*level;
    }

    if (!levelVar->put(levels, itsDataLevels.size()))
      throw Spine::Exception(BCP, "Failed to store levels");
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
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

    //	OGRSpatialReference * geometrySRS = itsResMgr.getGeometrySRS();
    //
    //	if (geometrySRS) {
    //	}
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
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
    OGRSpatialReference *geometrySRS = itsResMgr.getGeometrySRS();
    double lon_0, lat_0, lat_ts;

    if (!geometrySRS)
    {
      const NFmiStereographicArea &a = *(dynamic_cast<const NFmiStereographicArea *>(area));

      lon_0 = a.CentralLongitude();
      lat_0 = a.CentralLatitude();
      lat_ts = a.TrueLatitude();
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
    throw Spine::Exception::Trace(BCP, "Operation failed!");
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

    addAttribute(&ncFile, "Conventions", "CF-1.6");
    addAttribute(&ncFile, "title", "<title>");
    addAttribute(&ncFile, "institution", "fmi.fi");
    addAttribute(&ncFile, "source", "<producer>");

    // Time dimension

    addTimeDimension();

    // Level dimension

    addLevelDimension();

    // Set projection

    auto crsVar = addVariable("crs", ncShort);

    auto classid = area->ClassId();

    if (itsReqParams.areaClassId != A_Native)
      classid = itsReqParams.areaClassId;

    switch (classid)
    {
      case kNFmiLatLonArea:
        setLatLonGeometry(area, crsVar);
        break;
      case kNFmiStereographicArea:
        setStereographicGeometry(area, crsVar);
        break;
      default:
        throw Spine::Exception(BCP, "Unsupported projection in input data");
    }

    // Store y/x and/or lat/lon dimensions and coordinate variables, cropping the grid if manual
    // cropping is set

    bool projected = (classid != kNFmiLatLonArea);

    size_t x0 = (cropping.cropped ? cropping.bottomLeftX : 0),
           y0 = (cropping.cropped ? cropping.bottomLeftY : 0);
    size_t xN = (cropping.cropped ? (x0 + cropping.gridSizeX) : itsReqGridSizeX),
           yN = (cropping.cropped ? (y0 + cropping.gridSizeY) : itsReqGridSizeY);
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
      //		 "T(k,j,i) is associated with the coordinate values lon(j,i), lat(j,i), and
      // lev(k).
      // The
      // vertical coordinate is
      //		  represented by the coordinate variable lev(lev) and the latitude and
      // longitude
      // coordinates are represented by
      //		  the auxiliary coordinate variables lat(yc,xc) and lon(yc,xc) which are
      // identified
      // by
      // the
      // coordinates attribute.
      //
      //		  Note that coordinate variables are also defined for the xc and yc
      // dimensions. This
      // faciliates processing of this
      //		  data by generic applications that don't recognize the multidimensional
      // latitude
      // and
      // longitude coordinates."

      auto yVar = addCoordVariable("y", itsNY, ncFloat, "projection_y_coordinate", "m", "Y", yDim);
      auto xVar = addCoordVariable("x", itsNX, ncFloat, "projection_x_coordinate", "m", "X", xDim);

      NFmiPoint p0 =
          ((itsReqParams.datumShift == Plugin::Download::Datum::None) ? grid->GridToWorldXY(x0, y0)
                                                                      : tgtWorldXYs[x0][y0]);
      NFmiPoint pN = ((itsReqParams.datumShift == Plugin::Download::Datum::None)
                          ? grid->GridToWorldXY(xN - 1, yN - 1)
                          : tgtWorldXYs[xN - 1][yN - 1]);

      double worldY[itsNY], worldX[itsNX];
      double wY = p0.Y(), wX = p0.X();
      double stepY = yStep * ((itsNY > 1) ? ((pN.Y() - p0.Y()) / (yN - y0 - 1)) : 0.0);
      double stepX = xStep * ((itsNX > 1) ? ((pN.X() - p0.X()) / (xN - x0 - 1)) : 0.0);

      for (y = 0; (y < itsNY); wY += stepY, y++)
        worldY[y] = wY;
      for (x = 0; (x < itsNX); wX += stepX, x++)
        worldX[x] = wX;

      if (!yVar->put(worldY, itsNY))
        throw Spine::Exception(BCP, "Failed to store y -coordinates");

      if (!xVar->put(worldX, itsNX))
        throw Spine::Exception(BCP, "Failed to store x -coordinates");

      latVar = addVariable("lat", ncFloat, &(*yDim), &(*xDim));
      lonVar = addVariable("lon", ncFloat, &(*yDim), &(*xDim));

      for (y = y0, n = 0; (y < yN); y += yStep)
        for (x = x0; (x < xN); x += xStep, n++)
        {
          const NFmiPoint p =
              ((itsReqParams.datumShift == Plugin::Download::Datum::None) ? grid->GridToLatLon(x, y)
                                                                          : tgtLatLons[x][y]);

          lat[n] = p.Y();
          lon[n] = p.X();
        }

      if (!latVar->put(lat, itsNY, itsNX))
        throw Spine::Exception(BCP, "Failed to store latitude(y,x) coordinates");
      if (!lonVar->put(lon, itsNY, itsNX))
        throw Spine::Exception(BCP, "Failed to store longitude(y,x) coordinates");
    }
    else
    {
      // latlon, grid defined as cartesian product of latitude and longitude axes
      //
      latVar = addCoordVariable("lat", itsNY, ncFloat, "latitude", "degrees_north", "Y", latDim);
      lonVar = addCoordVariable("lon", itsNX, ncFloat, "longitude", "degrees_east", "X", lonDim);

      for (y = y0, n = 0; (y < yN); y += yStep, n++)
        lat[n] = ((itsReqParams.datumShift == Plugin::Download::Datum::None)
                      ? grid->GridToLatLon(0, y).Y()
                      : tgtLatLons[0][y].Y());

      for (x = x0, n = 0; (x < xN); x += xStep, n++)
        lon[n] = ((itsReqParams.datumShift == Plugin::Download::Datum::None)
                      ? grid->GridToLatLon(x, 0).X()
                      : tgtLatLons[x][0].X());

      if (!latVar->put(lat, itsNY))
        throw Spine::Exception(BCP, "Failed to store latitude coordinates");

      if (!lonVar->put(lon, itsNX))
        throw Spine::Exception(BCP, "Failed to store longitude coordinates");
    }

    addAttribute(latVar, "standard_name", "latitude");
    addAttribute(latVar, "long_name", "latitude");
    addAttribute(latVar, "units", "degrees_north");
    addAttribute(lonVar, "standard_name", "longitude");
    addAttribute(lonVar, "long_name", "longitude");
    addAttribute(lonVar, "units", "degrees_east");

    if (Plugin::Download::Datum::isDatumShiftToWGS84(itsReqParams.datumShift))
    {
      addAttribute(crsVar, "semi_major", Plugin::Download::Datum::Sphere::NetCdf::WGS84_semiMajor);
      addAttribute(crsVar,
                   "inverse_flattening",
                   Plugin::Download::Datum::Sphere::NetCdf::WGS84_invFlattening);
    }
    else if (projected)
      addAttribute(crsVar, "earth_radius", Plugin::Download::Datum::Sphere::NetCdf::Fmi_6371220m);
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
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

    throw Spine::Exception(
        BCP, "Invalid time period length " + boost::lexical_cast<string>(periodLengthInMinutes));
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
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

    boost::shared_ptr<NcDim> tDim(ncFile.get_dim(timeDimName.c_str()), dimDeleter);

    if (tDim)
      return tDim;

    // Add aggregate period length specific time dimension and variable

    boost::shared_ptr<NcVar> tVar;
    tDim = addTimeDimension(periodLengthInMinutes, tVar);

    // Add time bounds dimension

    if (!timeBoundsDim)
      timeBoundsDim = addDimension("time_bounds", 2);

    // Determine and store time bounds

    Spine::TimeSeriesGenerator::LocalTimeList::const_iterator timeIter = itsDataTimes.begin();
    ptime startTime = itsDataTimes.front().utc_time(), vt;
    int bounds[2 * timeDim->size()];
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

    auto timeBoundsVar = addVariable(name, ncInt, &(*tDim), &(*timeBoundsDim));

    if (!timeBoundsVar->put(bounds, timeDim->size(), 2))
      throw Spine::Exception(BCP, "Failed to store time bounds");

    // Connect the bounds to the time variable

    addAttribute(tVar, "bounds", name.c_str());

    return tDim;
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
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
    NcDim &yOrLat = (yDim ? *yDim : *latDim);
    NcDim &xOrLon = (yDim ? *xDim : *lonDim);

    NcDim *dim1;                                      // Time dimension
    NcDim *dim2 = levelDim ? &(*levelDim) : &yOrLat;  // Level or Y/lat dimension
    NcDim *dim3 = levelDim ? &yOrLat : &xOrLon;       // Y/lat or X/lon dimension
    NcDim *dim4 = levelDim ? &xOrLon : nullptr;       // X dimension or n/a

    boost::shared_ptr<NcDim> tDim;

    for (auto it = itsDataParams.begin(); (it != itsDataParams.end()); it++)
    {
      NFmiParam theParam(it->number());
      const ParamChangeTable &pTable = itsCfg.getParamChangeTable(false);
      string paramName, stdName, longName, unit, timeDimName = "time";
      size_t i, j;

      dim1 = &(*timeDim);

      signed long usedParId = theParam.GetIdent();

      for (i = j = 0; i < pTable.size(); ++i)
        if (usedParId == pTable[i].itsWantedParam.GetIdent())
        {
          if (relative_uv == (pTable[i].itsGridRelative ? *pTable[i].itsGridRelative : false))
            break;
          else if (j == 0)
            j = i + 1;
          else
            throw Spine::Exception(BCP,
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
          dim1 = &(*tDim);
        }
      }
      else
        paramName = theParam.GetName();

      auto dataVar = addVariable(paramName + "_" + boost::lexical_cast<string>(usedParId),
                                 ncFloat,
                                 dim1,
                                 dim2,
                                 dim3,
                                 dim4);
      addAttribute(dataVar, "units", unit.c_str());
      addAttribute(dataVar, "_FillValue", kFloatMissing);
      addAttribute(dataVar, "missing_value", kFloatMissing);
      addAttribute(dataVar, "grid_mapping", "crs");

      if (!stdName.empty())
        addAttribute(dataVar, "standard_name", stdName.c_str());

      if (!longName.empty())
        addAttribute(dataVar, "long_name", longName.c_str());

      if ((i < pTable.size()) && (!pTable[i].itsStepType.empty()))
        // Cell method for aggregate data
        //
        addAttribute(dataVar, "cell_methods", (timeDimName + ": " + pTable[i].itsStepType).c_str());

      if (yDim)
        addAttribute(dataVar, "coordinates", "lat lon");

      dataVars.push_back(dataVar.get());
    }

    it_Var = dataVars.begin();
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
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

    bool cropxy = (cropping.cropped && cropping.cropMan);
    size_t x0 = (cropxy ? cropping.bottomLeftX : 0), y0 = (cropxy ? cropping.bottomLeftY : 0);
    size_t xN = (cropping.cropped ? (x0 + cropping.gridSizeX) : itsReqGridSizeX),
           yN = (cropping.cropped ? (y0 + cropping.gridSizeY) : itsReqGridSizeY);
    size_t xStep = (itsReqParams.gridStepXY ? (*(itsReqParams.gridStepXY))[0].first : 1),
           yStep = (itsReqParams.gridStepXY ? (*(itsReqParams.gridStepXY))[0].second : 1), x, y;

    boost::shared_ptr<float[]> values(new float[itsNY * itsNX]);

    int i = 0;

    for (y = y0; (y < yN); y += yStep)
      for (x = x0; (x < xN); x += xStep, i++)
      {
        auto value = itsGridValues[x][y];

        if (value != kFloatMissing)
          values[i] = (value + itsScalingIterator->second) / itsScalingIterator->first;
        else
          values[i] = value;
      }

    // Store the values for current parameter/[level/]validtime.
    //
    // First skip variables for missing parameters if any.
    //
    // Note: timeIndex was incremented after getting the data

    if ((it_Var == dataVars.begin()) && (itsParamIterator != itsDataParams.begin()))
      for (auto it_p = itsDataParams.begin(); (it_p != itsParamIterator); it_p++)
        it_Var++;

    if (!(*it_Var)->set_cur(itsTimeIndex - 1, levelDim ? itsLevelIndex : -1))
      throw Spine::Exception(BCP, "Failed to set active netcdf time/level");

    long edge1 = 1;                         // Time dimension, edge length 1
    long edge2 = levelDim ? 1 : itsNY;      // Level (edge length 1) or Y dimension
    long edge3 = levelDim ? itsNY : itsNX;  // Y or X dimension
    long edge4 = levelDim ? itsNX : -1;     // X dimension or n/a

    if (!(*it_Var)->put(values.get(), edge1, edge2, edge3, edge4))
      throw Spine::Exception(BCP, "Failed to store netcdf variable values");
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Handle change of parameter
 *
 */
// ----------------------------------------------------------------------

void NetCdfStreamer::paramChanged()
{
  try
  {
    // Note: Netcdf varibles are created when first nonmissing querydata parameter is encountered

    if (dataVars.size() > 0)
    {
      if (it_Var != dataVars.end())
        it_Var++;

      if ((it_Var == dataVars.end()) && (itsParamIterator != itsDataParams.end()))
        throw Spine::Exception(BCP, "paramChanged: internal: No more netcdf variables");
    }
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
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
    if (setMeta)
    {
      // Set geometry and dimensions
      //
      setGeometry(q, area, grid);

      // Add parameters

      addParameters(q->isRelativeUV());

      setMeta = false;
    }

    // Data is loaded from 'values'; set nonempty chunk to indicate data is available.

    chunk = " ";
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

}  // namespace Download
}  // namespace Plugin
}  // namespace SmartMet

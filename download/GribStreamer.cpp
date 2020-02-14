// ======================================================================
/*!
 * \brief SmartMet download service plugin; grib streaming
 */
// ======================================================================

#include "GribStreamer.h"
#include "Datum.h"
#include "Plugin.h"

#include <spine/Exception.h>

#include <newbase/NFmiEnumConverter.h>
#include <newbase/NFmiQueryData.h>
#include <newbase/NFmiQueryDataUtil.h>
#include <newbase/NFmiStereographicArea.h>
#include <newbase/NFmiTimeList.h>

#include <boost/foreach.hpp>
#include <string>

#include <sys/types.h>
#include <unistd.h>

#include <boost/interprocess/sync/lock_options.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>

#include "boost/date_time/gregorian/gregorian.hpp"

static const long gribMissingValue = 9999;

using namespace std;

using namespace boost::posix_time;
using namespace boost::interprocess;

namespace SmartMet
{
namespace Plugin
{
namespace Download
{
template <typename T>
boost::optional<vector<pair<T, T>>> nPairsOfValues(string &pvs, const char *param, size_t nPairs);

GribStreamer::GribStreamer(const Spine::HTTP::Request &req,
                           const Config &config,
                           const Producer &producer,
                           OutputFormat outputFormat,
                           unsigned int grib2TablesVersion)
    : DataStreamer(req, config, producer), grib1(outputFormat == Grib1)
{
  try
  {
    // Get grib handle

    grib_context *c = grib_context_get_default();
    gribHandle = grib_handle_new_from_samples(c, grib1 ? "GRIB1" : "GRIB2");
    if (!gribHandle)
      throw Spine::Exception(BCP, string("Could not get handle for grib") + (grib1 ? "1" : "2"));

    // Set tables version for grib2

    if (grib2TablesVersion > 0)
      gset(gribHandle, "gribMasterTablesVersionNumber", (unsigned long)grib2TablesVersion);
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

GribStreamer::~GribStreamer()
{
  if (gribHandle)
    grib_handle_delete(gribHandle);
}

// ----------------------------------------------------------------------
/*!
 * \brief Determine grid x/y scanning directions
 */
// ----------------------------------------------------------------------

void GribStreamer::scanningDirections(long &iNegative, long &jPositive) const
{
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wswitch-enum"
  try
  {
    // newbase enum includes all kinds of variations
    // which are useless here. Should use specific
    // enums for specific purposes
    //
    // ???
    //
    // Did not quite get the comment above in plugin's point of view, using the same enums as
    // newbase
    // ...
    //
    //		e.g. NFmiGridBase.cpp:
    //
    //		switch(itsStartingCorner)
    //		{
    //		case kBottomLeft:
    //		  return true;
    //		case kBottomRight:
    //		  {
    //		  }
    //		case kTopLeft:
    //		  {
    //		  }
    //		case kTopRight:
    //		  {
    //		  }
    //		default:
    //		  {
    //		  }
    //		}

    switch (itsGridOrigo)
    {
      case kTopLeft:
        iNegative = 0;
        jPositive = 0;
        break;

      case kTopRight:
        iNegative = 1;
        jPositive = 0;
        break;

      case kBottomLeft:
        iNegative = 0;
        jPositive = 1;
        break;

      case kBottomRight:
        iNegative = 1;
        jPositive = 1;
        break;

      default:
        throw Spine::Exception(BCP, "Unknown grid scanning mode");
    }
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
#pragma clang diagnostic pop
}

// ----------------------------------------------------------------------
/*!
 * \brief Set grib latlon projection metadata
 */
// ----------------------------------------------------------------------

void GribStreamer::setLatlonGeometryToGrib() const
{
  try
  {
    gset(gribHandle, "typeOfGrid", "regular_ll");

    gset(gribHandle, "longitudeOfFirstGridPointInDegrees", itsBoundingBox.bottomLeft.X());
    gset(gribHandle, "latitudeOfFirstGridPointInDegrees", itsBoundingBox.bottomLeft.Y());
    gset(gribHandle, "longitudeOfLastGridPointInDegrees", itsBoundingBox.topRight.X());
    gset(gribHandle, "latitudeOfLastGridPointInDegrees", itsBoundingBox.topRight.Y());

    gset(gribHandle, "Ni", itsNX);
    gset(gribHandle, "Nj", itsNY);

    double gridCellHeightInDegrees =
        (itsBoundingBox.topRight.Y() - itsBoundingBox.bottomLeft.Y()) / (itsNY - 1);
    double gridCellWidthInDegrees =
        (itsBoundingBox.topRight.X() - itsBoundingBox.bottomLeft.X()) / (itsNX - 1);

    long iNegative, jPositive;

    scanningDirections(iNegative, jPositive);

    gset(gribHandle, "jScansPositively", jPositive);
    gset(gribHandle, "iScansNegatively", iNegative);

    gset(gribHandle, "iDirectionIncrementInDegrees", gridCellWidthInDegrees);
    gset(gribHandle, "jDirectionIncrementInDegrees", gridCellHeightInDegrees);

    // DUMP(gribHandle, "geography");
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Set grib rotated latlon projection metadata
 *
 */
// ----------------------------------------------------------------------

void GribStreamer::setRotatedLatlonGeometryToGrib(const NFmiArea *area) const
{
  try
  {
    if (itsResMgr.getGeometrySRS())
      throw Spine::Exception(BCP, "setRotatedLatlonGeometryToGrib: use of SRS not supported");

    const NFmiRotatedLatLonArea &a = *(dynamic_cast<const NFmiRotatedLatLonArea *>(area));

    BBoxCorners rotLLBBox;
    rotLLBBox.bottomLeft = a.ToRotLatLon(itsBoundingBox.bottomLeft);
    rotLLBBox.topRight = a.ToRotLatLon(itsBoundingBox.topRight);

    gset(gribHandle, "typeOfGrid", "rotated_ll");

    gset(gribHandle, "longitudeOfFirstGridPointInDegrees", rotLLBBox.bottomLeft.X());
    gset(gribHandle, "latitudeOfFirstGridPointInDegrees", rotLLBBox.bottomLeft.Y());
    gset(gribHandle, "longitudeOfLastGridPointInDegrees", rotLLBBox.topRight.X());
    gset(gribHandle, "latitudeOfLastGridPointInDegrees", rotLLBBox.topRight.Y());

    gset(gribHandle, "Ni", itsNX);
    gset(gribHandle, "Nj", itsNY);

    double gridCellHeightInDegrees =
        (rotLLBBox.topRight.Y() - rotLLBBox.bottomLeft.Y()) / (itsNY - 1);
    double gridCellWidthInDegrees =
        (rotLLBBox.topRight.X() - rotLLBBox.bottomLeft.X()) / (itsNX - 1);

    long iNegative, jPositive;

    scanningDirections(iNegative, jPositive);

    gset(gribHandle, "jScansPositively", jPositive);
    gset(gribHandle, "iScansNegatively", iNegative);

    gset(gribHandle, "iDirectionIncrementInDegrees", gridCellWidthInDegrees);
    gset(gribHandle, "jDirectionIncrementInDegrees", gridCellHeightInDegrees);

    if (a.SouthernPole().X() != 0)
      throw Spine::Exception(
          BCP, "GRIB does not support rotated latlon areas where longitude is also rotated");

    gset(gribHandle, "latitudeOfSouthernPoleInDegrees", a.SouthernPole().Y());

    // DUMP(gribHandle, "geography");
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Set grib stereographic projection metadata
 *
 * Defaults obtained by a DUMP call once the projection is set:
 *
 * Nx = 16
 * Ny = 31
 * latitudeOfFirstGridPointInDegrees = 60
 * longitudeOfFirstGridPointInDegrees = 0
 * LaDInDegrees = 0
 * orientationOfTheGridInDegrees = 0
 * DxInMetres = 2000
 * DyInMetres = 2000
 * iScansNegatively = 0
 * jScansPositively = 0
 * jPointsAreConsecutive = 0
 * gridType = "polar_stereographic"
 * bitmapPresent = 0
 *
 * HOWEVER: GRIB1 has a fixed true latitude of 60 degrees, atleast if you
 * look at /usr/share/grib_api/definitions/grib1/grid_definition_5.def
 */
// ----------------------------------------------------------------------

void GribStreamer::setStereographicGeometryToGrib(const NFmiArea *area) const
{
  try
  {
    gset(gribHandle, "typeOfGrid", "polar_stereographic");

    // Note: grib2 longitude 0-360

    double lon = itsBoundingBox.bottomLeft.X();

    if ((!grib1) && (lon < 0))
      lon += 360;

    gset(gribHandle, "longitudeOfFirstGridPointInDegrees", lon);
    gset(gribHandle, "latitudeOfFirstGridPointInDegrees", itsBoundingBox.bottomLeft.Y());

    gset(gribHandle, "Ni", itsNX);
    gset(gribHandle, "Nj", itsNY);

    gset(gribHandle, "DxInMetres", itsDX);
    gset(gribHandle, "DyInMetres", itsDY);

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

    if ((!grib1) && (lon_0 < 0))
      lon_0 += 360;

    gset(gribHandle, "orientationOfTheGridInDegrees", lon_0);

    long iNegative, jPositive;

    scanningDirections(iNegative, jPositive);

    gset(gribHandle, "jScansPositively", jPositive);
    gset(gribHandle, "iScansNegatively", iNegative);

    if (!grib1)
      gset(gribHandle, "LaDInDegrees", lat_ts);
    else if (lat_ts != 60)
      throw Spine::Exception(
          BCP,
          "GRIB1 true latitude can only be 60 for polar stereographic projections with grib_api "
          "library");

    if (lat_0 != 90 && lat_0 != -90)
      throw Spine::Exception(BCP, "GRIB format supports only polar stereographic projections");

    if (lat_0 != 90)
      throw Spine::Exception(BCP, "Only N-pole polar stereographic projections are supported");

    // DUMP(gribHandle,"geography");
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Set grib mercator projection metadata
 *
 */
// ----------------------------------------------------------------------

void GribStreamer::setMercatorGeometryToGrib() const
{
  try
  {
    gset(gribHandle, "typeOfGrid", "mercator");

    gset(gribHandle, "longitudeOfFirstGridPointInDegrees", itsBoundingBox.bottomLeft.X());
    gset(gribHandle, "latitudeOfFirstGridPointInDegrees", itsBoundingBox.bottomLeft.Y());

    gset(gribHandle, "longitudeOfLastGridPointInDegrees", itsBoundingBox.topRight.X());
    gset(gribHandle, "latitudeOfLastGridPointInDegrees", itsBoundingBox.topRight.Y());

    gset(gribHandle, "Ni", itsNX);
    gset(gribHandle, "Nj", itsNY);

    gset(gribHandle, "DiInMetres", itsDX);
    gset(gribHandle, "DjInMetres", itsDY);

    long iNegative, jPositive;

    scanningDirections(iNegative, jPositive);

    gset(gribHandle, "jScansPositively", jPositive);
    gset(gribHandle, "iScansNegatively", iNegative);

    double lon_0 = 0;
    double lat_ts = 0;

    gset(gribHandle, "orientationOfTheGridInDegrees", lon_0);
    gset(gribHandle, "LaDInDegrees", lat_ts);

    // DUMP(gribHandle,"geography");
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Set grib named configuration settings.
 *
 */
// ----------------------------------------------------------------------

void GribStreamer::setNamedSettingsToGrib() const
{
  try
  {
    const Producer &pr = itsCfg.getProducer(itsReqParams.producer);
    auto setBeg = pr.namedSettingsBegin();
    auto setEnd = pr.namedSettingsEnd();
    const char *const centre = "centre";
    bool hasCentre = false;

    for (auto it = setBeg; (it != setEnd); it++)
    {
      gset(gribHandle, (it->first).c_str(), it->second);

      if (it->first == centre)
        hasCentre = true;
    }

    // Use default procuder's centre by default

    if (!hasCentre)
    {
      const Producer &dpr = itsCfg.defaultProducer();
      const auto dit = dpr.namedSettings.find(centre);

      if (dit != dpr.namedSettingsEnd())
        gset(gribHandle, (dit->first).c_str(), dit->second);
    }
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Set grib projection metadata
 *
 */
// ----------------------------------------------------------------------

void GribStreamer::setGeometryToGrib(const NFmiArea *area, bool relative_uv)
{
  try
  {
    auto classid = area->ClassId();

    if (itsReqParams.areaClassId != A_Native)
      classid = itsReqParams.areaClassId;

    valueArray.resize(itsNX * itsNY);

    switch (classid)
    {
      case kNFmiLatLonArea:
        setLatlonGeometryToGrib();
        break;
      case kNFmiRotatedLatLonArea:
        setRotatedLatlonGeometryToGrib(area);
        break;
      case kNFmiStereographicArea:
        setStereographicGeometryToGrib(area);
        break;
      case kNFmiMercatorArea:
        setMercatorGeometryToGrib();
        break;
      case kNFmiEquiDistArea:
        throw Spine::Exception(BCP, "Equidistant projection is not supported by GRIB");
      case kNFmiGnomonicArea:
        throw Spine::Exception(BCP, "Gnomonic projection is not supported by GRIB");
      case kNFmiPKJArea:
        throw Spine::Exception(BCP, "PKJ projection is not supported by GRIB");
      case kNFmiYKJArea:
        throw Spine::Exception(BCP, "YKJ projection is not supported by GRIB");
      case kNFmiKKJArea:
        throw Spine::Exception(BCP, "KKJ projection is not supported by GRIB");
      default:
        throw Spine::Exception(BCP, "Unsupported projection in input data");
    }

    // Set packing type

    if (!itsReqParams.packing.empty())
      gset(gribHandle, "packingType", itsReqParams.packing);

    // Set shape of the earth depending on the datum

    long resolAndCompFlags = get_long(gribHandle, "resolutionAndComponentFlags");

    if (grib1)
    {
      if (Datum::isDatumShiftToWGS84(itsReqParams.datumShift))
        resolAndCompFlags |= (1 << Datum::Sphere::Grib1::WGS84);
      else
        resolAndCompFlags &= ~(1 << Datum::Sphere::Grib1::WGS84);
    }
    else
      gset(gribHandle,
           "shapeOfTheEarth",
           (Datum::isDatumShiftToWGS84(itsReqParams.datumShift)
                ? Datum::Sphere::Grib2::WGS84
                : Datum::Sphere::Grib2::Fmi_6371229m));

    if (relative_uv)
      resolAndCompFlags |= (1 << 3);
    else
      resolAndCompFlags &= ~(1 << 3);

    gset(gribHandle, "resolutionAndComponentFlags", resolAndCompFlags);

    // Bitmap to flag missing values

    gset(gribHandle, "bitmapPresent", 1);
    gset(gribHandle, "missingValue", gribMissingValue);
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Set grib level and parameter. Parameter's index in config
 * 		table is returned in 'paramIdx'
 *
 */
// ----------------------------------------------------------------------

void GribStreamer::setLevelAndParameterToGrib(int level,
                                              const NFmiParam &theParam,
                                              const ParamChangeTable &pTable,
                                              size_t &paramIdx)
{
  // Get parameter id, and configured level type and value for surface data.
  //
  // Using hardcoded level types for pressure, hybrid and height/depth data and for
  // surface data if level configuration is missing.

#define PressureLevel "isobaricInhPa"
#define HybridLevel "hybrid"
#define EntireAtmosphere "entireAtmosphere"
#define HeightLevel "heightAboveSea"
#define DepthLevel "depthBelowSea"

  try
  {
    string centre;
    signed long usedParId = theParam.GetIdent();
    NFmiLevel *cfgLevel = nullptr;
    string levelTypeStr;
    size_t i, j;
    long templateNumber = 0;

    for (i = 0, j = pTable.size(); i < pTable.size(); ++i)
      if (usedParId == pTable[i].itsWantedParam.GetIdent())
      {
        // Preferring entry with level for surface data and without level for pressure and hybrid
        // data.
        // If preferred entry does not exist, taking the parameter id from the first entry for the
        // parameter.
        //
        cfgLevel = pTable[i].itsLevel;

        if ((isSurfaceLevel(levelType) && cfgLevel) || (!(isSurfaceLevel(levelType) || cfgLevel)))
          break;

        if (j == pTable.size())
          j = i;
      }

    if (i >= pTable.size())
      i = j;

    paramIdx = i;

    if (i < pTable.size())
    {
      usedParId = pTable[i].itsOriginalParamId;
      cfgLevel = pTable[i].itsLevel;
      centre = pTable[i].itsCentre;
      templateNumber = (long)pTable[i].itsTemplateNumber;
    }

    if (isSurfaceLevel(levelType))
    {
      if (cfgLevel)
      {
        levelTypeStr = cfgLevel->GetName();
        level = boost::numeric_cast<int>(cfgLevel->LevelValue());
      }
      else
      {
        levelTypeStr = EntireAtmosphere;
        level = 0;
      }
    }
    else if (isPressureLevel(levelType))
      levelTypeStr = PressureLevel;
    else if (isHybridLevel(levelType))
      levelTypeStr = HybridLevel;
    else if (isHeightLevel(levelType, level))
      levelTypeStr = HeightLevel;
    else if (isDepthLevel(levelType, level))
      levelTypeStr = DepthLevel;
    else
      throw Spine::Exception(
          BCP, "Internal: Unrecognized level type " + boost::lexical_cast<string>(levelType));

    if (!centre.empty())
      gset(gribHandle, "centre", centre);

    // Cannot set template number 0 unless stepType has been set
    gset(gribHandle, "stepType", "instant");
    if ((!grib1) && (templateNumber != 0))
      gset(gribHandle, "productDefinitionTemplateNumber", templateNumber);

    gset(gribHandle, "paramId", usedParId);
    gset(gribHandle, "typeOfLevel", levelTypeStr);
    gset(gribHandle, "level", boost::numeric_cast<long>(abs(level)));
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Set step data into grib buffer
 */
// ----------------------------------------------------------------------

void GribStreamer::setStepToGrib(const ParamChangeTable &pTable,
                                 size_t paramIdx,
                                 bool setOriginTime,
                                 const ptime &validTime)
{
  try
  {
    // stepUnits always 'minute' to keep it simple

    string stepUnits = "m", stepType;
    time_duration fromOriginTime(validTime - gribOriginTime);
    long step = (fromOriginTime.hours() * 60) + fromOriginTime.minutes();
    long startStep = step, endStep = step;

    // Set step type and calculate start and end step for aggregates.
    //
    // Note: There's no metadata available about whether given data/parameter has start or end time
    // stamping;
    //	     stamping is selected with boolean 'bDataIsEndTimeStamped'.

    const bool bDataIsEndTimeStamped = true;

    if ((paramIdx < pTable.size()) && (!pTable[paramIdx].itsStepType.empty()))
    {
      // Aggregate period length must be the same or multiple of data time step for time steps less
      // than day
      //
      long timeStep = ((itsReqParams.timeStep > 0) ? itsReqParams.timeStep : itsDataTimeStep);

      if (timeStep <= 0)
        throw Spine::Exception(BCP,
                               "Invalid data timestep " + boost::lexical_cast<string>(timeStep) +
                                   " for producer '" + itsReqParams.producer + "'");

      if (pTable[paramIdx].itsPeriodLengthMinutes > 0)
      {
        if (((itsDataTimeStep < minutesInDay) &&
             (pTable[paramIdx].itsPeriodLengthMinutes % itsDataTimeStep)) ||
            ((timeStep >= minutesInDay) && (pTable[paramIdx].itsPeriodLengthMinutes != timeStep)) ||
            (timeStep > minutesInMonth))
          throw Spine::Exception(
              BCP,
              "Aggregate period length " +
                  boost::lexical_cast<string>(pTable[paramIdx].itsPeriodLengthMinutes) +
                  " min is not valid for data time step " + boost::lexical_cast<string>(timeStep) +
                  " min");

        if (timeStep < minutesInDay)
        {
          time_duration td(validTime.time_of_day());
          long validTimeMinutes = (td.hours() * 60) + td.minutes();
          long periodLengthMinutes = pTable[paramIdx].itsPeriodLengthMinutes;
          long periodStartMinutes = (validTimeMinutes / periodLengthMinutes) * periodLengthMinutes;

          if (bDataIsEndTimeStamped)
          {
            // Use validtime as end step
            //
            if (periodStartMinutes == validTimeMinutes)
            {
              // Set start step backwards to the start of ending/full aggregate period
              //
              startStep = step - periodLengthMinutes;
            }
            else
            {
              // Set start step backwards to the start of current/incomplete aggregate period
              //
              startStep = step - (validTimeMinutes - periodStartMinutes);
            }
          }
          else
          {
            // Set start step to the start of current/incomplete aggregate period and advance end
            // step
            //
            startStep = step - (validTimeMinutes - periodStartMinutes);
            endStep += itsDataTimeStep;
          }
        }
      }

      if (timeStep >= minutesInDay)
      {
        // Note: For daily and monthly data aggregate period length (if given/nonzero) must equal
        // time
        // step;
        //		 we do not support cumulative aggregates
        //
        ptime validTimeDate = ptime(validTime.date()), periodStart, periodEnd;

        if (bDataIsEndTimeStamped)
        {
          if (timeStep == minutesInDay)
          {
            // Previous day
            //
            periodStart = ptime((validTimeDate - time_duration(1, 0, 0)).date());
            periodEnd = validTimeDate;
          }
          else
          {
            // Previous month
            //
            boost::gregorian::date d((validTimeDate - time_duration(1, 0, 0)).date());
            periodStart = ptime(boost::gregorian::date(d.year(), d.month(), 1));
            periodEnd = ptime(boost::gregorian::date(
                validTimeDate.date().year(), validTimeDate.date().month(), 1));
          }
        }
        else
        {
          if (timeStep == minutesInDay)
          {
            // Current day
            //
            periodStart = validTimeDate;
            periodEnd = ptime((periodStart + time_duration(25, 0, 0)).date());
          }
          else
          {
            // Current month
            //
            periodStart = ptime(boost::gregorian::date(
                validTimeDate.date().year(), validTimeDate.date().month(), 1));
            ptime t(periodStart + time_duration(32 * 24, 0, 0));
            periodEnd = ptime(boost::gregorian::date(t.date().year(), t.date().month(), 1));
          }
        }

        startStep = (periodStart - gribOriginTime).hours() * 60;
        endStep = (periodEnd - gribOriginTime).hours() * 60;
      }

      if (startStep < 0)
      {
        // Can't be negative, set start step to 0 and adjust origintime and end step accordingly
        //
        gribOriginTime -= time_duration(0, -startStep, 0);
        endStep -= startStep;
        startStep = 0;

        setOriginTime = true;
      }

      gset(gribHandle, "stepType", pTable[paramIdx].itsStepType);
    }

    if (setOriginTime)
    {
      boost::gregorian::date d = gribOriginTime.date();
      time_duration t = gribOriginTime.time_of_day();

      long dateLong = d.year() * 10000 + d.month() * 100 + d.day();
      long timeLong = t.hours() * 100 + t.minutes();

      gset(gribHandle, "date", dateLong);
      gset(gribHandle, "time", timeLong);
    }

    // Set time step and unit

    gset(gribHandle, "stepUnits", stepUnits);
    gset(gribHandle, "startStep", startStep);
    gset(gribHandle, "endStep", endStep);
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Return time adjusted backwards to even timestep
 *
 */
// ----------------------------------------------------------------------

ptime adjustToTimeStep(const ptime &pt, long timeStepInMinutes)
{
  try
  {
    if (timeStepInMinutes <= 0)
      throw Spine::Exception(BCP,
                             "adjustToTimeStep: Invalid data timestep " +
                                 boost::lexical_cast<string>(timeStepInMinutes));

    if ((timeStepInMinutes == 60) || (timeStepInMinutes == 180) || (timeStepInMinutes == 360) ||
        (timeStepInMinutes == 720))
      return ptime(pt.date(),
                   time_duration(pt.time_of_day().hours() -
                                     (pt.time_of_day().hours() % (timeStepInMinutes / 60)),
                                 0,
                                 0));
    else if (timeStepInMinutes == DataStreamer::minutesInDay)
      return ptime(pt.date(), time_duration(0, 0, 0));
    else if (timeStepInMinutes == DataStreamer::minutesInMonth)
      return ptime(boost::gregorian::date(pt.date().year(), pt.date().month(), 1),
                   time_duration(0, 0, 0));

    return pt;
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Copy data (one level/param/time grid) into grib buffer
 *
 */
// ----------------------------------------------------------------------

void GribStreamer::addValuesToGrib(Engine::Querydata::Q q,
                                   const NFmiMetTime &vTime,
                                   int level,
                                   const NFmiDataMatrix<float> &dataValues,
                                   float scale,
                                   float offset)
{
  try
  {
    // Set named configuration settings

    setNamedSettingsToGrib();

    // Use first validtime as origintime if it is earlier than the origintime.
    //
    // Note: originTime is unset (is_not_a_date_time()) when called for first time instant.
    //
    //		 If the actual data origintime is used, adjust it backwards to even data timestep;
    //		 the output validtimes are set as number of timesteps forwards from the origintime.

    ptime oTime = q->originTime();
    ptime validTime = vTime;
    bool setOriginTime = (itsOriginTime.is_not_a_date_time() || (itsOriginTime != oTime));

    if (setOriginTime)
    {
      // Set origintime
      //
      itsOriginTime = oTime;
      gribOriginTime =
          ((validTime < itsOriginTime) ? validTime
                                       : adjustToTimeStep(itsOriginTime, itsDataTimeStep));
    }

    // Set level and parameter. Parameter's index in 'ptable' is returned in paramIdx (needed in
    // setStep())

    NFmiParam param(*(q->param().GetParam()));
    const ParamChangeTable &pTable = itsCfg.getParamChangeTable();
    size_t paramIdx = pTable.size();

    setLevelAndParameterToGrib(level, param, pTable, paramIdx);

    // Set start and end step and step type (for average, cumulative etc. data)

    setStepToGrib(pTable, paramIdx, setOriginTime, validTime);

    // Load the data, cropping the grid/values it if manual cropping is set

    bool cropxy = (cropping.cropped && cropping.cropMan);
    size_t x0 = (cropxy ? cropping.bottomLeftX : 0), y0 = (cropxy ? cropping.bottomLeftY : 0);
    size_t xN = (cropping.cropped ? (x0 + cropping.gridSizeX) : itsReqGridSizeX),
           yN = (cropping.cropped ? (y0 + cropping.gridSizeY) : itsReqGridSizeY);

    size_t xStep = (itsReqParams.gridStepXY ? (*(itsReqParams.gridStepXY))[0].first : 1),
           yStep = (itsReqParams.gridStepXY ? (*(itsReqParams.gridStepXY))[0].second : 1), x, y;
    int i = 0;

    for (y = y0; (y < yN); y += yStep)
      for (x = x0; (x < xN); x += xStep, i++)
      {
        float value = dataValues[x][y];

        if (value != kFloatMissing)
          valueArray[i] = (value + offset) / scale;
        else
          valueArray[i] = gribMissingValue;
      }

    grib_set_double_array(gribHandle, "values", &valueArray[0], valueArray.size());
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Add given data values and return complete grib message
 */
// ----------------------------------------------------------------------

string GribStreamer::getGribMessage(Engine::Querydata::Q q,
                                    int level,
                                    const NFmiMetTime &mt,
                                    const NFmiDataMatrix<float> &values,
                                    float scale,
                                    float offset)
{
  try
  {
    addValuesToGrib(q, mt, level, values, scale, offset);

    const void *mesg;
    size_t mesg_len;
    grib_get_message(gribHandle, &mesg, &mesg_len);

    if (mesg_len == 0)
      throw Spine::Exception(BCP, "Empty grib message returned");

    return string((const char *)mesg, mesg_len);
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

std::string GribStreamer::getChunk()
{
  try
  {
    try
    {
      ostringstream chunkBuf;
      string chunk;
      size_t chunkBufLength = 0, nChunks = 0;

      while (!isDone)
      {
        // Get next chunk e.g. next param/level/validtime grid
        //
        extractData(chunk);
        nChunks++;

        if (chunk.empty())
          isDone = true;
        else
          chunkBufLength += chunk.length();

        // To avoid small chunk transfer overhead collect chunks until max chunk length or max count
        // of
        // collected chunks are reached

        if (isDone || (nChunks >= itsMaxMsgChunks) || (chunkBufLength >= itsChunkLength))
        {
          if (isDone)
            setStatus(ContentStreamer::StreamerStatus::EXIT_OK);

          if (nChunks > 1)
          {
            chunkBuf << chunk;
            return chunkBuf.str();
          }

          return chunk;
        }

        chunkBuf << chunk;
      }

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

// ----------------------------------------------------------------------
/*!
 * \brief Load chunk of data; called by DataStreamer to get format specific chunk.
 *
 */
// ----------------------------------------------------------------------

void GribStreamer::getDataChunk(Engine::Querydata::Q q,
                                const NFmiArea *area,
                                NFmiGrid * /* grid */,
                                int level,
                                const NFmiMetTime &mt,
                                NFmiDataMatrix<float> &values,
                                string &chunk)
{
  try
  {
    if (setMeta)
    {
      // Set geometry
      //
      setGeometryToGrib(area, q->isRelativeUV());
      setMeta = false;
    }

    // Build and get grib message

    chunk =
        getGribMessage(q, level, mt, values, itsScalingIterator->first, itsScalingIterator->second);
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

}  // namespace Download
}  // namespace Plugin
}  // namespace SmartMet

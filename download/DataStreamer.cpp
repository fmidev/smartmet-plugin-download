// ======================================================================
/*!
 * \brief SmartMet download service plugin; data streaming
 */
// ======================================================================

#include "DataStreamer.h"
#include "Datum.h"
#include "Plugin.h"

#include <newbase/NFmiAreaFactory.h>
#include <newbase/NFmiEnumConverter.h>
#include <newbase/NFmiQueryData.h>
#include <newbase/NFmiQueryDataUtil.h>
#include <newbase/NFmiStereographicArea.h>
#include <newbase/NFmiTimeList.h>
#include <newbase/NFmiMetTime.h>

#include <gis/DEM.h>
#include <gis/LandCover.h>

#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/foreach.hpp>
#include <macgyver/StringConversion.h>
#include <spine/Exception.h>
#include <string>

#include <sys/types.h>
#include <unistd.h>
#include <unordered_set>

#include <gdal/ogr_geometry.h>

static const long maxChunkLengthInBytes = 2048 * 2048;  // Max length of data chunk to return
static const long maxMsgChunks = 30;  // Max # of data chunks collected and returned as one chunk

using namespace std;

using namespace boost::gregorian;
using namespace boost::posix_time;

namespace SmartMet
{
namespace Plugin
{
namespace Download
{
// Extern
template <typename T>
boost::optional<vector<pair<T, T>>> nPairsOfValues(string &pvs, const char *param, size_t nPairs);

ResMgr::ResMgr() : area(), grid(), spatialReferences(), transformations(), geometrySRS(nullptr) {}

ResMgr::~ResMgr()
{
  // Delete coordinate transformations
  //
  BOOST_FOREACH (OGRCoordinateTransformation *ct, transformations)
  {
    OGRCoordinateTransformation::DestroyCT(ct);
  }
  
  // Delete cloned srs:s
  //
  // Note: If geometrySRS is nonnull, the object pointed by it gets deleted too
  //
  BOOST_FOREACH (OGRSpatialReference *srs, spatialReferences)
  {
    OGRSpatialReference::DestroySpatialReference(srs);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Create area with given projection string
 */
// ----------------------------------------------------------------------

void ResMgr::createArea(string &projection)
{
  try
  {
    area = NFmiAreaFactory::Create(projection);

    if (!area.get())
      throw Spine::Exception(BCP, "Could not create projection '" + projection + "'");
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Get current projected area object
 */
// ----------------------------------------------------------------------

const NFmiArea *ResMgr::getArea()
{
  try
  {
    return area.get();
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}
// ----------------------------------------------------------------------
/*!
 * \brief (Re)create grid
 */
// ----------------------------------------------------------------------

void ResMgr::createGrid(const NFmiArea &a, size_t gridSizeX, size_t gridSizeY)
{
  try
  {
    grid.reset(new NFmiGrid(&a, gridSizeX, gridSizeY));

    if (!grid.get())
      throw Spine::Exception(BCP, "Internal: could not create grid");
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Check if suitable grid exists
 */
// ----------------------------------------------------------------------

bool ResMgr::hasGrid(const NFmiArea &a, size_t gridSizeX, size_t gridSizeY)
{
  try
  {
    NFmiGrid *g = grid.get();
    const NFmiArea *ga = (g ? g->Area() : nullptr);

    if (!(ga && (ga->ClassId() == a.ClassId()) && (g->XNumber() == gridSizeX) &&
          (g->YNumber() == gridSizeY)))
    {
      return false;
    }

    return true;
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Return current grid if it (exists and) matches the area and
 * 	    gridsize given. Otherwise the grid is (re)created.
 */
// ----------------------------------------------------------------------

NFmiGrid *ResMgr::getGrid(const NFmiArea &a, size_t gridSizeX, size_t gridSizeY)
{
  try
  {
    if (!hasGrid(a, gridSizeX, gridSizeY))
      createGrid(a, gridSizeX, gridSizeY);

    return grid.get();
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Clone spatial reference
 */
// ----------------------------------------------------------------------

OGRSpatialReference *ResMgr::cloneCS(const OGRSpatialReference &SRS, bool isGeometrySRS)
{
  try
  {
    OGRSpatialReference *srs = SRS.Clone();

    if (srs)
    {
      spatialReferences.push_back(srs);

      if (isGeometrySRS)
        geometrySRS =  srs;
    }

    return srs;
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Clone geographic spatial reference
 */
// ----------------------------------------------------------------------

OGRSpatialReference *ResMgr::cloneGeogCS(const OGRSpatialReference &SRS, bool isGeometrySRS)
{
  try
  {
    OGRSpatialReference *srs = SRS.CloneGeogCS();

    if (srs)
    {
      spatialReferences.push_back(srs);

      if (isGeometrySRS)
        geometrySRS =  srs;
    }

    return srs;
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Get coordinate transformation
 */
// ----------------------------------------------------------------------

OGRCoordinateTransformation *ResMgr::getCoordinateTransformation(OGRSpatialReference *fromSRS,
                                                                 OGRSpatialReference *toSRS,
                                                                 bool isGeometrySRS)
{
  try
  {
    OGRCoordinateTransformation *ct = OGRCreateCoordinateTransformation(fromSRS, toSRS);

    if (ct)
    {
      // Store the target srs if output geometry will be set from it (instead of using qd's area)
      //
      if (isGeometrySRS)
      {
        if (!(geometrySRS = toSRS->Clone()))
          throw Spine::Exception(BCP,
                                 "getCoordinateTransformation: OGRSpatialReference cloning failed");
        else
          spatialReferences.push_back(geometrySRS);
      }

      transformations.push_back(ct);
    }

    return ct;
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Utility routines for testing querydata's level type
 */
// ----------------------------------------------------------------------

bool isSurfaceLevel(FmiLevelType levelType)
{
  return ((levelType == kFmiGroundSurface) || (levelType == kFmiAnyLevelType));
}

bool isPressureLevel(FmiLevelType levelType)
{
  return (levelType == kFmiPressureLevel);
}

bool isHybridLevel(FmiLevelType levelType)
{
  return (levelType == kFmiHybridLevel);
}

bool isHeightOrDepthLevel(FmiLevelType levelType)
{
  return ((levelType == kFmiHeight) || (levelType == kFmiDepth));
}

bool isHeightLevel(FmiLevelType levelType, int levelValue)
{
  return ((levelType == kFmiHeight) && (levelValue >= 0));
}

bool isDepthLevel(FmiLevelType levelType, int levelValue)
{
  return (((levelType == kFmiHeight) && (levelValue < 0)) || (levelType == kFmiDepth));
}

// ----------------------------------------------------------------------
/*!
 * \brief Utility routine for getting querydata's level type
 */
// ----------------------------------------------------------------------

static FmiLevelType getLevelTypeFromData(Engine::Querydata::Q q,
                                         const string &producer,
                                         FmiLevelType &nativeLevelType,
                                         bool &positiveLevels)
{
  try
  {
    q->firstLevel();

    // BUG: Onko bugi kun on auto mukana??
    auto levelType = nativeLevelType = q->levelType();

    if ((!isSurfaceLevel(levelType)) && (!isHybridLevel(levelType)) &&
        (!isPressureLevel(levelType)) && (!isHeightOrDepthLevel(levelType)))
    {
      throw Spine::Exception(BCP,
                             "Internal: Unrecognized level type '" +
                                 boost::lexical_cast<string>(levelType) + "' for producer '" +
                                 producer + "'");
    }

    positiveLevels = true;

    if (isHeightOrDepthLevel(levelType))
    {
      // Height level data with negative levels is returned as kFmiDepth; check the second level
      // (first might be 0).
      //
      if (!q->nextLevel())
        q->firstLevel();

      if (q->levelValue() < 0)
      {
        levelType = kFmiDepth;
        positiveLevels = false;
      }
    }

    return levelType;
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Utility routine for testing querydata's level order
 */
// ----------------------------------------------------------------------

static bool areLevelValuesInIncreasingOrder(Engine::Querydata::Q q)
{
  try
  {
    q->firstLevel();

    if (isSurfaceLevel(q->levelType()))
      return true;

    double firstLevel = q->levelValue();

    if (!q->nextLevel())
      return true;

    double secondLevel = q->levelValue();

    // Note: Height level data can have negative levels.

    return (fabs(secondLevel) > fabs(firstLevel));
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Utility routine for getting projection parameter's value from srs
 */
// ----------------------------------------------------------------------

double getProjParam(const OGRSpatialReference &srs,
                    const char *param,
                    bool ignoreErr,
                    double defaultValue)
{
  try
  {
    OGRErr err;

    double v = srs.GetNormProjParm(param, defaultValue, &err);

    if (err != OGRERR_NONE)
    {
      if (ignoreErr)
        return defaultValue;
      else
        throw Spine::Exception(BCP, string("Getting projection parameter '") + param + "' failed");
    }

    return v;
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Chunked data streaming
 */
// ----------------------------------------------------------------------

DataStreamer::DataStreamer(const Spine::HTTP::Request &req,
                           const Config &config,
                           const Producer &producer,
                           const ReqParams &reqParams)
    : Spine::HTTP::ContentStreamer(),
      itsRequest(req),
      itsCfg(config),
      itsReqParams(reqParams),
      itsProducer(producer),
      isDone(false),
      itsChunkLength(maxChunkLengthInBytes),
      itsMaxMsgChunks(maxMsgChunks),
      setMeta(true),
      itsReqGridSizeX(0),
      itsReqGridSizeY(0),
      itsProjectionChecked(false),
      itsGridMetaData(itsReqParams.producer)
{
  try
  {
    cropping.crop = cropping.cropped = false;
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

DataStreamer::~DataStreamer() {}

// ----------------------------------------------------------------------
/*!
 * \brief Determine data timestep
 *
 */
// ----------------------------------------------------------------------

void DataStreamer::checkDataTimeStep(long timeStep)
{
  try
  {
    const long minMinutesInMonth = 28 * minutesInDay;
    const long maxMinutesInMonth = (31 * minutesInDay);
    const long minMinutesInYear = 365 * minutesInDay;
    const long maxMinutesInYear = 366 * minutesInDay;

    auto q = itsQ;

    itsDataTimeStep = 0;

    if (q && q->firstTime())
    {
      NFmiTime t1 = q->validTime();
      itsDataTimeStep = (q->nextTime() ? q->validTime().DifferenceInMinutes(t1) : 60);

      q->firstTime();
    }
    else if (timeStep >= 0)
    {
      itsDataTimeStep = timeStep;

      if (itsDataTimeStep == 0)
        itsDataTimeStep = 60;
    }

    if ((itsDataTimeStep >= 60) && (itsDataTimeStep < minutesInDay) &&
        ((itsDataTimeStep % 60) == 0) && ((minutesInDay % itsDataTimeStep) == 0))
      // n hours
      ;
    else if (itsDataTimeStep == minutesInDay)
      // day
      ;
    else if ((itsDataTimeStep >= minMinutesInMonth) && (itsDataTimeStep <= maxMinutesInMonth))
      // month
      itsDataTimeStep = minutesInMonth;
    else if ((itsDataTimeStep == minMinutesInYear) || (itsDataTimeStep == maxMinutesInYear))
      // year
      itsDataTimeStep = minutesInYear;
    else if ((itsDataTimeStep > 0) && (itsDataTimeStep < minutesInDay) &&
             ((minutesInDay % itsDataTimeStep) == 0))
      // n minutes
      ;
    else
      throw Spine::Exception(BCP,
                             "Invalid data timestep (" +
                                 boost::lexical_cast<string>(itsDataTimeStep) + ") for producer '" +
                                 itsReqParams.producer + "'");
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Generate list of validtimes for the grid data to be loaded and
 * 		set origin-, start- and endtime parameters from data if unset
 * 		(they are only used when naming download file)
 *
 */
// ----------------------------------------------------------------------

const string &DataStreamer::GridMetaData::getLatestOriginTime(ptime *originTime)
{
  if (originTimeParams.empty())
      throw Spine::Exception(BCP, "No data available for producer " + producer);

  if (originTime)
    *originTime = from_iso_string(originTimeParams.rbegin()->first);

  return originTimeParams.rbegin()->first;
}

bool DataStreamer::GridMetaData::getDataTimeRange(const std::string &originTime,
                                                  ptime &firstTime,
                                                  ptime &lastTime,
                                                  long &timeStep)
{
  auto ott = originTimeTimes.find(originTime);

  if (ott == originTimeTimes.end())
    return false;

  auto t = ott->second.begin();
  firstTime = from_iso_string(*t);
  lastTime = from_iso_string(*(ott->second.rbegin()));

  if (++t != ott->second.end())
  {
    auto secondTime = from_iso_string(*t);
    timeStep = (secondTime - firstTime).minutes();
  }
  else
    timeStep = 60;

  return true;
}

using ValidTimeList = SmartMet::Engine::Querydata::ValidTimeList;

boost::shared_ptr<ValidTimeList> DataStreamer::GridMetaData::getDataTimes(const std::string &originTime)
{
  boost::shared_ptr<ValidTimeList> validTimeList(new ValidTimeList());

  auto ott = originTimeTimes.find(originTime);

  if (ott != originTimeTimes.end())
  {
    auto tit = ott->second.begin();

    for (; (tit != ott->second.end()); tit++)
      validTimeList->push_back(from_iso_string(*tit));
  }

  return validTimeList;
}

void DataStreamer::generateGridValidTimeList(Query &query, ptime &oTime, ptime &sTime, ptime &eTime)
{
  try
  {
    // Use data times if not given in request
    //
    // Note: Query with too old validtime returns no data (and an exception is thrown by
    // extractGridData()); start with first available validtime.

    string originTimeStr;
    long timeStep;

    if (oTime.is_not_a_date_time())
      originTimeStr = itsGridMetaData.getLatestOriginTime(&oTime);
    else
      originTimeStr = to_iso_string(oTime);

    itsGridMetaData.originTime = oTime;

    if (! itsGridMetaData.getDataTimeRange(originTimeStr, itsFirstDataTime, itsLastDataTime, timeStep))
      throw Spine::Exception(BCP, "No data available for producer " + itsReqParams.producer +
                             "; ot=" + originTimeStr +
                             ", ft=" + to_iso_string(itsFirstDataTime) +
                             ", lt=" + to_iso_string(itsLastDataTime) + ")"
                            );

    if (sTime.is_not_a_date_time() || (sTime < itsFirstDataTime))
      sTime = query.tOptions.startTime = itsFirstDataTime;

    if (eTime.is_not_a_date_time())
      eTime = query.tOptions.endTime = itsLastDataTime;

    // Check and store data timestep

    checkDataTimeStep(timeStep);

    // Generate list of validtimes for the data to be loaded.
    //
    // Note: Mode must be changed from TimeSteps to DataTimes if timestep was not given (we don't
    // use the default).

    bool hasTimeStep = (query.tOptions.timeStep && (*query.tOptions.timeStep > 0));

    if ((query.tOptions.mode == Spine::TimeSeriesGeneratorOptions::TimeSteps) && (!hasTimeStep))
      query.tOptions.mode = Spine::TimeSeriesGeneratorOptions::DataTimes;

    if ((query.tOptions.mode == Spine::TimeSeriesGeneratorOptions::DataTimes) ||
        query.tOptions.startTimeData || query.tOptions.endTimeData)
    {
      query.tOptions.setDataTimes(itsGridMetaData.getDataTimes(originTimeStr), false);
    }

    auto tz = itsGeoEngine->getTimeZones().time_zone_from_string(query.timeZone);
    itsDataTimes = Spine::TimeSeriesGenerator::generate(query.tOptions, tz);

    if (itsDataTimes.empty())
      throw Spine::Exception(BCP, "No valid times in the requested time period")
          .disableStackTrace();
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Generate list of validtimes for the data to be loaded and
 * 		set origin-, start- and endtime parameters from data if unset
 * 		(they are only used when naming download file)
 *
 */
// ----------------------------------------------------------------------

void DataStreamer::generateValidTimeList(
    const Engine::Querydata::Q &q, Query &query, ptime &oTime, ptime &sTime, ptime &eTime)
{
  try
  {
    // Check and store data timestep

    itsQ = q;
    itsQ->firstTime();
    itsFirstDataTime = itsQ->validTime();

    checkDataTimeStep();

    // Use data times if not given in request
    //
    // Note: Query with too old validtime returns no data (and an exception is thrown by
    // extractData()); start with first available validtime.

    if (oTime.is_not_a_date_time())
      oTime = itsQ->originTime();

    if (sTime.is_not_a_date_time() || (sTime < itsQ->validTime()))
      sTime = query.tOptions.startTime = itsQ->validTime();

    itsQ->lastTime();
    itsLastDataTime = itsQ->validTime();
    itsQ->firstTime();

    if (eTime.is_not_a_date_time())
      eTime = query.tOptions.endTime = itsLastDataTime;

    // Generate list of validtimes for the data to be loaded.
    //
    // Note: Mode must be changed from TimeSteps to DataTimes if timestep was not given (we don't
    // use
    // the default).

    bool hasTimeStep = (query.tOptions.timeStep && (*query.tOptions.timeStep > 0));

    if ((query.tOptions.mode == Spine::TimeSeriesGeneratorOptions::TimeSteps) && (!hasTimeStep))
      query.tOptions.mode = Spine::TimeSeriesGeneratorOptions::DataTimes;

    if ((query.tOptions.mode == Spine::TimeSeriesGeneratorOptions::DataTimes) ||
        query.tOptions.startTimeData || query.tOptions.endTimeData)
    {
      query.tOptions.setDataTimes(q->validTimes(), q->isClimatology());
    }

    auto tz = itsGeoEngine->getTimeZones().time_zone_from_string(query.timeZone);
    itsDataTimes = Spine::TimeSeriesGenerator::generate(query.tOptions, tz);

    if (itsDataTimes.empty())
      throw Spine::Exception(BCP, "No valid times in the requested time period")
          .disableStackTrace();
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Set levels from request parameter(s) or from data if none was given.
 *		Check querydata(s) level types and time step.
 *
 */
// ----------------------------------------------------------------------

void DataStreamer::setGridLevels(const Query &query, const Producer &producer, uint levelScale)
{
  try
  {
    Query::Levels allLevels;

    // Fetching level/height range ?

    itsLevelRng = ((!isSurfaceLevel(levelType)) &&
                   ((itsReqParams.minLevel >= 0) || (itsReqParams.maxLevel > 0)));
    itsHeightRng = ((!isSurfaceLevel(levelType)) &&
                    ((itsReqParams.minHeight >= 0) || (itsReqParams.maxHeight > 0)));

    bool noLevelsGiven = (query.levels.begin() == query.levels.end());
    auto queryLevels = noLevelsGiven ? producer.gridDefaultLevels : query.levels;

    Query::Levels &levels =
        ((queryLevels.begin() == queryLevels.end()) && ((!itsLevelRng) && (!itsHeightRng)))
            ? itsDataLevels
            : allLevels;

    levels.insert(itsGridMetaData.levels.begin(), itsGridMetaData.levels.end());

    itsRisingLevels = true;

    if (isSurfaceLevel(levelType))
      // Surface data; set exactly one level (ignoring user input), value does not matter
      //
      itsDataLevels.insert(0);
    else
    {
      // If no levels/heights were given, using all data levels

      if (queryLevels.begin() == queryLevels.end())
      {
        if (itsLevelRng || itsHeightRng)
          for (int l = itsReqParams.minLevel; (l <= itsReqParams.maxLevel); l++)
            itsDataLevels.insert(l);
      }
      else
        for (auto it = queryLevels.begin(); (it != queryLevels.end()); it++)
          itsDataLevels.insert(*it * levelScale);
    }
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Set levels from request parameter(s) or from data if none was given.
 *		Check querydata(s) level types and time step.
 *
 */
// ----------------------------------------------------------------------

void DataStreamer::setLevels(const Query &query)
{
  try
  {
    Query::Levels allLevels;

    auto q = itsQ;

    // Level type

    levelType = getLevelTypeFromData(q, itsReqParams.producer, nativeLevelType, itsPositiveLevels);

    // Fetching level/height range ?

    itsLevelRng = ((!isSurfaceLevel(levelType)) &&
                   ((itsReqParams.minLevel >= 0) || (itsReqParams.maxLevel > 0)));
    itsHeightRng = ((!isSurfaceLevel(levelType)) &&
                    ((itsReqParams.minHeight >= 0) || (itsReqParams.maxHeight > 0)));

    Query::Levels &levels =
        ((query.levels.begin() == query.levels.end()) && ((!itsLevelRng) && (!itsHeightRng)))
            ? itsDataLevels
            : allLevels;

    for (q->resetLevel(); q->nextLevel();)
      // Note: The level values are stored unsigned; negative values are used when necessary
      // when getting the data (when querying height level data with negative levels).
      //
      levels.insert(boost::numeric_cast<int>(abs(q->levelValue())));

    itsRisingLevels = areLevelValuesInIncreasingOrder(q);

    if (isSurfaceLevel(levelType))
      // Surface data; set exactly one level (ignoring user input), value does not matter
      //
      itsDataLevels.insert(0);
    else
    {
      // If no levels/heights were given, using all querydata levels

      if (query.levels.begin() == query.levels.end())
      {
        if (itsLevelRng || itsHeightRng)
          for (int l = itsReqParams.minLevel; (l <= itsReqParams.maxLevel); l++)
            itsDataLevels.insert(l);
      }
      else
        itsDataLevels = query.levels;
    }
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Store unique data parameter names.
 *
 */
// ----------------------------------------------------------------------

void DataStreamer::setParams(const Spine::OptionParsers::ParameterList &params,
                             const Scaling &scaling)
{
  try
  {
    std::unordered_set<unsigned long> paramIds;
    auto scalingIterator = scaling.begin();
    bool hasScaling = (scalingIterator != scaling.end());

    for (auto paramIterator = params.begin(); (paramIterator != params.end()); paramIterator++)
    {
      if (paramIds.find(paramIterator->number()) == paramIds.end())
      {
        itsDataParams.push_back(*paramIterator);
        if (hasScaling)
          itsValScaling.push_back(*scalingIterator);

        paramIds.insert(paramIterator->number());
      }

      if (hasScaling)
        scalingIterator++;
    }
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Check if (any) requested grid data is available.
 *        Collects available levels and validtimes.
 *
 */
// ----------------------------------------------------------------------

bool DataStreamer::hasRequestedGridData(const Producer &producer,
                                        Query &query,
                                        ptime &oTime,
                                        ptime &sTime,
                                        ptime &eTime)
{
  try
  {
    // Check if any of the requested parameters and levels exist or is interpolatable.

    Engine::Grid::Times validTimes;
    size_t nMissingParam = 0;

    BOOST_FOREACH (auto const &param, itsDataParams)
    {
      SmartMet::Engine::Grid::ParameterDetails_vec paramDetails;
      itsGridEngine->getParameterDetails(itsReqParams.producer, param.name(), paramDetails);

      if (paramDetails.size() > 0)
      {
        // Use first detail for newbase producer+parameter

//      if (paramDetails.size() > 1)
//        throw Spine::Exception(BCP, "Multiple mappings for parameter " + param.name());

        string paramKey = itsReqParams.producer + ";" + param.name();

        if (strcasecmp(paramDetails[0].mProducerName.c_str(),paramKey.c_str()) != 0)
        {
          set<T::ParamLevel> paramLevels;
          bool hasParam = false;

          itsGridEngine->mapParameterDetails(paramDetails);

          BOOST_FOREACH (auto const &paramMapping, paramDetails[0].mMappings)
          {
            if (
                (paramMapping.mMapping.mParameterLevelIdType == T::ParamLevelIdTypeValue::FMI) &&
                (
                 (paramMapping.mMapping.mParameterLevelId == GridMetaData::GridFMILevelTypeGround) ||
                 (paramMapping.mMapping.mParameterLevelId == GridMetaData::GridFMILevelTypePressure) ||
                 (paramMapping.mMapping.mParameterLevelId == GridMetaData::GridFMILevelTypeHybrid) ||
                 (paramMapping.mMapping.mParameterLevelId == GridMetaData::GridFMILevelTypeHeight) ||
                 (paramMapping.mMapping.mParameterLevelId == GridMetaData::GridFMILevelTypeDepth)
                )
               )
            {
              if (itsGridMetaData.paramLevelId != GridMetaData::GridFMILevelTypeNone)
              {
                if (paramMapping.mMapping.mParameterLevelId != itsGridMetaData.paramLevelId)
                  throw Spine::Exception(BCP, "Multiple leveltypes for parameter " + param.name());
              }
              else
              {
                itsGridMetaData.paramLevelId = paramMapping.mMapping.mParameterLevelId;

                if (itsGridMetaData.paramLevelId == GridMetaData::GridFMILevelTypeGround)
                  levelType = kFmiGroundSurface;
                else if (itsGridMetaData.paramLevelId == GridMetaData::GridFMILevelTypePressure)
                  levelType = kFmiPressureLevel;
                else if (itsGridMetaData.paramLevelId == GridMetaData::GridFMILevelTypeHybrid)
                  levelType = kFmiHybridLevel;
                else if  (itsGridMetaData.paramLevelId == GridMetaData::GridFMILevelTypeHeight)
                  levelType = kFmiHeight;
                else
                  levelType = kFmiDepth;
              }

              auto level = paramLevels.insert(paramMapping.mMapping.mParameterLevel);
              if (!level.second)
                throw Spine::Exception(BCP, "Duplicate level value for parameter " + param.name());

              // Collect origintimes and available parameters and times for each of them

              for (auto const & dataTimes : paramMapping.mTimes)
              {
                auto otp = itsGridMetaData.originTimeParams.insert(make_pair(dataTimes.first, set<string>()));
                otp.first->second.insert(param.name());

                auto ott = itsGridMetaData.originTimeTimes.insert(make_pair(dataTimes.first, set<string>()));
                ott.first->second.insert(dataTimes.second.begin(), dataTimes.second.end());
              }

              paramKey = paramMapping.mMapping.mParameterName + ":" + paramMapping.mMapping.mProducerName;
              hasParam = true;
            }
          }

          if (hasParam)
          {
            itsGridMetaData.paramKeys.insert(make_pair(param.name(), paramKey));

            if (paramLevels.size() > 0)
              itsGridMetaData.levels.insert(paramLevels.begin(), paramLevels.end());

            continue;
          }
        }
      }

      // Count leading missing parameters and erase their scaling information

      if (itsGridMetaData.paramLevelId == GridMetaData::GridFMILevelTypeNone)
      {
        nMissingParam++;

        if (!itsValScaling.empty())
          itsValScaling.pop_front();
        else
          throw Spine::Exception(BCP, "Internal error in skipping missing parameters");
      }
    }

    if (itsGridMetaData.paramLevelId == GridMetaData::GridFMILevelTypeNone)
      return false;

    // Erase leading missing parameters. The first parameter in 'itsDataParams' must be the first
    // valid/existent parameter after the initialization phase.
    // At the end of initialization parameter iterator is set to the start of 'itsDataParams';
    // data chunking starts with it

    if (nMissingParam > 0)
      itsDataParams.erase(itsDataParams.begin(), itsDataParams.begin() + nMissingParam);

    // Generate list of validtimes for the data to be loaded.

    generateGridValidTimeList(query, oTime, sTime, eTime);

    // Set request levels. Scale default/configured pressure levels to hPa

    setGridLevels(query, producer, (levelType == kFmiPressureLevel) ? 100 : 1);

    // Check data is available for at least one requested level

    Query::Levels::const_iterator it = itsDataLevels.begin();
    bool exactLevel;

    for (; (it != itsDataLevels.end()); it++)
    {
      int level = *it;

      if (isGridLevelAvailable(level, exactLevel))
        return true;
    }

    return false;
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Check if (any) requested data is available.
 *
 */
// ----------------------------------------------------------------------

bool DataStreamer::hasRequestedData(const Producer &producer, Query &query, ptime &originTime, ptime &startTime, ptime &endTime)
{
  try
  {
    if (itsReqParams.source == "grid")
      return hasRequestedGridData(producer, query, originTime, startTime, endTime);

    auto q = itsQ;
    bool hasData = false;

    // Get grid's origo

    if (!q->isGrid())
      throw Spine::Exception(BCP, "Nongrid data for producer + '" + itsReqParams.producer + "'");

    const NFmiGrid &grid = q->grid();

    itsGridOrigo = grid.Origo();

    // Check if any of the requested parameters exists.

    size_t nMissingParam = 0;
    BOOST_FOREACH (auto const &param, itsDataParams)
    {
      if (q->param(param.number()))
      {
        hasData = true;
        break;
      }

      // Count leading missing parameters and erase their scaling information

      nMissingParam++;

      if (!itsValScaling.empty())
        itsValScaling.pop_front();
      else
        throw Spine::Exception(BCP, "Internal error in skipping missing parameters");
    }

    if (!hasData)
      return false;

    // Erase leading missing parameters. The first parameter in 'itsDataParams' must be the first
    // valid/existent parameter after the initialization phase.
    // At the end of initialization parameter iterator is set to the start of 'itsDataParams', which
    // must be the same as itsQ's current parameter
    // set in loop above; data chunking starts with it

    if (nMissingParam > 0)
      itsDataParams.erase(itsDataParams.begin(), itsDataParams.begin() + nMissingParam);

    // Check if any of the requested levels exist or is interpolatable.

    bool exactLevel = (itsLevelRng || isSurfaceLevel(levelType));

    BOOST_FOREACH (auto const &queryLevel, itsDataLevels)
    {
      // Loop over the available data levels. Level interpolation is possible for pressure data
      // only.
      //
      // Note: Height level data can have negative levels.

      int level;
      bool first = true;

      for (q->resetLevel(); q->nextLevel(); first = false)
      {
        level = abs(boost::numeric_cast<int>(q->levelValue()));

        if (itsLevelRng)
        {
          if ((itsReqParams.maxLevel > 0) && (level > itsReqParams.maxLevel))
            if (itsRisingLevels)
              break;
            else
              continue;
          else if ((itsReqParams.minLevel >= 0) && (level < itsReqParams.minLevel))
          {
            if (itsRisingLevels)
              continue;
            else
              break;
          }
        }

        //
        // Height range query currently not implemented.
        //
        //			else if (heightrng) {
        //			}
        else if (!isSurfaceLevel(levelType))
        {
          exactLevel = (level == queryLevel);

          if (!exactLevel)
          {
            if (queryLevel > level)
              if (itsRisingLevels)
                continue;
              else
              {
                if (first || (!isPressureLevel(levelType)) || (!producer.verticalInterpolation))
                  break;
              }
            else if (itsRisingLevels)
            {
              if (first || (!isPressureLevel(levelType)) || (!producer.verticalInterpolation))
                break;
            }
            else
              continue;
          }
        }

        // Some data is available.

        return true;
      }  // for available levels
    }    // for queried levels

    return false;
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Get (regular) latlon bbox.
 *
 */
// ----------------------------------------------------------------------

void DataStreamer::getRegLLBBox(Engine::Querydata::Q q)
{
  try
  {
    auto shared_latlons = q->latLonCache();
    const auto &llc = *shared_latlons;

    double blLon = 0.0, blLat = 0.0, trLon = 0.0, trLat = 0.0;
    size_t gridSizeX = q->grid().XNumber(), gridSizeY = q->grid().YNumber();

    // Loop all columns of first and last row and first and last columns of other rows.

    for (size_t y = 1, n = 0, dx = (gridSizeX - 1); (y <= gridSizeY); y++, n++)
      for (size_t x = 1; (x <= gridSizeX);)
      {
        const NFmiPoint &p = llc[n];
        auto px = p.X(), py = p.Y();

        if (n == 0)
        {
          blLon = trLon = px;
          blLat = trLat = py;
        }
        else
        {
          if (px < blLon)
            blLon = px;
          else if (px > trLon)
            trLon = px;

          if (py < blLat)
            blLat = py;
          else if (py > trLat)
            trLat = py;
        }

        size_t dn = (((y == 1) || (y == gridSizeY)) ? 1 : dx);

        x += dn;
        if (x <= gridSizeX)
          n += dn;
      }

    itsRegBoundingBox = BBoxCorners();

    (*itsRegBoundingBox).bottomLeft = NFmiPoint(blLon, blLat);
    (*itsRegBoundingBox).topRight = NFmiPoint(trLon, trLat);
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Get (regular) latlon bbox string.
 *
 */
// ----------------------------------------------------------------------

string DataStreamer::getRegLLBBoxStr(Engine::Querydata::Q q)
{
  try
  {
    if (!itsRegBoundingBox)
      getRegLLBBox(q);

    ostringstream os;
    os << fixed << setprecision(8) << (*itsRegBoundingBox).bottomLeft.X() << ","
       << (*itsRegBoundingBox).bottomLeft.Y() << "," << (*itsRegBoundingBox).topRight.X() << ","
       << (*itsRegBoundingBox).topRight.Y();

    return os.str();
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Get latlon bbox.
 *
 */
// ----------------------------------------------------------------------

void DataStreamer::getLLBBox(Engine::Querydata::Q q)
{
  try
  {
    if (!itsRegBoundingBox)
      getRegLLBBox(q);

    itsBoundingBox.bottomLeft = (*itsRegBoundingBox).bottomLeft;
    itsBoundingBox.topRight = (*itsRegBoundingBox).topRight;
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Calculate stepped grid xy size and adjust cropping with the step.
 *
 * 		Note: Stepping is not applicable to querydata output.
 *
 */
// ----------------------------------------------------------------------

void DataStreamer::setSteppedGridSize()
{
  try
  {
    size_t xCnt = (cropping.cropped ? cropping.gridSizeX : itsReqGridSizeX),
           yCnt = (cropping.cropped ? cropping.gridSizeY : itsReqGridSizeY);
    size_t xStep = (itsReqParams.gridStepXY ? (*(itsReqParams.gridStepXY))[0].first : 1),
           yStep = (itsReqParams.gridStepXY ? (*(itsReqParams.gridStepXY))[0].second : 1);

    itsNX = xCnt / xStep;
    itsNY = yCnt / yStep;

    if (xStep > 1)
    {
      if (xCnt % xStep)
        itsNX++;

      if (cropping.cropped)
      {
        cropping.topRightX = cropping.bottomLeftX + ((itsNX - 1) * xStep);
        cropping.gridSizeX = ((cropping.topRightX - cropping.bottomLeftX) + 1);
      }
    }

    if (yStep > 1)
    {
      if (yCnt % yStep)
        itsNY++;

      if (cropping.cropped)
      {
        cropping.topRightY = cropping.bottomLeftY + ((itsNY - 1) * yStep);
        cropping.gridSizeY = ((cropping.topRightY - cropping.bottomLeftY) + 1);
      }
    }

    if ((itsNX < 2) || (itsNY < 2))
      throw Spine::Exception(BCP, "Minimum gridsize is 2x2, adjust bbox and/or gridstep");
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Set requested grid size. Returns true is using native grid size.
 *
 */
// ----------------------------------------------------------------------

bool DataStreamer::setRequestedGridSize(const NFmiArea &area,
                                        size_t nativeGridSizeX,
                                        size_t nativeGridSizeY)
{
  try
  {
    size_t gridSizeX, gridSizeY;

    if (itsReqParams.gridSizeXY)
    {
      gridSizeX = boost::numeric_cast<size_t>((*itsReqParams.gridSizeXY)[0].first);
      gridSizeY = boost::numeric_cast<size_t>((*itsReqParams.gridSizeXY)[0].second);
    }
    else if (itsReqParams.gridResolutionXY)
    {
      gridSizeX = boost::numeric_cast<size_t>(
          fabs(ceil(area.WorldXYWidth() / ((*itsReqParams.gridResolutionXY)[0].first * 1000))));
      gridSizeY = boost::numeric_cast<size_t>(
          fabs(ceil(area.WorldXYHeight() / ((*itsReqParams.gridResolutionXY)[0].second * 1000))));

      if ((gridSizeX <= 1) || (gridSizeY <= 1))
        throw Spine::Exception(BCP,
                               "Invalid gridsize for producer '" + itsReqParams.producer + "'");

      // Must use constant grid size for querydata output; set calculated absolute gridsize

      if (itsReqParams.outputFormat == QD)
      {
        ostringstream os;

        os << gridSizeX << "," << gridSizeY;

        itsReqParams.gridSize = os.str();
        itsReqParams.gridSizeXY =
            nPairsOfValues<unsigned int>(itsReqParams.gridSize, "gridsize", 1);

        itsReqParams.gridResolution.clear();
        itsReqParams.gridResolutionXY = GridResolution();
      }
    }
    else
    {
      gridSizeX = nativeGridSizeX;
      gridSizeY = nativeGridSizeY;
    }

    itsReqGridSizeX = gridSizeX;
    itsReqGridSizeY = gridSizeY;

    // Take stepping (gridstep=dx,dy) into account

    setSteppedGridSize();

    return ((itsReqGridSizeX == nativeGridSizeX) && (itsReqGridSizeY == nativeGridSizeY));
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Get projection string for gridcenter bounding.
 *
 */
// ----------------------------------------------------------------------

std::string DataStreamer::getGridCenterBBoxStr(bool useNativeProj, const NFmiGrid &grid) const
{
  try
  {
    ostringstream os;

    os << fixed << setprecision(8) << (*itsReqParams.gridCenterLL)[0].first << ","
       << (*itsReqParams.gridCenterLL)[0].second << ",1|"
       << (*itsReqParams.gridCenterLL)[1].first << "," << (*itsReqParams.gridCenterLL)[1].second;

    return os.str();
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Set native grid resolution.
 *
 */
// ----------------------------------------------------------------------

void DataStreamer::setNativeGridResolution(const NFmiArea &nativeArea,
                                           size_t nativeGridSizeX,
                                           size_t nativeGridSizeY)
{
  try
  {
    ostringstream os;

    os << fixed << setprecision(8) << (nativeArea.WorldXYWidth() / (nativeGridSizeX - 1) / 1000.0)
       << "," << (nativeArea.WorldXYHeight() / (nativeGridSizeY - 1) / 1000.0);

    itsReqParams.gridResolution = os.str();
    itsReqParams.gridResolutionXY =
        nPairsOfValues<double>(itsReqParams.gridResolution, "gridresolution", 1);
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Calculate cropped grid xy area.
 *
 */
// ----------------------------------------------------------------------

void DataStreamer::setCropping(const NFmiGrid &grid)
{
  try
  {
    // Note: With rotlatlon projection bbox corners are now taken as regular latlons

    string bboxStr(itsReqParams.gridCenterLL ? getGridCenterBBoxStr(itsUseNativeProj, grid)
                                             : itsReqParams.origBBox);

    if (itsReqParams.gridCenterLL)
    {
      // NFmiFastQueryInfo does not support reading native grid points within bounded area;
      // creating temporary projection to get bboxrect to crop the native area.
      //
      // Rotated latlon area is created using 'invrotlatlon' projection to handle the given
      // bounding as rotated coordinates.
      //
      string projection =
          boost::algorithm::replace_all_copy(itsReqParams.projection, "rotlatlon", "invrotlatlon") +
          "|" + bboxStr;

      boost::shared_ptr<NFmiArea> a = NFmiAreaFactory::Create(projection);
      NFmiArea *area = a.get();
      if (!area)
        throw Spine::Exception(BCP, "Could not create projection '" + projection + "'");

      NFmiPoint bl(area->BottomLeftLatLon());
      NFmiPoint tr(area->TopRightLatLon());

      ostringstream os;
      os << fixed << setprecision(8) << bl.X() << "," << bl.Y() << "," << tr.X() << "," << tr.Y();

      bboxStr = os.str();
    }

    itsReqParams.bboxRect = nPairsOfValues<double>(bboxStr, "bboxstr", 2);

    NFmiPoint bl((*itsReqParams.bboxRect)[BOTTOMLEFT].first,
                 (*itsReqParams.bboxRect)[BOTTOMLEFT].second);
    NFmiPoint tr((*itsReqParams.bboxRect)[TOPRIGHT].first,
                 (*itsReqParams.bboxRect)[TOPRIGHT].second);

    NFmiPoint xy1 = grid.LatLonToGrid(bl);
    NFmiPoint xy2 = grid.LatLonToGrid(tr);

    cropping.bottomLeftX = boost::numeric_cast<int>(floor(xy1.X()));
    cropping.bottomLeftY = boost::numeric_cast<int>(floor(xy1.Y()));
    cropping.topRightX = boost::numeric_cast<int>(ceil(xy2.X()));
    cropping.topRightY = boost::numeric_cast<int>(ceil(xy2.Y()));

    if (cropping.bottomLeftX < 0)
      cropping.bottomLeftX = 0;
    if (cropping.bottomLeftY < 0)
      cropping.bottomLeftY = 0;
    if (cropping.topRightX >= (int)grid.XNumber())
      cropping.topRightX = grid.XNumber() - 1;
    if (cropping.topRightY >= (int)grid.YNumber())
      cropping.topRightY = grid.YNumber() - 1;

    if ((cropping.bottomLeftX >= cropping.topRightX) ||
        (cropping.bottomLeftY >= cropping.topRightY))
      throw Spine::Exception(BCP, "Bounding box does not intersect the grid").disableStackTrace();

    cropping.gridSizeX = ((cropping.topRightX - cropping.bottomLeftX) + 1);
    cropping.gridSizeY = ((cropping.topRightY - cropping.bottomLeftY) + 1);

    cropping.crop = cropping.cropped = true;

    // Take stepping (gridstep=dx,dy) into account

    setSteppedGridSize();

    bl = grid.GridToLatLon(NFmiPoint(cropping.bottomLeftX, cropping.bottomLeftY));
    tr = grid.GridToLatLon(NFmiPoint(cropping.topRightX, cropping.topRightY));

    ostringstream os;
    os << fixed << setprecision(8) << bl.X() << "," << bl.Y() << "," << tr.X() << "," << tr.Y();

    itsReqParams.bbox = os.str();
    itsReqParams.bboxRect = nPairsOfValues<double>(itsReqParams.bbox, "bbox", 2);
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Get projection type for a srs loaded using epsg code
 */
// ----------------------------------------------------------------------

static AreaClassId getProjectionType(const ReqParams &itsReqParams, const char *projection)
{
  try
  {
    typedef struct
    {
      const char *projection;
      AreaClassId areaClassId;
      bool grib1;
      bool grib2;
      bool netcdf;
    } SupportedProjection;

    static SupportedProjection projections[] = {
        {SRS_PT_EQUIRECTANGULAR, A_LatLon, true, true, true},
        //		{ ?,							A_RotLatLon,
        // true,	true,	true	},
        {SRS_PT_POLAR_STEREOGRAPHIC, A_PolarStereoGraphic, true, true, true},
        //		{ SRS_PT_MERCATOR_1SP,			A_Mercator,
        // true,	true,	true	},
        //		{ SRS_PT_MERCATOR_2SP,			A_Mercator,
        // true,	true,	true	},
        {nullptr, A_Native, false, false, false}};

    if (!projection)
      throw Spine::Exception(BCP, "Projection name is undefined");

    string proj(projection);

    for (SupportedProjection *p = projections; p->projection; p++)
      if (proj.find(p->projection) == 0)
      {
        if (((itsReqParams.outputFormat == Grib1) && p->grib1) ||
            ((itsReqParams.outputFormat == Grib2) && p->grib2) ||
            ((itsReqParams.outputFormat == NetCdf) && p->netcdf))
          return p->areaClassId;

        break;
      }

    throw Spine::Exception(BCP, "Unsupported projection '" + proj + "'");
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Coordinate transformation from querydata 'datum'/projection to
 * 		requested projection with or without datum shift to wgs84
 *
 *		The transformed coordinates are stored to 'srcLatLons' -member;
 *		they are used to get the data from the source grid.
 *
 *		Target projection's bounding box is set to 'itsBoundingBox' -member.
 *
 *		Note: 'resMgr' member is the sole owner and thus responsible for
 *		releasing *ALL* objects created/returned by calling it's methods.
 *
 */
// ----------------------------------------------------------------------

void DataStreamer::setTransformedCoordinates(Engine::Querydata::Q q, const NFmiArea *area)
{
  try
  {
    OGRSpatialReference qdProjectedSrs, *qdLLSrsPtr;
    OGRErr err;

    // qd projected (or latlon/geographic) cs

    if ((err = qdProjectedSrs.SetFromUserInput(area->WKT().c_str())) != OGRERR_NONE)
      throw Spine::Exception(BCP,
                             "transform: srs.Set(WKT) error " + boost::lexical_cast<string>(err));

    // qd geographic cs

    qdLLSrsPtr = itsResMgr.cloneGeogCS(qdProjectedSrs);
    if (!qdLLSrsPtr)
      throw Spine::Exception(BCP, "transform: qdsrs.cloneGeogCS() failed");

    // Helmert transformation parameters for wgs84 output

    if (Datum::isDatumShiftToWGS84(itsReqParams.datumShift))
    {
      double htp[7];
      Datum::getHelmertTransformationParameters(itsReqParams.datumShift, area, qdProjectedSrs, htp);

      qdLLSrsPtr->SetTOWGS84(htp[0], htp[1], htp[2], htp[3], htp[4], htp[5], htp[6]);
    }

    OGRSpatialReference wgs84ProjectedSrs, *wgs84PrSrsPtr = &wgs84ProjectedSrs,
                                           *wgs84LLSrsPtr = &wgs84ProjectedSrs;
    bool qdProjLL =
             ((area->AreaStr().find("rotlatlon") == 0) || (area->AreaStr().find("latlon") == 0)),
         wgs84ProjLL;

    if (itsReqParams.projType == P_Epsg)
    {
      // Epsg projection
      //
      if ((err = wgs84PrSrsPtr->importFromEPSG(itsReqParams.epsgCode)) != OGRERR_NONE)
        throw Spine::Exception(BCP,
                               "transform: srs.importFromEPSG(" +
                                   boost::lexical_cast<string>(itsReqParams.epsgCode) + ") error " +
                                   boost::lexical_cast<string>(err));
    }
    else if ((!Datum::isDatumShiftToWGS84(itsReqParams.datumShift)) ||
             ((itsReqParams.projType != P_LatLon) && (itsReqParams.projType != P_RotLatLon) &&
              ((itsReqParams.projType != P_Native) || (!qdProjLL))))
    {
      // qd projection
      //
      if (!(wgs84PrSrsPtr = (Datum::isDatumShiftToWGS84(itsReqParams.datumShift)
                                 ? itsResMgr.cloneCS(qdProjectedSrs)
                                 : itsResMgr.cloneGeogCS(qdProjectedSrs))))
        throw Spine::Exception(BCP, "transform: qdsrs.clone() failed");
    }

    // If selected set wgs84 geographic output cs

    if (Datum::isDatumShiftToWGS84(itsReqParams.datumShift))
      if ((err = wgs84PrSrsPtr->SetWellKnownGeogCS("WGS84")) != OGRERR_NONE)
        throw Spine::Exception(
            BCP, "transform: srs.Set(WGS84) error " + boost::lexical_cast<string>(err));

    // If projected output cs, get geographic output cs

    if (!(wgs84ProjLL = (!(wgs84PrSrsPtr->IsProjected()))))
    {
      if (itsReqParams.projType == P_Epsg)
        itsReqParams.areaClassId =
            getProjectionType(itsReqParams, wgs84PrSrsPtr->GetAttrValue("PROJECTION"));

      if (!(wgs84LLSrsPtr = itsResMgr.cloneGeogCS(*wgs84PrSrsPtr)))
        throw Spine::Exception(BCP, "transform: wgs84.cloneGeogCS() failed");
    }
    else if (itsReqParams.projType == P_Epsg)
    {
      // Output not projected, getting the data using native (projected or latlon) qd projection
      // (setting latlon geometry does not reference the qd native area object).
      //
      // If data is projected, get latlon bounding box.
      //
      itsReqParams.areaClassId = A_LatLon;

      if (!qdProjLL)
        getLLBBox(q);
    }

    // Transform qd grid bottom left and top right latlons to output cs projected coordinates.
    //
    // Note: When getting the transformation object true is passed as last parameter if the target
    // is
    // projected epsg srs;
    // the target srs is cloned and stored by resMgr and the srs is used later when setting output
    // geometry.

    OGRCoordinateTransformation *qdLL2Wgs84Prct = itsResMgr.getCoordinateTransformation(
        qdLLSrsPtr, wgs84PrSrsPtr, ((itsReqParams.projType == P_Epsg) && (!wgs84ProjLL)));
    if (!qdLL2Wgs84Prct)
      throw Spine::Exception(BCP, "transform: OGRCreateCoordinateTransformation(qd,wgs84) failed");

    typedef NFmiDataMatrix<float>::size_type sz_t;
    double xc, yc;

    if ((!qdProjLL) || (!wgs84ProjLL))
    {
      for (sz_t i = 0; i < 2; i++)
      {
        NFmiPoint &p = (i ? itsBoundingBox.topRight : itsBoundingBox.bottomLeft);

        xc = p.X();
        yc = p.Y();

        if (!(qdLL2Wgs84Prct->Transform(1, &xc, &yc)))
          throw Spine::Exception(BCP, "transform: Transform(qd,wgs84) failed");

        p = NFmiPoint(xc, yc);
      }
    }

    NFmiPoint bl = itsBoundingBox.bottomLeft;
    NFmiPoint tr = itsBoundingBox.topRight;

    // Calculate/transform output cs grid cell projected (or latlon) coordinates to qd latlons.
    //
    // Get transformations from projected (or latlon) target cs to qd latlon cs and from projected
    // target cs to geographic target cs (to get target grid corner latlons).

    OGRCoordinateTransformation *wgs84Pr2QDLLct =
        itsResMgr.getCoordinateTransformation(wgs84PrSrsPtr, qdLLSrsPtr);
    if (!wgs84Pr2QDLLct)
      throw Spine::Exception(BCP, "transform: OGRCreateCoordinateTransformation(wgs84,qd) failed");

    OGRCoordinateTransformation *wgs84Pr2LLct = nullptr;
    if ((!wgs84ProjLL) &&
        (!(wgs84Pr2LLct = itsResMgr.getCoordinateTransformation(wgs84PrSrsPtr, wgs84LLSrsPtr))))
      throw Spine::Exception(BCP,
                             "transform: OGRCreateCoordinateTransformation(wgs84,wgs84) failed");

    srcLatLons.Resize(itsReqGridSizeX, itsReqGridSizeY);
    const sz_t xs = srcLatLons.NX(), ys = srcLatLons.NY();
    const sz_t xN = xs - 1, yN = ys - 1;
    sz_t x, y;

    if (itsReqParams.outputFormat == NetCdf)
    {
      tgtLatLons.Resize(itsReqGridSizeX, itsReqGridSizeY);
      tgtWorldXYs.Resize(itsReqGridSizeX, itsReqGridSizeY);
    }

    itsDX = ((tr.X() - bl.X()) / xN);
    itsDY = ((tr.Y() - bl.Y()) / yN);

    for (y = 0, yc = bl.Y(); y < ys; y++, yc += itsDY)
    {
      xc = bl.X();

      if (qdProjLL && wgs84ProjLL &&
          (((y == 0) && (yc <= -89.999)) || ((y == yN) && (yc >= 89.999))))
      {
        for (x = 0; x < xs; x++, xc += itsDX)
          srcLatLons[x][y] = NFmiPoint(xc, ((y == 0) ? -90.0 : 90.0));

        continue;
      }

      for (x = 0; x < xs; x++, xc += itsDX)
      {
        double txc = xc;
        double tyc = yc;

        if (!(wgs84Pr2QDLLct->Transform(1, &txc, &tyc)))
          throw Spine::Exception(BCP, "transform: Transform(wgs84,qd) failed");

        srcLatLons[x][y] = NFmiPoint(txc, tyc);

        if (!wgs84ProjLL)
        {
          if (((y == 0) && (x == 0)) || ((y == yN) && (x == xN)))
          {
            // Output cs grid bottom left and top right projected coordinates to latlons
            //
            txc = xc;
            tyc = yc;

            if (!(wgs84Pr2LLct->Transform(1, &txc, &tyc)))
              throw Spine::Exception(BCP, "transform: Transform(wgs84,wgs84) failed");

            if (y == 0)
              itsBoundingBox.bottomLeft = NFmiPoint(txc, tyc);
            else
              itsBoundingBox.topRight = NFmiPoint(txc, tyc);
          }

          if (itsReqParams.outputFormat == NetCdf)
          {
            // Output cs world xy coordinates for netcdf output
            //
            tgtWorldXYs[x][y] = NFmiPoint(xc, yc);
          }
        }

        if (itsReqParams.outputFormat == NetCdf)
        {
          // Output cs grid (projected coordinates to) latlons for netcdf output
          //
          txc = xc;
          tyc = yc;

          if ((!wgs84ProjLL) && (!(wgs84Pr2LLct->Transform(1, &txc, &tyc))))
            throw Spine::Exception(BCP, "transform: Transform(wgs84,wgs84) failed");

          tgtLatLons[x][y] = NFmiPoint(txc, tyc);
        }
      }
    }

    itsDX = fabs((tr.X() - bl.X()) / xs);
    itsDY = fabs((tr.Y() - bl.Y()) / ys);
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Set locations for getting querydata values and bounding box and
 *		grid cell dimensions for output grid.
 */
// ----------------------------------------------------------------------

void DataStreamer::coordTransform(Engine::Querydata::Q q, const NFmiArea *area)
{
  try
  {
    if (setMeta)
    {
      // Set geometry
      //
      NFmiPoint bl, tr;

      if (((!cropping.cropped) && (itsReqParams.datumShift == Datum::None)) ||
          (!itsReqParams.bboxRect))
      {
        // Using the native or projected area's corners
        //
        bl = area->BottomLeftLatLon();
        tr = area->TopRightLatLon();
      }
      else
      {
        // Using the cropped area's corners or the given target bbox for gdal transformation
        //
        bl = NFmiPoint((*(itsReqParams.bboxRect))[0].first, (*(itsReqParams.bboxRect))[0].second);
        tr = NFmiPoint((*(itsReqParams.bboxRect))[1].first, (*(itsReqParams.bboxRect))[1].second);
      }

      itsBoundingBox.bottomLeft = bl;
      itsBoundingBox.topRight = tr;

      if (itsReqParams.datumShift == Datum::None)
      {
        itsDX = area->WorldXYWidth() / (itsReqGridSizeX - 1);
        itsDY = area->WorldXYHeight() / (itsReqGridSizeY - 1);
      }
      else
      {
        // Transform the coordinates to 'srcLatLons' -member
        //
        setTransformedCoordinates(q, area);
      }

      if (itsReqParams.gridStepXY)
      {
        itsDX *= (*(itsReqParams.gridStepXY))[0].first;
        itsDY *= (*(itsReqParams.gridStepXY))[0].second;
      }
    }
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Build a new NFmiVPlaceDescriptor
 *
 */
// ----------------------------------------------------------------------

NFmiVPlaceDescriptor DataStreamer::makeVPlaceDescriptor(Engine::Querydata::Q q,
                                                        bool allLevels) const
{
  try
  {
    if (allLevels)
    {
      auto info = q->info();
      return NFmiVPlaceDescriptor(((NFmiQueryInfo *) &(*info))->VPlaceDescriptor());
    }

    auto old_idx = q->levelIndex();

    NFmiLevelBag lbag;

    for (q->resetLevel(); q->nextLevel();)
    {
      float value = q->levelValue();

      if (find(itsDataLevels.begin(), itsDataLevels.end(), value) != itsDataLevels.end())
      {
        lbag.AddLevel(q->level());

        if (itsReqParams.outputFormat != QD)
          // Only one level for querydata created for cached projection handling
          //
          break;
      }
    }

    q->levelIndex(old_idx);

    return NFmiVPlaceDescriptor(lbag);
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Build a new NFmiParamDescriptor
 *
 */
// ----------------------------------------------------------------------

NFmiParamDescriptor DataStreamer::makeParamDescriptor(Engine::Querydata::Q q,
                                                      const std::list<FmiParameterName> &currentParams) const
{
  try
  {
    NFmiParamBag pbag;

    if (currentParams.size() > 0)
    {
      for (auto const &param : currentParams)
      {
        q->param(param);
        pbag.Add(q->param());
      }

      if (currentParams.size() > 1)
        q->param(currentParams.front());

      return pbag;
    }

    // Save parameter status
    auto old_idx = q->paramIndex();
    bool wasSubParamUsed = q->isSubParamUsed();

    for (auto it = itsDataParams.begin(); it != itsDataParams.end(); ++it)
    {
      if (q->param(it->number()))
      {
        pbag.Add(q->param());

        if (itsReqParams.outputFormat != QD)
          // Only one parameter for querydata created for cached projection handling
          //
          break;
      }
    }

    // Restore parameter status
    q->paramIndex(old_idx);
    q->setIsSubParamUsed(wasSubParamUsed);

    return pbag;
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Build a new NFmiTimeDescriptor
 *
 */
// ----------------------------------------------------------------------

NFmiTimeDescriptor DataStreamer::makeTimeDescriptor(Engine::Querydata::Q q, bool nativeTimes)
{
  try
  {
    // Note: Origintime is taken from the first querydata; firstTime() (by generateValidTimeList())
    // and possibly Time(itsDataTimes.begin()) (by extractData()) has been called for q.

    if (nativeTimes)
    {
      auto info = q->info();
      return NFmiTimeDescriptor(((NFmiQueryInfo *) &(*info))->TimeDescriptor());
    }

    NFmiMetTime ot = q->originTime();
    Spine::TimeSeriesGenerator::LocalTimeList::const_iterator timeIter = itsDataTimes.begin();
    NFmiTimeList dataTimes;

    for (; (timeIter != itsDataTimes.end()); timeIter++)
    {
      dataTimes.Add(new NFmiMetTime(timeIter->utc_time()));

      if (itsReqParams.outputFormat != QD)
      {
        // Only one time for querydata created for cached projection handling
        //
        return NFmiTimeDescriptor(ot, dataTimes);
      }
    }

    return NFmiTimeDescriptor(ot, dataTimes);
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Create target querydata.
 *
 */
// ----------------------------------------------------------------------

void DataStreamer::createQD(const NFmiGrid &g)
{
  try
  {
    NFmiParamDescriptor pdesc = makeParamDescriptor(itsQ);
    NFmiTimeDescriptor tdesc = makeTimeDescriptor(itsQ);
    NFmiHPlaceDescriptor hdesc = NFmiHPlaceDescriptor(g);
    NFmiVPlaceDescriptor vdesc = makeVPlaceDescriptor(itsQ);
    NFmiFastQueryInfo qi(pdesc, tdesc, hdesc, vdesc, itsQ->infoVersion());

    itsQueryData.reset(NFmiQueryDataUtil::CreateEmptyData(qi));
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Get grid's area object
 */
// ----------------------------------------------------------------------

const NFmiArea &getGridArea(const NFmiGrid &grid)
{
  try
  {
    // ???? Should we throw an exception if 'grid.Area()' points to nullptr?
    return *grid.Area();
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Get grid values using cached locations
 *
 */
// ----------------------------------------------------------------------

static void valBufDeleter(float *ptr)
{
  try
  {
    if (ptr)
      delete[] ptr;
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

void DataStreamer::cachedProjGridValues(Engine::Querydata::Q q,
                                        NFmiGrid &wantedGrid,
                                        const NFmiMetTime *mt,
                                        NFmiDataMatrix<float> *demValues,
                                        NFmiDataMatrix<bool> *waterFlags)
{
  try
  {
    unsigned long xs = wantedGrid.XNumber();

    itsGridValues.Resize(xs, wantedGrid.YNumber(), kFloatMissing);

    // Target querydata is needed for the interpolation. It will be used for the data output too if
    // qd format was selected.

    if (!itsQueryData.get())
      createQD(wantedGrid);

    // Get location cache

    if (locCache.NX() == 0)
    {
      NFmiFastQueryInfo tqi(itsQueryData.get());
      q->calcLatlonCachePoints(tqi, locCache);
    }
    else if (demValues && waterFlags && (demValues->NX() == 0))
      // Target grid does not intersect the native grid; the DEM values were loaded (and then
      // cleared) upon the first call
      //
      return;

    // Get time cache

    NFmiTimeCache tc;
    if (mt)
      tc = q->calcTimeCache(*mt);

    FmiParameterName id = q->parameterName();

    bool cropxy = (cropping.cropped && cropping.cropMan);
    size_t x0 = (cropxy ? cropping.bottomLeftX : 0), y0 = (cropxy ? cropping.bottomLeftY : 0);
    size_t xN = (cropping.cropped ? (x0 + cropping.gridSizeX) : itsReqGridSizeX),
           yN = (cropping.cropped ? (y0 + cropping.gridSizeY) : itsReqGridSizeY);

    size_t xStep = (itsReqParams.gridStepXY ? (*(itsReqParams.gridStepXY))[0].first : 1),
           yStep = (itsReqParams.gridStepXY ? (*(itsReqParams.gridStepXY))[0].second : 1), x, y;

    if (q->isRelativeUV() && ((id == kFmiWindUMS) || (id == kFmiWindVMS)))
    {
      // Wind components need to be rotated by the difference of the true north azimuthal angles.
      //
      // Note: getting/setting isSubParamUsed flag is obsolete when no more setting the parameter
      // index; for now keeping it.

      bool isSubParamUsed = q->isSubParamUsed();

      if (!q->param(kFmiWindUMS))
        throw Spine::Exception(BCP, "Data does not contain Wind U-component");
      if (!q->param(kFmiWindVMS))
        throw Spine::Exception(BCP, "Data does not contain Wind V-component");

      q->setIsSubParamUsed(isSubParamUsed);

      // Get V values

      typedef std::unique_ptr<float, void (*)(float *)> valBuf;

      valBuf vValues(new float[xs * wantedGrid.YNumber()], valBufDeleter);
      float *vPtr0 = vValues.get();

      for (y = y0; (y < yN); y += yStep)
        for (x = x0; (x < xN); x += xStep)
        {
          NFmiLocationCache &lc = locCache[x][y];
          *(vPtr0 + ((y * xs) + x)) =
              (mt ? q->cachedInterpolation(lc, tc) : q->cachedInterpolation(lc));
        }

      // Get U values

      if (!q->param(kFmiWindUMS))
        throw Spine::Exception(BCP, "Internal error: could not switch to parameter U");
      q->setIsSubParamUsed(isSubParamUsed);

      valBuf uValues(new float[xs * wantedGrid.YNumber()], valBufDeleter);
      float *uPtr0 = uValues.get();

      for (y = y0; (y < yN); y += yStep)
        for (x = x0; (x < xN); x += xStep)
        {
          NFmiLocationCache &lc = locCache[x][y];
          *(uPtr0 + ((y * xs) + x)) =
              (mt ? q->cachedInterpolation(lc, tc) : q->cachedInterpolation(lc));
        }

      // Rotate

      const NFmiArea *sourceArea = q->grid().Area();
      const NFmiArea *targetArea = wantedGrid.Area();

      for (y = y0; (y < yN); y += yStep)
        for (x = x0; (x < xN); x += xStep)
        {
          float *vPtr = vPtr0 + ((y * xs) + x), *uPtr = uPtr0 + ((y * xs) + x);
          double value = kFloatMissing;

          if ((*uPtr != kFloatMissing) && (*vPtr != kFloatMissing))
          {
            if (!wantedGrid.Index(wantedGrid.Index(x, y)))
              throw Spine::Exception(BCP, "Internal error: could not set grid index");

            double azimuth1 = sourceArea->TrueNorthAzimuth(wantedGrid.LatLon()).ToRad();
            double azimuth2 = targetArea->TrueNorthAzimuth(wantedGrid.LatLon()).ToRad();
            double da = azimuth2 - azimuth1;

            double uu = *uPtr * cos(da) + *vPtr * sin(da);
            double vv = *vPtr * cos(da) - *uPtr * sin(da);

            value = ((id == kFmiWindUMS) ? uu : vv);
          }

          itsGridValues[x][y] = boost::numeric_cast<float>(value);
        }

      if (!q->param(id))
        throw Spine::Exception(BCP,
                               "Internal error: could not switch to parameter " +
                                   boost::lexical_cast<std::string>(id));
      q->setIsSubParamUsed(isSubParamUsed);
    }
    else if (demValues && waterFlags)
    {
      // Landscaping

      auto &demMatrix = *demValues;
      auto &waterFlagMatrix = *waterFlags;

      if (demMatrix.NX() == 0)
      {
        // Load dem values and water flags for the target area/grid
        //
        double resolution = wantedGrid.Area()->WorldXYWidth() / 1000.0 / itsGridValues.NX();
        auto theDEM = itsGeoEngine->dem();
        auto theLandCover = itsGeoEngine->landCover();

        if ((!(theDEM && theLandCover)) ||
            (!(q->loadDEMAndWaterFlags(
                *theDEM, *theLandCover, resolution, locCache, demMatrix, waterFlagMatrix))))
        {
          // No dem/waterflag data or target grid does not intersect the native grid
          //
          demMatrix = NFmiDataMatrix<float>();
          return;
        }
      }

      // Note: Because time cache must not be empty (NoValue()), set the current (native)
      // time instant when no time interpolation (we know a native time has been set to q)

      if (!mt)
        tc = q->calcTimeCache(q->validTime());

      q->landscapeCachedInterpolation(itsGridValues, locCache, tc, demMatrix, waterFlagMatrix);
    }
    else
    {
      // Normal access

      for (y = y0; (y < yN); y += yStep)
      {
        for (x = x0; (x < xN); x += xStep)
        {
          NFmiLocationCache &lc = locCache[x][y];
          itsGridValues[x][y] = (mt ? q->cachedInterpolation(lc, tc) : q->cachedInterpolation(lc));
        }
      }
    }
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Check if 'requestedLevel' is available in grid data.
 *
 *		Returns true on success and 'exactLevel' is set to indicate
 *		exact match or need to interpolate.
 *
 */
// ----------------------------------------------------------------------

bool DataStreamer::isGridLevelAvailable(int &requestedLevel, bool &exactLevel) const
{
  try
  {
    if (isSurfaceLevel(levelType))
    {
      // Just set the one and only level from the surface data.
      //
      requestedLevel = 0;
      exactLevel = true;

      return true;
    }

    // Level interpolation is possible for pressure data only.

    bool interpolatable = (isPressureLevel(levelType) && itsProducer.verticalInterpolation);
    bool first = true;

    set<T::ParamLevel>::const_iterator it = itsGridMetaData.levels.begin();

    for ( ; (it != itsGridMetaData.levels.end()); first = false, it++)
    {
      // Note: Height level data can have negative levels.

      int level = *it;

      if ((exactLevel = (level == requestedLevel)))
        return true;

      if (requestedLevel > level)
      {
        if (!itsRisingLevels)
          // Interpolatable if between data levels, data is interpolatable and interpolation is
          // allowed
          //
          return (!(first || (!interpolatable)));
      }
      else if (itsRisingLevels)
        // Interpolatable if between data levels, data is interpolatable and interpolation is
        // allowed
        //
        return (!(first || (!interpolatable)));
    }

    return false;
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Check if 'requestedLevel' is available in the querydata.
 *
 *		Returns true on success and 'exactLevel' is set to indicate
 *		exact match or need to interpolate.
 *
 */
// ----------------------------------------------------------------------

bool DataStreamer::isLevelAvailable(Engine::Querydata::Q q,
                                    int &requestedLevel,
                                    bool &exactLevel) const
{
  try
  {
    q->resetLevel();

    bool hasNextLevel = q->nextLevel();

    if (!hasNextLevel)
      throw Spine::Exception(BCP, "isLevelAvailable: internal: no levels in data");

    if (isSurfaceLevel(levelType))
    {
      // Just set the one and only level from the surface data.
      //
      requestedLevel = abs(boost::numeric_cast<int>(q->levelValue()));
      exactLevel = true;

      return true;
    }

    // Level interpolation is possible for pressure data only.

    bool interpolatable = (isPressureLevel(levelType) && itsProducer.verticalInterpolation);
    bool first = true;

    for (; hasNextLevel; first = false, hasNextLevel = q->nextLevel())
    {
      // Note: Height level data can have negative levels.

      int level = abs(boost::numeric_cast<int>(q->levelValue()));

      if ((exactLevel = (level == requestedLevel)))
        return true;

      if (requestedLevel > level)
      {
        if (!itsRisingLevels)
          // Interpolatable if between data levels, data is interpolatable and interpolation is
          // allowed
          //
          return (!(first || (!interpolatable)));
      }
      else if (itsRisingLevels)
        // Interpolatable if between data levels, data is interpolatable and interpolation is
        // allowed
        //
        return (!(first || (!interpolatable)));
    }

    return false;
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Inspect request's gridsize and projection related parameters
 *	    and create target projection (area object) if needed.
 *
 *		Note: If area is created, it is owned by resource manager.
 */
// ----------------------------------------------------------------------

void DataStreamer::createArea(Engine::Querydata::Q q,
                              const NFmiArea &nativeArea,
                              unsigned long nativeClassId,
                              size_t nativeGridSizeX,
                              size_t nativeGridSizeY)
{
  try
  {
    itsUseNativeProj = itsUseNativeBBox = true;
    itsRetainNativeGridResolution = cropping.crop = false;

    if (itsReqParams.datumShift != Datum::None)
    {
      // With datum shift the data is read using transformed coordinates and native projected data.
      //
      // Note: Rotated latlon (needs wkt import from proj4 ob_tran projection string) and mercator
      // (the only mercator qd seen so far had equal bottom left and top right projected X
      // coordinate)
      // currently not supported.
      //
      if ((itsReqParams.areaClassId == A_RotLatLon) ||
          ((itsReqParams.areaClassId == A_Native) && (nativeClassId == kNFmiRotatedLatLonArea)))
        throw Spine::Exception(BCP, "Rotated latlon not supported when using gdal transformation");
      else if ((itsReqParams.areaClassId == A_Mercator) ||
               ((itsReqParams.areaClassId == A_Native) && (nativeClassId == kNFmiMercatorArea)))
        throw Spine::Exception(BCP, "Mercator not supported when using gdal transformation");

      return;
    }

    // No datum shift; nonnative target projection, bounding or gridsize ?

    if ((!itsReqParams.projection.empty()) || (!itsReqParams.bbox.empty()) ||
        (!itsReqParams.gridCenter.empty()) || (!itsUseNativeGridSize))
    {
      string projection = nativeArea.AreaStr(), projStr, bboxStr;
      boost::replace_all(projection, ":", "|");

      if ((!itsReqParams.projection.empty()) && (projection.find(itsReqParams.projection) == 0))
        itsReqParams.projection.clear();

      if ((!itsReqParams.projection.empty()) || (!itsReqParams.bbox.empty()) ||
          (!itsReqParams.gridCenter.empty()))
      {
        size_t bboxPos = projection.find("|");

        if ((bboxPos != string::npos) && (bboxPos > 0) && (bboxPos < (projStr.length() - 1)))
        {
          projStr = projection.substr(0, bboxPos);
          bboxStr = projection.substr(bboxPos + 1);

          itsUseNativeProj =
              (itsReqParams.projection.empty() || (itsReqParams.projection == projStr));

          if (!itsUseNativeProj)
            projStr = itsReqParams.projection;  // Creating nonnative projection

          itsUseNativeBBox = ((itsReqParams.bbox.empty() || (itsReqParams.bbox == bboxStr)) &&
                              itsReqParams.gridCenter.empty());
          if (!itsUseNativeBBox && (((itsReqParams.outputFormat == QD) && (!itsUseNativeProj)) ||
                                    (!itsUseNativeGridSize)))
          {
            // Creating native or nonnative projection with given bounding to load data using
            // absolute
            // gridsize
            //
            if (itsUseNativeGridSize)
            {
              // Projected qd output; set native gridresolution for output querydata
              //
              setNativeGridResolution(nativeArea, nativeGridSizeX, nativeGridSizeY);
              itsUseNativeGridSize = false;
            }

            itsUseNativeProj = false;
          }
          else if ((!itsUseNativeProj) && (nativeClassId != kNFmiLatLonArea))
          {
            // Get native area latlon bounding box for nonnative projection.
            // Set itsRetainNativeGridResolution to retain native gridresolution if projecting to
            // latlon.
            //
            bboxStr = getRegLLBBoxStr(q);

            if (itsReqParams.projType == P_LatLon)
              itsRetainNativeGridResolution = itsUseNativeGridSize;
          }

          itsReqParams.projection = projStr;

          if ((!itsUseNativeProj) || ((itsReqParams.outputFormat == QD) && (!itsUseNativeBBox)))
          {
            if (itsUseNativeProj)
            {
              // Set cropping to the native grid and adjust the target bbox.
              //
              setCropping(q->grid());
            }

            if (!itsReqParams.bbox.empty())
              // bbox from the request or set by setCropping()
              bboxStr = itsReqParams.bbox;
            else if (!itsReqParams.gridCenter.empty())
              // lon,lat,xkm,ykm
              bboxStr = getGridCenterBBoxStr(itsUseNativeProj, q->grid());
            else
            {
              // Native area latlon bounding box from getRegLLBBoxStr()
            }

            projection = projStr + "|" + bboxStr;
            itsResMgr.createArea(projection);
          }

          cropping.crop |= (itsUseNativeProj && (!itsUseNativeBBox) && itsUseNativeGridSize);
        }
        else
          throw Spine::Exception(BCP,
                                 "Unrecognized projection '" + projection + "' for producer '" +
                                     itsReqParams.producer + "'");
      }
    }
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Inspect request's gridsize related parameters and create new
 *		grid with requested size if needed.
 *
 *		Note: The grid is owned by resource manager.
 */
// ----------------------------------------------------------------------

void DataStreamer::createGrid(const NFmiArea &area,
                              size_t gridSizeX,
                              size_t gridSizeY,
                              bool interpolation)
{
  try
  {
    NFmiGrid *grid = itsResMgr.getGrid(area, gridSizeX, gridSizeY);

    if (cropping.crop)
    {
      if (!cropping.cropped)
      {
        // Set cropped grid xy area
        //
        setCropping(*grid);
      }

      // Must use manual cropping (loading entire grid and manually extracting data within given
      // bounding)
      // if nonnative projection or level/pressure interpolated data; CroppedValues() does not
      // support
      // level/pressure interpolation.

      cropping.cropMan = ((!itsUseNativeProj) || interpolation);
    }
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Inspect request's gridsize and projection related parameters
 *		and create target projection (area object) and grid if needed.
 *
 *		DEM values and open water flags for landscaping are also loaded
 *		for native grid upon 1'st call.
 *
 *		The area (native or projected) and grid (nullptr for native) are
 *		returned in given parameters.
 *
 *		Note: returned area and grid (if not nullptr) are owned by the resource manager;
 *		*DO NOT DELETE*
 *
 *		Returns false for native area and grid.
 */
// ----------------------------------------------------------------------

bool DataStreamer::getAreaAndGrid(Engine::Querydata::Q q,
                                  bool interpolation,
                                  bool landscaping,
                                  const NFmiArea **area,
                                  NFmiGrid **grid)
{
  try
  {
    const NFmiArea &nativeArea = getGridArea(q->grid());
    unsigned long nativeClassId = nativeArea.ClassId();

    size_t nativeGridSizeX = q->grid().XNumber(), nativeGridSizeY = q->grid().YNumber();
    size_t gridSizeX = itsReqGridSizeX, gridSizeY = itsReqGridSizeY;

    // All data has same projection, gridsize and bounding box; thus target projection (area object)
    // and grid
    // needs to be checked/created only once.
    //
    // Note: itsUseNativeProj, itsUseNativeBBox, itsRetainNativeGridResolution and cropping are set
    // by
    // createArea().
    //		 itsReqGridSizeX and itsReqGridSizeY are set by setRequestedGridSize.

    if (!itsProjectionChecked)
    {
      itsUseNativeGridSize = setRequestedGridSize(nativeArea, nativeGridSizeX, nativeGridSizeY);
      createArea(q, nativeArea, nativeClassId, nativeGridSizeX, nativeGridSizeY);
    }

    // Note: area is set to the native area or owned by the resource manager; *DO NOT DELETE*

    if (!(*area = itsResMgr.getArea()))
      *area = &nativeArea;

    if (!itsProjectionChecked)
    {
      // Recalculate gridsize if nonnative projection and grid resolution is given (or set to retain
      // it when projecting to latlon).
      //
      if (itsRetainNativeGridResolution)
        setNativeGridResolution(nativeArea, nativeGridSizeX, nativeGridSizeY);

      if ((!itsUseNativeProj) && (!itsReqParams.gridResolution.empty()))
        itsUseNativeGridSize = setRequestedGridSize(**area, nativeGridSizeX, nativeGridSizeY);
    }

    bool nonNativeGrid = (!(itsUseNativeProj && itsUseNativeGridSize));

    if (!itsProjectionChecked)
    {
      if ((itsReqParams.datumShift == Datum::None) && (nonNativeGrid || (!itsUseNativeBBox)))
      {
        // Create grid if using nonnative grid size. Use the cropped size for cropped querydata.
        //
        gridSizeX = ((itsReqParams.outputFormat == QD) && cropping.cropped) ? cropping.gridSizeX
                                                                            : itsReqGridSizeX;
        gridSizeY = ((itsReqParams.outputFormat == QD) && cropping.cropped) ? cropping.gridSizeY
                                                                            : itsReqGridSizeY;

        createGrid(**area, gridSizeX, gridSizeY, interpolation);
      }

      auto gs = (cropping.crop ? cropping.gridSizeX * cropping.gridSizeY
                               : itsReqGridSizeX * itsReqGridSizeY);
      unsigned long numValues =
          itsDataParams.size() * itsDataLevels.size() * itsDataTimes.size() * gs;

      if (numValues > itsCfg.getMaxRequestDataValues())
      {
        throw Spine::Exception(
            BCP,
            "Too much data requested (" + Fmi::to_string(numValues) + " values, max " +
                Fmi::to_string(itsCfg.getMaxRequestDataValues()) +
                "); adjust area/grid and/or number of parameters, levels and times");
      }

      itsProjectionChecked = true;
    }

    // Note: grid is set to nullptr or owned by the resource manager; *DO NOT DELETE*

    *grid = itsResMgr.getGrid();

    if ((!nonNativeGrid) && landscaping && (itsDEMMatrix.NX() == 0))
    {
      // Load dem values and water flags for the native grid
      //
      int x1 = 0, y1 = 0, x2 = 0, y2 = 0;

      if (cropping.cropped && (!cropping.cropMan))
      {
        x1 = cropping.bottomLeftX;
        y1 = cropping.bottomLeftY;
        x2 = cropping.topRightX;
        y2 = cropping.topRightY;
      }

      auto theDEM = itsGeoEngine->dem();
      auto theLandCover = itsGeoEngine->landCover();

      if (theDEM && theLandCover)
        q->loadDEMAndWaterFlags(*theDEM,
                                *theLandCover,
                                0,
                                NFmiDataMatrix<NFmiLocationCache>(),
                                itsDEMMatrix,
                                itsWaterFlagMatrix,
                                x1,
                                y1,
                                x2,
                                y2);
    }

    return nonNativeGrid;
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Move to next grid parameter
 *
 */
// ----------------------------------------------------------------------

void DataStreamer::nextGridParam()
{
  try
  {
    // Advance parameter and scaling iterators. Skip missing parameters

    for (itsParamIterator++; (itsParamIterator != itsDataParams.end()); itsParamIterator++)
    {
      if (itsScalingIterator != itsValScaling.end())
      {
        itsScalingIterator++;

        if (itsScalingIterator == itsValScaling.end())
          throw Spine::Exception(BCP, "nextGridParam: internal: No more scaling data");
      }

      if (itsGridMetaData.paramKeys.find(itsParamIterator->name()) != itsGridMetaData.paramKeys.end())
        break;
    }

    paramChanged();
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Move to next querydata parameter
 *
 */
// ----------------------------------------------------------------------

void DataStreamer::nextParam(Engine::Querydata::Q q)
{
  try
  {
    // Advance parameter and scaling iterators. Skip missing parameters

    for (itsParamIterator++; (itsParamIterator != itsDataParams.end()); itsParamIterator++)
    {
      if ((itsReqParams.outputFormat != QD) && (itsScalingIterator != itsValScaling.end()))
      {
        itsScalingIterator++;

        if (itsScalingIterator == itsValScaling.end())
          throw Spine::Exception(BCP, "nextParam: internal: No more scaling data");
      }

      if (q->param(itsParamIterator->number()))
        break;
    }

    // In-memory qd needs to be reloaded if it does not contain current parameter

    if ((itsParamIterator != itsDataParams.end()) && itsCPQ && (!itsCPQ->param(itsParamIterator->number())))
      itsCPQ.reset();

    paramChanged();
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Get Q for in-memory querydata object containing only current parameter(s)
 *        (both U and V if true north azimuth adjustment needed)
 *
 */
// ----------------------------------------------------------------------

Engine::Querydata::Q DataStreamer::getCurrentParamQ(const std::list<FmiParameterName> &currentParams) const
{
  NFmiParamDescriptor paramDescriptor = makeParamDescriptor(itsQ, currentParams);
  auto srcInfo = itsQ->info();

  NFmiFastQueryInfo info(paramDescriptor,
                         srcInfo->TimeDescriptor(),
                         srcInfo->HPlaceDescriptor(),
                         srcInfo->VPlaceDescriptor(),
                         itsQ->infoVersion()
                        );

  boost::shared_ptr<NFmiQueryData> data(NFmiQueryDataUtil::CreateEmptyData(info));
  NFmiFastQueryInfo dstInfo(data.get());
  auto levelIndex = itsQ->levelIndex();

  for (dstInfo.ResetParam(); dstInfo.NextParam();)
  {
    srcInfo->Param(dstInfo.Param());

    for (dstInfo.ResetLocation(), srcInfo->ResetLocation();
         dstInfo.NextLocation() && srcInfo->NextLocation();)
    {
      for (dstInfo.ResetLevel(), srcInfo->ResetLevel(); dstInfo.NextLevel() && srcInfo->NextLevel();)
      {
        for (dstInfo.ResetTime(), srcInfo->ResetTime(); dstInfo.NextTime() && srcInfo->NextTime();)
        {
          dstInfo.FloatValue(srcInfo->FloatValue());
        }
      }
    }
  }

  itsQ->levelIndex(levelIndex);

  std::size_t hash = 0;
  auto model = boost::make_shared<Engine::Querydata::Model>(data, hash);

  return boost::make_shared<Engine::Querydata::QImpl>(model);
}

// ----------------------------------------------------------------------
/*!
 * \brief Extract data
 *
 */
// ----------------------------------------------------------------------

void DataStreamer::extractData(string &chunk)
{
  try
  {
    // First chunk is loaded at iniatialization

    if (!itsDataChunk.empty())
    {
      chunk = itsDataChunk;
      itsDataChunk.clear();

      return;
    }

    chunk.clear();

    if (itsReqParams.source == "grid")
    {
      extractGridData(chunk);
      return;
    }

    auto theParamsEnd = itsDataParams.end();
    auto theLevelsBegin = itsDataLevels.begin();
    auto theLevelsEnd = itsDataLevels.end();
    auto theTimesBegin = itsDataTimes.begin();
    auto theTimesEnd = itsDataTimes.end();

    // Note: for better performance looping order (param, level, time) follows the querydata
    // structure.
    //
    // Loop over the parameters.

    auto q = itsQ;

    for (; (itsParamIterator != theParamsEnd); nextParam(q),
                                               itsLevelIterator = theLevelsBegin,
                                               itsLevelIndex = 0,
                                               itsTimeIterator = theTimesBegin,
                                               itsTimeIndex = 0)
    {
      // Loop over the queried levels.

      for (; (itsLevelIterator != theLevelsEnd);
           itsLevelIterator++, itsLevelIndex++, itsTimeIterator = theTimesBegin, itsTimeIndex = 0)
      {
        // Skip times earlier than first available validtime

        auto timeInstant = itsFirstDataTime;

        while (itsTimeIterator != theTimesEnd)
        {
          timeInstant = itsTimeIterator->utc_time();

          if (timeInstant < itsFirstDataTime)
          {
            itsTimeIterator++;
            itsTimeIndex++;
          }
          else
            break;
        }

        // Skip times later than last available validtime

        if ((itsTimeIterator == theTimesEnd) || (timeInstant > itsLastDataTime))
        {
          // Next level or parameter
          //
          continue;
        }

        // Check that the requested level is available in the querydata (exact match or
        // interpolatable).

        int level = *itsLevelIterator;
        bool exactLevel;

        if (!isLevelAvailable(q, level, exactLevel))
        {
          continue;
        }

        // Inspect request parameters and get area (projection) and grid.
        // Upon first call for landscaped parameter DEM values and open water flags are obtained for
        // the grid.
        //
        // Note: The returned area is the native area or owned by resource manager; *DO NOT DELETE*.
        // Note: The returned grid is nullptr or owned by resource manager; *DO NOT DELETE*.
        //
        // Note: Grid size is set to 'itsReqGridSizeX' and 'itsReqGridSizeY' members.

        // bool landscapedParam =
        //     (isSurfaceLevel(levelType) && (itsParamIterator->type() ==
        //     Parameter::Type::Landscaped));
        bool landscapedParam =
            false;  // Disable landscaping until sufficiently fast algorithm is found!
        decltype(itsDEMMatrix) noDEMValues;
        decltype(itsWaterFlagMatrix) noWaterFlags;

        const NFmiArea *area;
        NFmiGrid *grid;

        bool nonNativeGrid = getAreaAndGrid(q, !exactLevel, landscapedParam, &area, &grid);

        auto const &demValues = (landscapedParam ? itsDEMMatrix : noDEMValues);
        auto const &waterFlags = (landscapedParam ? itsWaterFlagMatrix : noWaterFlags);

        // Heigth level data with negative levels ?

        if ((levelType == kFmiDepth) && (nativeLevelType == kFmiHeight))
          level = 0 - level;

        NFmiMetTime mt(itsTimeIterator->utc_time());

        // Set target projection geometry data (to 'itsBoundingBox' and 'dX'/'dY' members) and if
        // using gdal/proj4 projection,
        // transform target projection grid coordinates to 'srcLatLons' -member to get the grid
        // values.

        coordTransform(q, area);

        if (! itsMultiFile)
        {
          if (!itsCPQ)
          {
            // Get Q for in-memory querydata object containing only current parameter.
            //
            // For wind component true north adjustment both U and V are needed.
            //
            // Note: Should check all called newbase methods actually do the fix
            // (DoWindComponentFix or whatever).

            std::list<FmiParameterName> currentParams;

            auto id = q->parameterName();
            currentParams.push_back(id);

            if (q->isRelativeUV() && ((id == kFmiWindUMS) || (id == kFmiWindVMS)))
            {
              FmiParameterName id2 = ((id == kFmiWindUMS) ? kFmiWindVMS : kFmiWindUMS);
              if (q->param(id2))
                currentParams.push_back(id2);

              // No need to reset param (to 'id') here, will be set by call to getCurrentParamQ
            }

            itsCPQ = getCurrentParamQ(currentParams); 
          }

          // Set level index from main data, time index gets set (or is not used) below

          itsCPQ->levelIndex(itsQ->levelIndex());
          q = itsCPQ;
        }

        if (itsReqParams.datumShift == Datum::None)
        {
          // Using newbase projection.
          //
          if (exactLevel)
          {
            bool timeInterpolation = (!q->time(mt));

            if (timeInterpolation || nonNativeGrid)
            {
              if (nonNativeGrid)
                cachedProjGridValues(q,
                                     *grid,
                                     timeInterpolation ? &mt : nullptr,
                                     landscapedParam ? &itsDEMMatrix : nullptr,
                                     landscapedParam ? &itsWaterFlagMatrix : nullptr);
              else
              {
                // Must manually crop the data if bounding was given
                // ('cropMan' was not set by the call to getAreaAndGrid())
                //
                cropping.cropMan = cropping.crop;
                q->values(itsGridValues, mt, demValues, waterFlags);
              }
            }
            else
            {
              if (cropping.cropped && (!cropping.cropMan))
                q->croppedValues(itsGridValues,
                                 cropping.bottomLeftX,
                                 cropping.bottomLeftY,
                                 cropping.topRightX,
                                 cropping.topRightY,
                                 demValues,
                                 waterFlags);
              else
                q->values(itsGridValues, demValues, waterFlags);
            }
          }
          else if (nonNativeGrid)
            q->pressureValues(itsGridValues, *grid, mt, level, q->isRelativeUV());
          else
            q->pressureValues(itsGridValues, mt, level);
        }
        else
          // Using gdal/proj4 projection.
          //
          q->values(srcLatLons, itsGridValues, mt, exactLevel ? kFloatMissing : level);

        // Load the data chunk from 'itsGridValues'.
        //
        // Note: With querydata and netcdf output the data is taken (and buffered) from the
        // 'itsGridValues' member
        // instead of 'chunk' by the upper level (e.g. the format specific getChunk() method).

        if ((itsGridValues.NX() == 0) || (itsGridValues.NY() == 0))
          throw Spine::Exception(BCP,
                                 "Extract data: internal: Query returned no data for producer '" +
                                     itsReqParams.producer + "'");

        getDataChunk(q, area, grid, level, mt, itsGridValues, chunk);

        // Move to next time instant

        itsTimeIterator++;
        itsTimeIndex++;

        return;
      }  // for queried levels
    }    // for parameters

    // No more data
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Build grid query object for querying data for
 *        current parameter, level and validtime
 *
 */
// ----------------------------------------------------------------------

bool DataStreamer::buildGridQuery(QueryServer::Query &gridQuery) const
{
  bool nativeArea = false;

  if (itsReqParams.bboxRect)
  {
    string bbox = Fmi::to_string((*itsReqParams.bboxRect)[0].first) + "," +
                  Fmi::to_string((*itsReqParams.bboxRect)[0].second) + "," +
                  Fmi::to_string((*itsReqParams.bboxRect)[1].first) + "," +
                  Fmi::to_string((*itsReqParams.bboxRect)[1].second);

    if (
        (fabs((*itsReqParams.bboxRect)[0].first) <= 360) &&
        (fabs((*itsReqParams.bboxRect)[0].second) <= 180) &&
        (fabs((*itsReqParams.bboxRect)[1].first) <= 360) &&
        (fabs((*itsReqParams.bboxRect)[1].second) <= 180)
       )
      gridQuery.mAttributeList.addAttribute("grid.llbox", bbox);
    else
      gridQuery.mAttributeList.addAttribute("grid.bbox", bbox);
  }
  else if (itsReqParams.gridCenterLL)
  {
    string gridCenter = Fmi::to_string((*itsReqParams.gridResolutionXY)[0].first) + "," +
                        Fmi::to_string((*itsReqParams.gridResolutionXY)[0].second);
    string gridMetricWidth = Fmi::to_string((*itsReqParams.gridResolutionXY)[1].first);
    string gridMetricHeight = Fmi::to_string((*itsReqParams.gridResolutionXY)[1].second);

    gridQuery.mAttributeList.addAttribute("grid.center", gridCenter);
    gridQuery.mAttributeList.addAttribute("grid.metricWidth", gridMetricWidth);
    gridQuery.mAttributeList.addAttribute("grid.metricHeight", gridMetricHeight);
  }
  else
    nativeArea = true;

  bool nativeResolution = false;

  if (itsReqParams.gridSizeXY)
  {
    string gridWidth = Fmi::to_string((*itsReqParams.gridSizeXY)[0].first);
    string gridHeight = Fmi::to_string((*itsReqParams.gridSizeXY)[0].second);

    gridQuery.mAttributeList.addAttribute("grid.width", gridWidth);
    gridQuery.mAttributeList.addAttribute("grid.height", gridHeight);
  }
  else if (itsReqParams.gridResolutionXY)
  {
    string gridCellWidth = Fmi::to_string((*itsReqParams.gridResolutionXY)[0].first);
    string gridCellHeight = Fmi::to_string((*itsReqParams.gridResolutionXY)[0].second);

    gridQuery.mAttributeList.addAttribute("grid.cell.width", gridCellWidth);
    gridQuery.mAttributeList.addAttribute("grid.cell.height", gridCellHeight);
  }
  else
    nativeResolution = (!nativeArea);  // Use 'data' crs if area not given

  gridQuery.mSearchType = QueryServer::Query::SearchType::TimeSteps;
  gridQuery.mTimezone = "UTC";
  gridQuery.mForecastTimeList.insert(to_iso_string(itsTimeIterator->utc_time()));

  QueryServer::QueryParameter queryParam;

  queryParam.mType = QueryServer::QueryParameter::Type::Vector;
  queryParam.mLocationType = QueryServer::QueryParameter::LocationType::Geometry;

  queryParam.mParam = itsGridMetaData.paramKeys.find(itsParamIterator->name())->second;
  queryParam.mParameterLevelIdType = T::ParamLevelIdTypeValue::FMI;
  queryParam.mParameterLevelId = itsGridMetaData.paramLevelId;
  queryParam.mParameterLevel = *itsLevelIterator;

  queryParam.mForecastType = -1;
  queryParam.mForecastNumber = -1;
  queryParam.mGeometryId = -1;

  queryParam.mAreaInterpolationMethod = -1;
  queryParam.mTimeInterpolationMethod = -1;
  queryParam.mLevelInterpolationMethod = -1;

  // If reprojecting and bbox/area is not given, query without data first to get the bbox

  bool queryBBox = false;

  if (itsReqParams.projection.empty())
    gridQuery.mAttributeList.addAttribute("grid.crs", nativeResolution ? "crop" : "data");
  else if (nativeArea)
  {
    // Reprojecting and bbox/area is not given; query without data first to get the bbox

    queryParam.mFlags = (QueryServer::QueryParameter::Flags::ReturnCoordinates); // |
//                       QueryServer::QueryParameter::Flags::NoReturnValues);

    gridQuery.mAttributeList.addAttribute("grid.crs", "data");

    queryBBox = true;
  }
  else
    gridQuery.mAttributeList.addAttribute("grid.crs", itsReqParams.projection);

  gridQuery.mQueryParameterList.push_back(queryParam);

  return queryBBox;
}

// ----------------------------------------------------------------------
/*!
 * \brief Get grid projection and datum
 *
 */
// ----------------------------------------------------------------------

void DataStreamer::getGridProjection(const QueryServer::Query &gridQuery)
{
  try
  {
    auto attr = "grid.crs";
    auto crsAttr = gridQuery.mAttributeList.getAttribute(attr);
    auto gridProjection = T::GridProjectionValue::Unknown;

    if (crsAttr && (crsAttr->mValue == "crop"))
    {
      attr = "grid.original.crs";
      crsAttr = gridQuery.mAttributeList.getAttribute("grid.original.crs");
    }

    if ((!crsAttr) || crsAttr->mValue.empty())
      throw Spine::Exception(BCP, string(attr) + " not set in query result");

    if (crsAttr->mValue == itsGridMetaData.crs)
      return;

    char wktBuf[crsAttr->mValue.size() + 1];
    strcpy(wktBuf, crsAttr->mValue.c_str());
    char *wkt = wktBuf;

    OGRSpatialReference srs;
    OGRErr err = srs.SetFromUserInput(wkt);

    if (err != OGRERR_NONE)
      throw Spine::Exception(BCP,"Could not import grid crs: " + crsAttr->mValue);

    if (srs.IsProjected())
    {
      /*
      static const GridProjection Unknown                             = 0;

      static const GridProjection LatLon                              = 1;

      static const GridProjection RotatedLatLon                       = 2;
      +proj=ob_tran +o_proj=lonlat +o_lon_p=x o_lat_p=nonzero

      static const GridProjection StretchedLatLon                     = 3;
      static const GridProjection StretchedRotatedLatLon              = 4;
      static const GridProjection VariableResolutionLatLon            = 5;
      static const GridProjection VariableResolutionRotatedLatLon     = 6;

      static const GridProjection Mercator                            = 7;
      +proj=merc
      SRS_PT_MERCATOR_1SP     "Mercator_1SP"
      SRS_PT_MERCATOR_2SP     "Mercator_2SP"
      [ SRS_PT_MERCATOR_AUXILIARY_SPHERE ]

      static const GridProjection TransverseMercator                  = 8;
      +proj=tmerc
      SRS_PT_TRANSVERSE_MERCATOR
      [ SRS_PT_TRANSVERSE_MERCATOR_SOUTH_ORIENTED ]

      static const GridProjection PolarStereographic                  = 9;
      +proj=stere +lat_0=90 +lat_ts=60
      SRS_PT_POLAR_STEREOGRAPHIC
      [ SRS_PT_STEREOGRAPHIC ] grib N/A, netcdf ?

      static const GridProjection LambertConformal                    = 10;
      +proj=lcc +lon_0=-90 +lat_1=33 +lat_2=45
      SRS_PT_LAMBERT_CONFORMAL_CONIC_1SP
      SRS_PT_LAMBERT_CONFORMAL_CONIC_2SP
      [ SRS_PT_LAMBERT_CONFORMAL_CONIC_2SP_BELGIUM ]

      static const GridProjection ObliqueLambertConformal             = 11;
      Specify the latitude origin and longitude origin to center the map projection
      to the area to be mapped.
      Specifying a non-Equatorial or non-polar origin causes an oblique projection
      +proj=lcc +lon_0=-80 +lat_1=33 +lat_2=45
      ?

      static const GridProjection Albers                              = 12;
      +proj=aea +lat_1=29.5 +lat_2=42.5
      SRS_PT_ALBERS_CONIC_EQUAL_AREA

      static const GridProjection Gaussian                            = 13;
      static const GridProjection RotatedGaussian                     = 14;
      static const GridProjection StretchedGaussian                   = 15;
      static const GridProjection StretchedRotatedGaussian            = 16;
      static const GridProjection SphericalHarmonic                   = 17;
      static const GridProjection RotatedSphericalHarmonic            = 18;
      static const GridProjection StretchedSphericalHarmonic          = 19;
      static const GridProjection StretchedRotatedSphericalHarmonic   = 20;
      static const GridProjection SpaceView                           = 21;
      static const GridProjection Triangular                          = 22;
      static const GridProjection Unstructured                        = 23;
      static const GridProjection EquatorialAzimuthalEquidistant      = 24;
      static const GridProjection AzimuthRange                        = 25;
      static const GridProjection IrregularLatLon                     = 26;
      static const GridProjection LambertAzimuthalEqualArea           = 27;
      static const GridProjection CrossSection                        = 28;
      static const GridProjection Hovmoller                           = 29;
      static const GridProjection TimeSection                         = 30;
      static const GridProjection GnomonicProjection                  = 31;
      static const GridProjection SimplePolyconicProjection           = 32;
      static const GridProjection MillersCylindricalProjection        = 33;
      */

      // Check PROJ4 EXTENSION for rotlat projection;
      // search for +proj=ob_tran, +o_proj=lonlat, nonzero +o_lat_p and +o_lon_p

      itsGridMetaData.projection = srs.GetAttrValue("PROJECTION");
      auto projection = itsGridMetaData.projection.c_str();
      auto p4Extension = srs.GetExtension("PROJCS","PROJ4","");

      if (strstr(p4Extension,"+proj=ob_tran") &&
          (
           strstr(p4Extension,"+o_proj=latlon") ||
           strstr(p4Extension,"+o_proj=lonlat") ||
           strstr(p4Extension,"+o_proj=longlat")
          )
         )
      {
        auto o_lat_p = strstr(p4Extension,"+o_lat_p=");
        auto o_lon_p = strstr(p4Extension,"+o_lon_p=");

        if (o_lat_p)
          o_lat_p += strlen("+o_lat_p=");
        if (o_lon_p)
          o_lon_p += strlen("+o_lon_p=");

        if (o_lat_p && *o_lat_p && o_lon_p && *o_lon_p)
        {
          char olatpbuf[strcspn(o_lat_p," ") + 1];
          char olonpbuf[strcspn(o_lon_p," ") + 1];

          strncpy(olatpbuf, o_lat_p, sizeof(olatpbuf) - 1);
          strncpy(olonpbuf, o_lon_p, sizeof(olonpbuf) - 1);

          olatpbuf[sizeof(olatpbuf) - 1] = '\0';
          olonpbuf[sizeof(olonpbuf) - 1] = '\0';

          itsGridMetaData.southernPoleLat = 0 - Fmi::stod(olatpbuf);
          itsGridMetaData.southernPoleLon = Fmi::stod(olonpbuf);

          if (itsGridMetaData.southernPoleLat != 0)
            gridProjection = T::GridProjectionValue::RotatedLatLon;
          else
            throw Spine::Exception(BCP, "rotlat grid crs proj4 extension is expected to have nonzero o_lat_p: " + crsAttr->mValue);
        }
        else
          throw Spine::Exception(BCP, "rotlat grid crs proj4 extension is expected to have o_lat_p and o_lon_p: " + crsAttr->mValue);
      }
      else if (*p4Extension)
        throw Spine::Exception(BCP,"Unḱnown grid crs proj4 extension: " + string(p4Extension));
      else if (EQUAL(projection, SRS_PT_POLAR_STEREOGRAPHIC))
        gridProjection = T::GridProjectionValue::PolarStereographic;
      else if (EQUAL(projection, SRS_PT_LAMBERT_CONFORMAL_CONIC_1SP))
        gridProjection = T::GridProjectionValue::LambertConformal;
      else if (EQUAL(projection, SRS_PT_LAMBERT_CONFORMAL_CONIC_2SP))
        gridProjection = T::GridProjectionValue::LambertConformal;
      else if (EQUAL(projection, SRS_PT_MERCATOR_1SP))
        gridProjection = T::GridProjectionValue::Mercator;
      else if (EQUAL(projection, SRS_PT_MERCATOR_2SP))
        gridProjection = T::GridProjectionValue::Mercator;
      else
        throw Spine::Exception(BCP, "Unsupported projection in input data: " + crsAttr->mValue);
    }
    else if (!srs.IsGeographic())
      throw Spine::Exception(BCP,"Grid crs is neither projected nor geographic: " + crsAttr->mValue);
    else
      gridProjection = T::GridProjectionValue::LatLon;

    // Spheroid

    auto ellipsoid = srs.GetAttrValue("SPHEROID");
    auto radiusOrSemiMajor = srs.GetAttrValue("SPHEROID", 1);
    auto flattening = srs.GetAttrValue("SPHEROID", 2);

    if (!(ellipsoid && radiusOrSemiMajor))
      throw Spine::Exception(BCP, string(attr) + ": SPHEROID not set");

    itsGridMetaData.ellipsoid = ellipsoid;
    itsGridMetaData.earthRadiusOrSemiMajorInMeters = Fmi::stod(radiusOrSemiMajor);

    if (flattening)
    {
      auto f = Fmi::stod(flattening);

      if (f != 0)
      {
        itsGridMetaData.flattening = f;
        itsGridMetaData.flatteningStr = flattening;
      }
    }

    // Clone/save crs

    itsResMgr.cloneCS(srs, true);

    itsGridMetaData.projType = gridProjection;
    itsGridMetaData.crs = crsAttr->mValue;
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Get projected grid area llbbox
 *
 */
// ----------------------------------------------------------------------

void DataStreamer::getGridLLBBox()
{
  try
  {
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Get projected grid area llbbox string
 *
 */
// ----------------------------------------------------------------------

string DataStreamer::getGridLLBBoxStr()
{
  try
  {
    if (!itsRegBoundingBox)
       return "";

    stringstream os;
    os << fixed << setprecision(8) << (*itsRegBoundingBox).bottomLeft.X() << ","
       << (*itsRegBoundingBox).bottomLeft.Y() << "," << (*itsRegBoundingBox).topRight.X() << ","
       << (*itsRegBoundingBox).topRight.Y();

    return os.str();
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Set/use constant grid size if size/resolution was not set
 *
 */
// ----------------------------------------------------------------------

void DataStreamer::setGridSize(size_t gridSizeX, size_t gridSizeY)
{
  try
  {
    if ((!itsReqParams.gridSizeXY) && (!itsReqParams.gridResolutionXY))
    {
      ostringstream os;

      os << gridSizeX << "," << gridSizeY;

      itsReqParams.gridSize = os.str();
      itsReqParams.gridSizeXY =
          nPairsOfValues<unsigned int>(itsReqParams.gridSize, "gridsize", 1);
    }
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Get native area bbox for requested projection
 *
 */
// ----------------------------------------------------------------------

void DataStreamer::getGridBBox(QueryServer::Query &gridQuery)
{
  try
  {
    // Projection and spheroid

    getGridProjection(gridQuery);

    // Grid size

    auto widthAttr = gridQuery.mAttributeList.getAttribute("grid.width");
    auto heightAttr = gridQuery.mAttributeList.getAttribute("grid.height");

    if ((!widthAttr) || (!heightAttr))
      throw Spine::Exception(BCP, "Grid width/height not set in query result");

    itsReqGridSizeX = Fmi::stoul(widthAttr->mValue.c_str());
    itsReqGridSizeY = Fmi::stoul(heightAttr->mValue.c_str());

    // Set/use constant grid size if size/resolution was not set

    setGridSize(itsReqGridSizeX, itsReqGridSizeY);

    // (Native) grid latlon coordinates

    auto coords = gridQuery.mQueryParameterList.front().mCoordinates;

    if (coords.empty())
      throw Spine::Exception(BCP,"No coordinates to determine data bbox");

    OGRLinearRing exterior;

    auto inputSRS = itsResMgr.getGeometrySRS();
    OGRSpatialReference llSRS;
    llSRS.CopyGeogCSFrom(inputSRS);

    OGRSpatialReference toSRS;
    OGRErr err = toSRS.SetFromUserInput(itsReqParams.projection.c_str());

    if (err != OGRERR_NONE)
      throw Spine::Exception(BCP,"Failed to initialize srs: " + itsReqParams.projection);

    for (size_t y = 1, n = 0, dx = (itsReqGridSizeX - 1); (y <= itsReqGridSizeY); y++, n++)
      for (size_t x = 1; (x <= itsReqGridSizeX);)
      {
        exterior.addPoint(coords[n].x(),coords[n].y());

        size_t dn = (((y == 1) || (y == itsReqGridSizeY)) ? 1 : dx);

        x += dn;

        if (x <= itsReqGridSizeX)
          n += dn;
      }

    exterior.assignSpatialReference(&llSRS);

    err = exterior.transformTo(&toSRS);

    if (err != OGRERR_NONE)
      throw Spine::Exception(BCP,"Failed to transform bbox: " + itsReqParams.projection);

    OGREnvelope psEnvelope;
    exterior.getEnvelope(&psEnvelope);

    string bboxStr = Fmi::to_string(psEnvelope.MinX) + "," +
                     Fmi::to_string(psEnvelope.MinY) + "," +
                     Fmi::to_string(psEnvelope.MaxX) + "," +
                     Fmi::to_string(psEnvelope.MaxY);

    itsReqParams.bboxRect = nPairsOfValues<double>(bboxStr, "bboxstr", 2);

    OGRCoordinateTransformation *ct = itsResMgr.getCoordinateTransformation(&toSRS, &llSRS);

    double lon[] = { psEnvelope.MinX, psEnvelope.MaxX };
    double lat[] = { psEnvelope.MinY, psEnvelope.MaxY };
    int pabSuccess[2];

    int status = ct->TransformEx(2, lon, lat, nullptr, pabSuccess);

    if (!(status && pabSuccess[0] && pabSuccess[1]))
      throw Spine::Exception(BCP,"Failed to transform bbox to llbbox: " + itsReqParams.projection);

    bboxStr = Fmi::to_string(lon[0]) + "," +
              Fmi::to_string(lat[0]) + "," +
              Fmi::to_string(lon[1]) + "," +
              Fmi::to_string(lat[1]);

    itsRegBoundingBox = BBoxCorners();

    (*itsRegBoundingBox).bottomLeft = NFmiPoint(lon[0], lat[0]);
    (*itsRegBoundingBox).topRight = NFmiPoint(lon[1], lat[1]);
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Get query result grid infomation (projection and grid size)
 *
 */
// ----------------------------------------------------------------------

void DataStreamer::getGridQueryInfo(const QueryServer::Query &gridQuery)
{
  try
  {
    // Can't rely on returned query status, check first if got any data

    auto vVec = gridQuery.mQueryParameterList.front().mValueList.front().mValueVector;

    if (vVec.size() == 0)
      throw Spine::Exception(BCP, "Query returned an empty result");

    // Projection and spheroid

    getGridProjection(gridQuery);

    // Bounding box

    string llBBoxStr;
    auto llBBoxAttr = gridQuery.mAttributeList.getAttribute("grid.llbox");

    if (!llBBoxAttr)
      llBBoxStr = getGridLLBBoxStr();
    else
      llBBoxStr = llBBoxAttr->mValue;

    auto llBBox = nPairsOfValues<double>(llBBoxStr, "grid.llbox", 2);

    if (!llBBox)
      throw Spine::Exception(BCP, "Grid llbox is empty in query result");

    itsBoundingBox.bottomLeft = NFmiPoint((*llBBox)[BOTTOMLEFT].first, (*llBBox)[BOTTOMLEFT].second);
    itsBoundingBox.topRight = NFmiPoint((*llBBox)[TOPRIGHT].first, (*llBBox)[TOPRIGHT].second);

    // Grid size

    auto widthAttr = gridQuery.mAttributeList.getAttribute("grid.width");
    auto heightAttr = gridQuery.mAttributeList.getAttribute("grid.height");

    if ((!widthAttr) || (!heightAttr))
      throw Spine::Exception(BCP, "Grid width/height not set in query result");

    itsReqGridSizeX = Fmi::stoul(widthAttr->mValue.c_str());
    itsReqGridSizeY = Fmi::stoul(heightAttr->mValue.c_str());

    if (vVec.size() != (itsReqGridSizeX * itsReqGridSizeY))
      throw Spine::Exception(BCP, "Grid size (" + Fmi::to_string(vVec.size()) +
                             ") and width/height (" + Fmi::to_string(itsReqGridSizeX) + "/" +
                             Fmi::to_string(itsReqGridSizeY) + ") mismatch");

    // Take stepping (gridstep=dx,dy) into account

    setSteppedGridSize();

    // Set/use constant grid size if size/resolution was not set
    //
    // TODO: set or not ?
    //
    // setGridSize(itsReqGridSizeX, itsReqGridSizeY);

    // Grid resolution

    auto xResolAttr = gridQuery.mAttributeList.getAttribute("grid.cell.width");
    auto yResolAttr = gridQuery.mAttributeList.getAttribute("grid.cell.height");

    if ((!xResolAttr) || (!yResolAttr))
    {
      xResolAttr = gridQuery.mAttributeList.getAttribute("grid.original.cell.width");
      yResolAttr = gridQuery.mAttributeList.getAttribute("grid.original.cell.height");
    }

    if ((!xResolAttr) || (!yResolAttr))
      throw Spine::Exception(BCP, "Grid cell width/height not set in query result");

    itsDX = Fmi::stod(xResolAttr->mValue.c_str());
    itsDY = Fmi::stod(yResolAttr->mValue.c_str());

    // Wind component direction

    auto uvAttr = gridQuery.mAttributeList.getAttribute("grid.original.relativeUV");

    if (uvAttr && (uvAttr->mValue != "0") && (uvAttr->mValue != "1"))
      throw Spine::Exception::Trace(BCP, "grid.original.relativeUV has unknown value");

    itsGridMetaData.relativeUV = (uvAttr && (uvAttr->mValue == "1"));
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Extract grid data
 *
 */
// ----------------------------------------------------------------------

void DataStreamer::extractGridData(string &chunk)
{
  try
  {
    auto theParamsEnd = itsDataParams.end();
    auto theLevelsBegin = itsDataLevels.begin();
    auto theLevelsEnd = itsDataLevels.end();
    auto theTimesBegin = itsDataTimes.begin();
    auto theTimesEnd = itsDataTimes.end();

    // Loop over the parameters.

    for (; (itsParamIterator != theParamsEnd); nextGridParam(),
                                               itsLevelIterator = theLevelsBegin,
                                               itsTimeIterator = theTimesBegin)
    {
      // Loop over the queried levels.

      for (; (itsLevelIterator != theLevelsEnd); itsLevelIterator++, itsTimeIterator = theTimesBegin)
      {
        // Skip times earlier than first available validtime

        auto timeInstant = itsFirstDataTime;

        while (itsTimeIterator != theTimesEnd)
        {
          timeInstant = itsTimeIterator->utc_time();

          if (timeInstant < itsFirstDataTime)
            itsTimeIterator++;
          else
            break;
        }

        // Skip times later than last available validtime

        if ((itsTimeIterator == theTimesEnd) || (timeInstant > itsLastDataTime))
        {
          // Next level or parameter

          continue;
        }

        // Check that the requested level is available in the querydata (exact match or
        // interpolatable).

        int level = *itsLevelIterator;
        bool exactLevel;

        if (!isGridLevelAvailable(level, exactLevel))
          continue;

        QueryServer::Query dataGridQuery,bboxGridQuery;
        QueryServer::Query &gridQuery = bboxGridQuery;

        bool queryBBox = buildGridQuery(gridQuery);

        int result = itsGridEngine->executeQuery(gridQuery);

        if ((result == 0) && queryBBox)
        {
          getGridBBox(gridQuery);

          gridQuery = dataGridQuery;
          buildGridQuery(gridQuery);

          result = itsGridEngine->executeQuery(gridQuery);
        }

        if (result != 0)
        {
          Spine::Exception exception(BCP, "The query server returns an error message!");
          exception.addParameter("Result", std::to_string(result));
          exception.addParameter("Message", QueryServer::getResultString(result));
          throw exception;
        }

        getGridQueryInfo(gridQuery);

        // Load the data chunk from gridQuery
        //
        // Note: With netcdf output the data is taken (and buffered) from the 'itsGridValues' member
        // instead of 'chunk' by the upper level (e.g. the format specific getChunk() method).

/*
        if ((itsGridValues.NX() == 0) || (itsGridValues.NY() == 0))
          throw Spine::Exception(BCP,
                                 "Extract data: internal: Query returned no data for producer '" +
                                     itsReqParams.producer + "'");
*/

        NFmiMetTime mt(itsTimeIterator->utc_time());

        getGridDataChunk(gridQuery, level, mt, chunk);

        // Move to next time instant

        itsTimeIterator++;

        return;
      }  // for queried levels
    }    // for parameters

    // No more data
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

}  // namespace Download
}  // namespace Plugin
}  // namespace SmartMet

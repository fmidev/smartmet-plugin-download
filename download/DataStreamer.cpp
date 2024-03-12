// ======================================================================
/*!
 * \brief SmartMet download service plugin; data streaming
 */
// ======================================================================

#include "DataStreamer.h"
#include "Datum.h"
#include "Plugin.h"
#include <boost/algorithm/string/split.hpp>
#include <boost/date_time/gregorian/gregorian.hpp>
#include <macgyver/DateTime.h>
#include <boost/foreach.hpp>
#include <gis/DEM.h>
#include <gis/LandCover.h>
#include <gis/ProjInfo.h>
#include <gis/SpatialReference.h>
#include <grid-files/identification/GridDef.h>
#include <macgyver/Exception.h>
#include <macgyver/StringConversion.h>
#include <newbase/NFmiAreaFactory.h>
#include <newbase/NFmiEnumConverter.h>
#include <newbase/NFmiMetTime.h>
#include <newbase/NFmiQueryData.h>
#include <newbase/NFmiQueryDataUtil.h>
#include <newbase/NFmiTimeList.h>
#include <sys/types.h>
#include <ogr_geometry.h>
#include <string>
#include <unistd.h>
#include <unordered_set>

static const uint minChunkLengthInBytes = 256 * 256;    // Min length of data chunk to return
static const uint maxChunkLengthInBytes = 2048 * 2048;  // Max length of data chunk to return
static const uint maxMsgChunks = 30;  // Max # of data chunks collected and returned as one chunk
static const uint maxGridQueryBlockSize = 30;  // Max # of grid params/timesteps fetched as a block

using namespace std;

using namespace boost::gregorian;
using namespace boost::posix_time;

namespace SmartMet
{
namespace Plugin
{
namespace Download
{
// ----------------------------------------------------------------------
/*!
 * \brief Chunked data streaming
 */
// ----------------------------------------------------------------------

DataStreamer::DataStreamer(const Spine::HTTP::Request &req,
                           const Config &config,
                           const Query &query,
                           const Producer &producer,
                           const ReqParams &reqParams)
    : Spine::HTTP::ContentStreamer(),
      itsRequest(req),
      itsCfg(config),
      itsQuery(query),
      itsReqParams(reqParams),
      itsProducer(producer),
      itsDoneFlag(false),
      itsChunkLength(min(itsReqParams.chunkSize, maxChunkLengthInBytes)),
      itsMaxMsgChunks(maxMsgChunks),
      itsMetaFlag(true),
      itsReqGridSizeX(0),
      itsReqGridSizeY(0),
      itsProjectionChecked(false),
      itsGridMetaData(this, itsReqParams.producer, (itsReqParams.gridParamBlockSize > 0))
{
  try
  {
    if (itsReqParams.dataSource == GridContent)
    {
      // Limit grid data block size and returned chunk size.
      //
      // By default use small chunk size for grid content data to avoid copying data
      // to chunk buffer

      if (itsReqParams.gridParamBlockSize > maxGridQueryBlockSize)
        itsReqParams.gridParamBlockSize = maxGridQueryBlockSize;
      if (itsReqParams.gridTimeBlockSize > maxGridQueryBlockSize)
        itsReqParams.gridTimeBlockSize = maxGridQueryBlockSize;

      if (itsChunkLength == 0)
        itsChunkLength = minChunkLengthInBytes;
    }
    else if (itsChunkLength == 0)
    {
      // Using legacy chunk size by default

      itsChunkLength = maxChunkLengthInBytes;
    }
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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
    const long maxMinutesInMonth = 31 * minutesInDay;
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
      throw Fmi::Exception(BCP,
                           "Invalid data timestep (" +
                               boost::lexical_cast<string>(itsDataTimeStep) + ") for producer '" +
                               itsReqParams.producer + "'");
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Increment grid iterator
 *
 */
// ----------------------------------------------------------------------

DataStreamer::GridMetaData::GridIterator &DataStreamer::GridMetaData::GridIterator::nextParam()
{
  try
  {
    auto ds = gridMetaData->dataStreamer;
    auto timesEnd = ds->itsDataTimes.end();

    if (ds->itsTimeIterator == timesEnd)
      return *this;

    auto paramsEnd = ds->itsDataParams.end();

    for (ds->itsParamIterator++; (ds->itsParamIterator != paramsEnd); ds->itsParamIterator++)
    {
      if (ds->itsScalingIterator != ds->itsValScaling.end())
        ds->itsScalingIterator++;

      if (ds->itsScalingIterator == ds->itsValScaling.end())
        throw Fmi::Exception(BCP, "GridIterator: internal: No more scaling data");

      ds->paramChanged();

      auto paramKey = gridMetaData->paramKeys.find(ds->itsParamIterator->name());

      // TODO: If parameter metadata is missing, should throw internal error ?

      if (paramKey != gridMetaData->paramKeys.end())
        return *this;
    }

    ds->itsParamIterator = ds->itsDataParams.begin();
    ds->itsScalingIterator =  ds->itsValScaling.begin();

    while (ds->itsTimeIterator != timesEnd)
    {
      ds->itsTimeIterator++;
      ds->itsTimeIndex++;

      if (ds->itsTimeIterator != timesEnd)
      {
        auto timeInstant = ds->itsTimeIterator->utc_time();

        if ((timeInstant >= ds->itsFirstDataTime) && (timeInstant <= ds->itsLastDataTime))
          break;
      }
    }

    // There's only one fixed level (0) to "loop", level is taken from radon parameter name

    ds->itsLevelIterator = ds->itsSortedDataLevels.begin();
    ds->itsLevelIndex = 0;

    return *this;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Increment grid iterator
 *
 */
// ----------------------------------------------------------------------

DataStreamer::GridMetaData::GridIterator &DataStreamer::GridMetaData::GridIterator::operator++()
{
  try
  {
    if (init)
    {
      // Skip first incrementation (incremented before loading 1'st grid)

      init = false;
      return *this;
    }

    if (gridMetaData->queryOrderParam)
      return nextParam();

    auto ds = gridMetaData->dataStreamer;
    auto paramsEnd = ds->itsDataParams.end();

    if (ds->itsParamIterator == paramsEnd)
      return *this;

    auto timesEnd = ds->itsDataTimes.end();

    while (ds->itsTimeIterator != timesEnd)
    {
      ds->itsTimeIterator++;
      ds->itsTimeIndex++;

      if (ds->itsTimeIterator != timesEnd)
      {
        if (gridMetaData->paramGeometries.empty())
          // Fetching function parameters only, looping time intants returned by each query
          //
          break;

        auto timeInstant = ds->itsTimeIterator->utc_time();

        if ((timeInstant >= ds->itsFirstDataTime) && (timeInstant <= ds->itsLastDataTime))
          break;
      }
    }

    if (ds->itsTimeIterator != timesEnd)
      return *this;

    ds->itsGridQuery.mForecastTimeList.clear();

    ds->itsTimeIterator = ds->itsDataTimes.begin();
    ds->itsTimeIndex = 0;

    auto levelsEnd = ds->itsSortedDataLevels.end();

    if (ds->itsLevelIterator != levelsEnd)
    {
      ds->itsLevelIterator++;
      ds->itsLevelIndex++;

      if (ds->itsLevelIterator != levelsEnd)
        return *this;
    }

    ds->itsLevelIterator = ds->itsSortedDataLevels.begin();
    ds->itsLevelIndex = 0;

    for (ds->itsParamIterator++; (ds->itsParamIterator != paramsEnd); ds->itsParamIterator++)
    {
      if (ds->itsScalingIterator != ds->itsValScaling.end())
        ds->itsScalingIterator++;

      if (ds->itsScalingIterator == ds->itsValScaling.end())
        throw Fmi::Exception(BCP, "GridIterator: internal: No more scaling data");

      ds->paramChanged();

      auto paramKey = gridMetaData->paramKeys.find(ds->itsParamIterator->name());

      if (paramKey != gridMetaData->paramKeys.end())
        break;
    }

    return *this;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

DataStreamer::GridMetaData::GridIterator DataStreamer::GridMetaData::GridIterator::operator++(int)
{
  try
  {
    // No need to return pre -value
    //
    // GridIterator pre(*this);

    operator++();
    return *this;

    // return pre;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Check if grid iterator is at end position
 *
 */
// ----------------------------------------------------------------------

bool DataStreamer::GridMetaData::GridIterator::atEnd()
{
  try
  {
    auto ds = gridMetaData->dataStreamer;

    if (gridMetaData->queryOrderParam)
      return (ds->itsTimeIterator == ds->itsDataTimes.end());

    return (ds->itsParamIterator == ds->itsDataParams.end());
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Check if data exists for current grid
 *
 */
// ----------------------------------------------------------------------

bool DataStreamer::GridMetaData::GridIterator::hasData(
    T::GeometryId &geometryId, T::ParamLevelId &gridLevelType, int &level)
{
  try
  {
    auto ds = gridMetaData->dataStreamer;

    gridMetaData->gridOriginTime = gridMetaData->originTime;

    if (ds->itsQuery.isFunctionParameter(ds->itsParamIterator->name(), geometryId, gridLevelType, level))
      // Function parameter is queried without knowing if any source data exists
      //
      return true;

    Fmi::DateTime validTime = ds->itsTimeIterator->utc_time();

    string originTimeStr = ds->itsMultiFile ? gridMetaData->getLatestOriginTime(
                                                  &gridMetaData->gridOriginTime, &validTime)
                                            : to_iso_string(gridMetaData->gridOriginTime);

    if (gridMetaData->gridOriginTime.is_not_a_date_time())
      return false;

    // Check if param/level/otime/validtime data is available

    auto const paramGeom = gridMetaData->paramGeometries.find(ds->itsParamIterator->name());
    if (paramGeom == gridMetaData->paramGeometries.end())
      return false;

    auto const geomLevels = paramGeom->second.find(gridMetaData->geometryId);
    if (geomLevels == paramGeom->second.end())
      return false;

    auto levelTimes = geomLevels->second.begin();
    auto prevLevelTimes = levelTimes;

    // Level interpolation is possible for pressure data only.

    bool gridContent = (ds->itsReqParams.dataSource == GridContent);

    if (! gridContent)
    {
      bool interpolatable =
          (isPressureLevel(ds->itsLevelType) && ds->itsProducer.verticalInterpolation);
      bool exactLevel = isSurfaceLevel(ds->itsLevelType), first = true;

      for (; ((!exactLevel) && (levelTimes != geomLevels->second.end())); first = false, levelTimes++)
      {
        if ((exactLevel = (levelTimes->first == *(ds->itsLevelIterator))))
          break;
        else if (*ds->itsLevelIterator < levelTimes->first)
        {
          // Interpolatable if between data levels, data is interpolatable and interpolation is
          // allowed
          //
          if (!(interpolatable && (!first)))
            return false;

          break;
        }

        prevLevelTimes = levelTimes;
      }

      if (!exactLevel)
        levelTimes = prevLevelTimes;
    }

    auto levelTimesEnd = next(levelTimes);

    for (; levelTimes != levelTimesEnd; levelTimes++)
    {
      auto originTimeTimes = levelTimes->second.find(originTimeStr);

      if ((originTimeTimes == levelTimes->second.end()) ||
          (validTime < Fmi::DateTime::from_iso_string(*(originTimeTimes->second.begin()))) ||
          (validTime > Fmi::DateTime::from_iso_string(*(originTimeTimes->second.rbegin()))))
        return false;
    }

    auto paramLevelId = gridMetaData->paramLevelIds.find(ds->itsParamIterator->name());

    if (paramLevelId == gridMetaData->paramLevelIds.end())
      throw Fmi::Exception(BCP,
                           "Internal error: Parameter level type not in metadata: " +
                               ds->itsParamIterator->name());

    gridLevelType = paramLevelId->second;

    level = (gridContent || isSurfaceLevel(ds->itsLevelType))
             ? prevLevelTimes->first
             : *(ds->itsLevelIterator);

    return true;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Return the latest common origintime
 *
 */
// ----------------------------------------------------------------------

Fmi::DateTime DataStreamer::GridMetaData::selectGridLatestValidOriginTime()
{
  try
  {
    // Check if all parameters have common origintime among the 2 newest origintimes
    //
    // Collect (max) 2 latest origitimes for each grid

    set<std::string> originTimeSet;

    for (auto const &paramGeom : paramGeometries)
      for (auto const &geomLevels : paramGeom.second)
        for (auto const &levelTimes : geomLevels.second)
        {
          auto ot = levelTimes.second.rbegin();

          for (int i = 0; (ot != levelTimes.second.rend()) && (i < 2); ot++, i++)
            originTimeSet.insert(ot->first);
        }

    // Search common origintime among grid's 2 latest origintimes

    long index = -1;

    auto ot = originTimeSet.rbegin();

    for (; ot != originTimeSet.rend(); ot++)
    {
      for (auto const &paramGeom : paramGeometries)
      {
        for (auto const &geomLevels : paramGeom.second)
        {
          for (auto const &levelTimes : geomLevels.second)
          {
            auto otLevel = levelTimes.second.find(*ot);

            if (otLevel != levelTimes.second.end())
            {
              index = levelTimes.second.size() - distance(levelTimes.second.begin(), otLevel);

              // Check if latest data covers the last validtime of 2'nd latest data

              /*
              if ((index == 1) && (levelTimes.second.size() > 1) &&
                  (*(otLevel->second.rbegin()) < *(prev(otLevel)->second.rbegin())))
                index = -1;
              */
            }
            else
              index = -1;

            if ((index < 0) || (index > 2))
            {
              index = -1;
              break;
            }
          }

          if (index < 0)
            break;
        }

        if (index < 0)
          break;
      }

      if (index < 0)
        continue;

      // Erase newer/nonvalid origintimes from metadata

      for (auto &paramGeom : paramGeometries)
        for (auto &geomLevels : paramGeom.second)
          for (auto &levelTimes : geomLevels.second)
          {
            auto otl = levelTimes.second.find(*ot);

            if (otl == levelTimes.second.end())
              throw Fmi::Exception(BCP,
                                   "GridMetaData: internal: Latest origintime not in metadata");

            levelTimes.second.erase(next(otl), levelTimes.second.end());
          }

      auto otp = originTimeParams.find(*ot);
      auto otl = originTimeLevels.find(*ot);
      auto ott = originTimeTimes.find(*ot);

      if ((otp == originTimeParams.end()) || (otl == originTimeLevels.end()) ||
          (ott == originTimeTimes.end()))
        throw Fmi::Exception(BCP,
                             "GridMetaData: internal: Latest origintime not in common metadata");

      originTimeParams.erase(next(otp), originTimeParams.end());
      originTimeLevels.erase(next(otl), originTimeLevels.end());
      originTimeTimes.erase(next(ott), originTimeTimes.end());

      return Fmi::DateTime::from_iso_string(ot->c_str());
    }

    throw Fmi::Exception(BCP, "Data has no common origintime");
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Return the latest origintime or latest origintime covering given validtime
 *
 */
// ----------------------------------------------------------------------

const string &DataStreamer::GridMetaData::getLatestOriginTime(Fmi::DateTime *originTime,
                                                              const Fmi::DateTime *validTime) const
{
  try
  {
    static const string empty("");

    if (originTimeTimes.empty())
      throw Fmi::Exception(BCP, "No data available for producer " + producer);

    auto ott = originTimeTimes.rbegin();

    if (validTime)
    {
      Fmi::DateTime firstTime, lastTime;
      long timeStep;

      for (; ott != originTimeTimes.rend(); ott++)
      {
        getDataTimeRange(ott->first, firstTime, lastTime, timeStep);

        if ((*validTime >= firstTime) && (*validTime <= lastTime))
          break;
      }
    }

    if (originTime)
      *originTime = ((ott == originTimeTimes.rend()) ? Fmi::DateTime() : Fmi::DateTime::from_iso_string(ott->first));

    return ((ott == originTimeTimes.rend()) ? empty : ott->first);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Return validtime range for given origintime or for all data/origintimes
 *
 */
// ----------------------------------------------------------------------

bool DataStreamer::GridMetaData::getDataTimeRange(const std::string &originTimeStr,
                                                  Fmi::DateTime &firstTime,
                                                  Fmi::DateTime &lastTime,
                                                  long &timeStep) const
{
  try
  {
    // If originTime is empty, return validtime range for all data/origintimes

    auto ott =
        originTimeStr.empty() ? originTimeTimes.begin() : originTimeTimes.find(originTimeStr);

    if (ott == originTimeTimes.end())
      return false;

    firstTime = Fmi::DateTime();

    for (; ott != originTimeTimes.end(); ott++)
    {
      auto t = ott->second.begin();

      if (firstTime.is_not_a_date_time())
        firstTime = Fmi::DateTime::from_iso_string(*t);
      lastTime = Fmi::DateTime::from_iso_string(*(ott->second.rbegin()));

      if (++t != ott->second.end())
      {
        auto secondTime = Fmi::DateTime::from_iso_string(*t);
        timeStep = (secondTime - firstTime).minutes();
      }
      else
        timeStep = 60;

      if (!originTimeStr.empty())
        break;
    }

    return true;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Get all validtimes for given origintime or for all data/origintimes
 *
 */
// ----------------------------------------------------------------------

using ValidTimeList = SmartMet::Engine::Querydata::ValidTimeList;

boost::shared_ptr<ValidTimeList> DataStreamer::GridMetaData::getDataTimes(
    const std::string &originTimeStr) const
{
  try
  {
    // If originTime is empty, return validtimes for all data/origintimes

    boost::shared_ptr<ValidTimeList> validTimeList(new ValidTimeList());

    auto ott =
        originTimeStr.empty() ? originTimeTimes.begin() : originTimeTimes.find(originTimeStr);

    for (; ott != originTimeTimes.end(); ott++)
    {
      for (auto tit = ott->second.cbegin(); (tit != ott->second.cend()); tit++)
        validTimeList->push_back(Fmi::DateTime::from_iso_string(*tit));

      if (!originTimeStr.empty())
        break;
    }

    return validTimeList;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Generate list of validtimes for the grid data to be loaded and
 *        set origin-, start- and endtime parameters from data if unset
 *        (they are only used when naming download file)
 *
 */
// ----------------------------------------------------------------------

void DataStreamer::generateGridValidTimeList(Query &query, Fmi::DateTime &oTime, Fmi::DateTime &sTime, Fmi::DateTime &eTime)
{
  try
  {
    if (itsGridMetaData.paramGeometries.empty())
    {
      // Fetching function parameters only, looping time instants returned by each query;
      // set one (special) time to enable initial/first time iterator step to query data, the
      // returned time instants are then iterated

      itsGridMetaData.originTime = oTime;

      checkDataTimeStep(itsReqParams.timeStep);
      itsDataTimes.push_back(Fmi::LocalDateTime(Fmi::LocalDateTime::NOT_A_DATE_TIME));

      return;
    }

    // Use data times if not given in request

    string originTimeStr;
    long timeStep;

    if (oTime.is_not_a_date_time())
    {
      auto latestOriginTimeStr = itsGridMetaData.getLatestOriginTime(&oTime);

      if (!itsMultiFile)
        originTimeStr = latestOriginTimeStr;
    }
    else
    {
      itsMultiFile = false;
      originTimeStr = to_iso_string(oTime);
    }

    itsGridMetaData.originTime = oTime;

    if (!itsGridMetaData.getDataTimeRange(
            originTimeStr, itsFirstDataTime, itsLastDataTime, timeStep))
      throw Fmi::Exception(BCP,
                           "No data available for producer " + itsReqParams.producer +
                               "; ot=" + (originTimeStr.empty() ? "none" : originTimeStr) +
                               ", ft=" + to_iso_string(itsFirstDataTime) +
                               ", lt=" + to_iso_string(itsLastDataTime) + ")");

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

    if ((query.tOptions.mode == TimeSeries::TimeSeriesGeneratorOptions::TimeSteps) &&
        (!hasTimeStep))
      query.tOptions.mode = TimeSeries::TimeSeriesGeneratorOptions::DataTimes;

    if ((query.tOptions.mode == TimeSeries::TimeSeriesGeneratorOptions::DataTimes) ||
        query.tOptions.startTimeData || query.tOptions.endTimeData)
    {
      query.tOptions.setDataTimes(itsGridMetaData.getDataTimes(originTimeStr), false);
    }

    auto tz = itsGeoEngine->getTimeZones().time_zone_from_string(query.timeZone);
    itsDataTimes = TimeSeries::TimeSeriesGenerator::generate(query.tOptions, tz);

    if (itsDataTimes.empty())
      throw Fmi::Exception(BCP, "No valid times in the requested time period").disableStackTrace();
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Generate list of validtimes for the data to be loaded and
 *        set origin-, start- and endtime parameters from data if unset
 *        (they are only used when naming download file)
 *
 */
// ----------------------------------------------------------------------

void DataStreamer::generateValidTimeList(
    const Engine::Querydata::Q &q, Fmi::DateTime &oTime, Fmi::DateTime &sTime, Fmi::DateTime &eTime)
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
      sTime = itsQuery.tOptions.startTime = itsQ->validTime();

    itsQ->lastTime();
    itsLastDataTime = itsQ->validTime();
    itsQ->firstTime();

    if (eTime.is_not_a_date_time())
      eTime = itsQuery.tOptions.endTime = itsLastDataTime;

    // Generate list of validtimes for the data to be loaded.
    //
    // Note: Mode must be changed from TimeSteps to DataTimes if timestep was not given (we don't
    // use the default).

    bool hasTimeStep = (itsQuery.tOptions.timeStep && (*itsQuery.tOptions.timeStep > 0));

    if ((itsQuery.tOptions.mode == TimeSeries::TimeSeriesGeneratorOptions::TimeSteps) &&
        (!hasTimeStep))
      itsQuery.tOptions.mode = TimeSeries::TimeSeriesGeneratorOptions::DataTimes;

    if ((itsQuery.tOptions.mode == TimeSeries::TimeSeriesGeneratorOptions::DataTimes) ||
        itsQuery.tOptions.startTimeData || itsQuery.tOptions.endTimeData)
    {
      itsQuery.tOptions.setDataTimes(q->validTimes(), q->isClimatology());
    }

    auto tz = itsGeoEngine->getTimeZones().time_zone_from_string(itsQuery.timeZone);
    itsDataTimes = TimeSeries::TimeSeriesGenerator::generate(itsQuery.tOptions, tz);

    if (itsDataTimes.empty())
      throw Fmi::Exception(BCP, "No valid times in the requested time period").disableStackTrace();
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Sort (requested or all available) data levels
 *
 */
// ----------------------------------------------------------------------

void DataStreamer::sortLevels()
{
  // Source data level order is needed for qd -output (for qd source)

  for (auto it = itsDataLevels.begin(); (it != itsDataLevels.end()); it++)
    itsSortedDataLevels.push_back(*it);

  if (!itsRisingLevels)
    itsSortedDataLevels.sort(std::greater<int>());
}

// ----------------------------------------------------------------------
/*!
 * \brief Set levels from request parameter(s) or from data if none was given.
 *
 */
// ----------------------------------------------------------------------

void DataStreamer::setGridLevels(const Producer &producer, const Query &query)
{
  try
  {
    if (itsReqParams.dataSource == GridContent)
    {
      // Set level 0, parameter specific level is used when fetching or storing parameter data

      itsDataLevels.insert(0);
      return;
    }

    Query::Levels allLevels;

    // Fetching level/height range ?

    itsLevelRng = ((!isSurfaceLevel(itsLevelType)) &&
                   ((itsReqParams.minLevel >= 0) || (itsReqParams.maxLevel > 0)));
    itsHeightRng = ((!isSurfaceLevel(itsLevelType)) &&
                    ((itsReqParams.minHeight >= 0) || (itsReqParams.maxHeight > 0)));

    bool noLevelsGiven = (query.levels.begin() == query.levels.end());
    auto queryLevels = noLevelsGiven ? producer.gridDefaultLevels : query.levels;

    Query::Levels &levels =
        ((queryLevels.begin() == queryLevels.end()) && ((!itsLevelRng) && (!itsHeightRng)))
            ? itsDataLevels
            : allLevels;

    auto metaDataLevels = itsGridMetaData.originTimeLevels.begin()->second;
    levels.insert(metaDataLevels.begin(), metaDataLevels.end());

    itsRisingLevels = true;

    if (isSurfaceLevel(itsLevelType))
      // Surface data; set only level 0 (ignoring user input).
      // Parameter specific level is used when fetching or storing parameter data
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
          itsDataLevels.insert(*it);
    }

    sortLevels();
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Set levels from request parameter(s) or from data if none was given.
 *		Check querydata(s) level types and time step.
 *
 */
// ----------------------------------------------------------------------

void DataStreamer::setLevels()
{
  try
  {
    Query::Levels allLevels;

    auto q = itsQ;

    // Level type

    itsLevelType =
        getLevelTypeFromData(q, itsReqParams.producer, itsNativeLevelType, itsPositiveLevels);

    // Fetching level/height range ?

    itsLevelRng = ((!isSurfaceLevel(itsLevelType)) &&
                   ((itsReqParams.minLevel >= 0) || (itsReqParams.maxLevel > 0)));
    itsHeightRng = ((!isSurfaceLevel(itsLevelType)) &&
                    ((itsReqParams.minHeight >= 0) || (itsReqParams.maxHeight > 0)));

    Query::Levels &levels =
        ((itsQuery.levels.begin() == itsQuery.levels.end()) && ((!itsLevelRng) && (!itsHeightRng)))
            ? itsDataLevels
            : allLevels;

    for (q->resetLevel(); q->nextLevel();)
      // Note: The level values are stored unsigned; negative values are used when necessary
      // when getting the data (when querying height level data with negative levels).
      //
      levels.insert(boost::numeric_cast<int>(abs(q->levelValue())));

    itsRisingLevels = areLevelValuesInIncreasingOrder(q);

    if (isSurfaceLevel(itsLevelType))
      // Surface data; set exactly one level (ignoring user input), value does not matter
      //
      itsDataLevels.insert(0);
    else
    {
      // If no levels/heights were given, using all querydata levels

      if (itsQuery.levels.begin() == itsQuery.levels.end())
      {
        if (itsLevelRng || itsHeightRng)
          for (int l = itsReqParams.minLevel; (l <= itsReqParams.maxLevel); l++)
            itsDataLevels.insert(l);
      }
      else
        itsDataLevels = itsQuery.levels;
    }

    sortLevels();
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Store unique data parameter names.
 *
 */
// ----------------------------------------------------------------------

void DataStreamer::setParams(const TimeSeries::OptionParsers::ParameterList &params,
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
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Get parameter details from grid content data
 *
 */
// ----------------------------------------------------------------------

void DataStreamer::getParameterDetailsFromContentData(
    const string &paramName, SmartMet::Engine::Grid::ParameterDetails_vec &parameterDetails)
{
  try
  {
    auto const &paramContents = itsQuery.getParameterContents();
    auto const &paramContent = paramContents.find(paramName);

    if ((paramContent == paramContents.end()) || (paramContent->second.getLength() == 0))
      return;

    auto const &generationInfos = itsQuery.getGenerationInfos();
    auto const &contentInfoList = paramContent->second;
    auto contentInfo = contentInfoList.getContentInfoByIndex(0);
    auto const contentLength = contentInfoList.getLength();

    // Ignore too old content

    auto generationInfo = generationInfos.find(contentInfo->mGenerationId);

    if (generationInfo == generationInfos.end())
      throw Fmi::Exception(
          BCP, "getParameterDetailsFromContentData: internal: generationId not found");

    if (!isValidGeneration(&(generationInfo->second)))
      return;

    vector<string> paramParts;
    itsQuery.parseRadonParameterName(paramName, paramParts);

    const string &param = paramParts[0];
    const string &producer = paramParts[1];

    typedef map<T::GeometryId, SmartMet::Engine::Grid::ParameterDetails_vec> GeomDetails;
    typedef map<T::ParamLevel, GeomDetails> LevelDetails;
    typedef map<T::ParamLevelId, LevelDetails> LevelTypeDetails;
    LevelTypeDetails levelGeomParamDetails;

    for (size_t idx = 0; (idx < contentLength); idx++)
    {
      contentInfo = contentInfoList.getContentInfoByIndex(idx);

      // Level type, level and geometry must be given in parameter name, but could collect data
      // for multiple level types, levels and geometries

      auto levelTypeIter = levelGeomParamDetails.find(contentInfo->mFmiParameterLevelId);

      if (levelTypeIter == levelGeomParamDetails.end())
        levelTypeIter = levelGeomParamDetails.insert(make_pair(
            contentInfo->mFmiParameterLevelId, LevelDetails())).first;

      auto levelIter = levelTypeIter->second.find(contentInfo->mParameterLevel);

      if (levelIter == levelTypeIter->second.end())
      levelIter = levelTypeIter->second.insert(make_pair(
          contentInfo->mParameterLevel, GeomDetails())).first;

      auto geomIter = levelIter->second.find(contentInfo->mGeometryId);

      if (geomIter == levelIter->second.end())
      {
        geomIter = levelIter->second.insert(make_pair(
            contentInfo->mGeometryId, SmartMet::Engine::Grid::ParameterDetails_vec())).first;

        SmartMet::Engine::Grid::ParameterDetails pd;

        pd.mProducerName = producer;
        pd.mGeometryId = Fmi::to_string(contentInfo->mGeometryId);
        pd.mLevelId = Fmi::to_string(contentInfo->mFmiParameterLevelId);
        pd.mLevel = Fmi::to_string(contentInfo->mParameterLevel);
        pd.mForecastType = Fmi::to_string(contentInfo->mForecastType);
        pd.mForecastNumber = Fmi::to_string(contentInfo->mForecastNumber);

        SmartMet::Engine::Grid::MappingDetails mappingDetails;

        mappingDetails.mMapping.mProducerName = producer;
        mappingDetails.mMapping.mParameterName = param;
        mappingDetails.mMapping.mParameterKey = contentInfo->mFmiParameterId;
        mappingDetails.mMapping.mGeometryId = contentInfo->mGeometryId;
        mappingDetails.mMapping.mParameterLevelId = contentInfo->mFmiParameterLevelId;
        mappingDetails.mMapping.mParameterLevel = contentInfo->mParameterLevel;

        pd.mMappings.push_back(mappingDetails);

        geomIter->second.push_back(pd);
      }

      auto timeIter = geomIter->second.front().mMappings.front().mTimes.find(
          generationInfo->second.mAnalysisTime);

      if (timeIter == geomIter->second.front().mMappings.front().mTimes.end())
        timeIter = geomIter->second.front().mMappings.front().mTimes.insert(make_pair(
            generationInfo->second.mAnalysisTime, set<string>())).first;

      timeIter->second.insert(contentInfo->getForecastTime());
    }

    // Return details for first (only) leveltype, level and geometry

    if (!levelGeomParamDetails.empty())
      parameterDetails.insert(
          parameterDetails.begin(),
          levelGeomParamDetails.begin()->second.begin()->second.begin()->second.begin(),
          levelGeomParamDetails.begin()->second.begin()->second.begin()->second.end());
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Check if (any) requested grid data is available.
 *        Collects available levels and validtimes.
 *
 */
// ----------------------------------------------------------------------

bool DataStreamer::hasRequestedGridData(
    const Producer &producer, Fmi::DateTime &oTime, Fmi::DateTime &sTime, Fmi::DateTime &eTime)
{
  try
  {
    // Check if any of the requested parameters and levels exist or is interpolatable.

    Engine::Grid::Times validTimes;
    string originTimeStr(oTime.is_not_a_date_time() ? "" : to_iso_string(oTime));
    string forecastType, forecastNumber;
    size_t nMissingParam = 0;
    bool gridContent = (itsReqParams.dataSource == GridContent), hasFuncParam = false;

    if (gridContent)
      itsReqParams.producer.clear();

    for (auto const &param : itsDataParams)
    {
      if (itsQuery.isFunctionParameter(param.name()))
      {
        hasFuncParam = true;
        continue;
      }

      SmartMet::Engine::Grid::ParameterDetails_vec paramDetails;

      if (gridContent)
        getParameterDetailsFromContentData(param.name(), paramDetails);
      else
        itsGridEngine->getParameterDetails(itsReqParams.producer, param.name(), paramDetails);

      for (auto const &paramDetail : paramDetails)
      {
        string paramKey = (gridContent ? param.name() : itsReqParams.producer + ";" + param.name());

        if (!gridContent)
        {
          if (
              // No mappings found ?
              //
              (strcasecmp(paramDetail.mProducerName.c_str(), paramKey.c_str()) == 0) ||
              (
               // TODO: Currently selecting data matching first forecasttype/number
               //
               (itsGridMetaData.paramLevelId != GridFmiLevelTypeNone) &&
               (
                (paramDetail.mForecastType != forecastType) ||
                (paramDetail.mForecastNumber != forecastNumber))
              )
             )
            continue;

          if (&paramDetail == &(paramDetails.front()))
            itsGridEngine->mapParameterDetails(paramDetails);

          paramKey.clear();
        }

        T::ParamLevelId paramLevelId = GridFmiLevelTypeNone;
        bool hasParam = false;

        for (auto const &paramMapping : paramDetail.mMappings)
        {
          auto const &pm = paramMapping.mMapping;

          FmiLevelType mappingLevelType = FmiLevelType(pm.mParameterLevelId);
          int level;

          if ((!gridContent) && (pm.mParameterLevelId == GridFmiLevelTypePressure))
            level = pm.mParameterLevel * 0.01;  // e.g. levels=850
          else
            level = pm.mParameterLevel;         // e.g. param=T-K::1093:2:85000:...

          if (!gridContent)
          {
            // Check for supported level type

            if (
                (pm.mParameterLevelId != GridFmiLevelTypeGround) &&
                (pm.mParameterLevelId != GridFmiLevelTypePressure) &&
                (pm.mParameterLevelId != GridFmiLevelTypeHybrid) &&
                (pm.mParameterLevelId != GridFmiLevelTypeHeight) &&
                (pm.mParameterLevelId != GridFmiLevelTypeDepth) &&
                (pm.mParameterLevelId != GridFmiLevelTypeEntireAtmosphere)
               )
              continue;

            // Check if level is requested by the query

            if ((pm.mParameterLevelId == GridFmiLevelTypeGround) ||
                (pm.mParameterLevelId == GridFmiLevelTypeHeight) ||
                (pm.mParameterLevelId == GridFmiLevelTypeEntireAtmosphere))
              mappingLevelType = kFmiGroundSurface;
            else if (pm.mParameterLevelId == GridFmiLevelTypePressure)
              mappingLevelType = kFmiPressureLevel;
            else if (pm.mParameterLevelId == GridFmiLevelTypeHybrid)
              mappingLevelType = kFmiHybridLevel;
            else
              mappingLevelType = kFmiDepth;

            if (!isGridLevelRequested(producer, itsQuery, mappingLevelType, level))
              continue;

            if (paramKey.empty())
              paramKey = pm.mParameterName + ":" + pm.mProducerName;

            if (itsGridMetaData.paramLevelId != GridFmiLevelTypeNone)
            {
              if (itsReqParams.dataSource == GridMapping)
              {
                // TODO: Currently selecting data matching first geometry

                if (pm.mGeometryId != itsGridMetaData.geometryId)
                  continue;
              }

              // Parameter must not change (e.g. Temperature, TEMPERATURE)
              //
              // TODO: Currently selecting data matching first parameter
              //
              auto pKey = pm.mParameterName + ":" + pm.mProducerName;

              if (pKey != paramKey)
                continue;
                /*
                throw Fmi::Exception(BCP,
                                     "GridMetaData: Multiple mappings: " + param.name() + ": " +
                                         paramKey + "," + pKey);
                */

              // Level type must not change, except allow ground and height (e.g. 2m) above ground
              //
              // clang-format off
              else if (
                       (
                        (paramLevelId != GridFmiLevelTypeNone) &&
                        (pm.mParameterLevelId != paramLevelId)
                       ) ||
                       (
                        (pm.mParameterLevelId != itsGridMetaData.paramLevelId) &&
                        (pm.mParameterLevelId != GridFmiLevelTypeGround) &&
                        (pm.mParameterLevelId != GridFmiLevelTypeHeight) &&
                        (pm.mParameterLevelId != GridFmiLevelTypeEntireAtmosphere) &&
                        (itsGridMetaData.paramLevelId != GridFmiLevelTypeGround) &&
                        (itsGridMetaData.paramLevelId != GridFmiLevelTypeHeight) &&
                        (itsGridMetaData.paramLevelId != GridFmiLevelTypeEntireAtmosphere)
                       )
                      )
              {
                string itsLevelTypeId = (paramLevelId != GridFmiLevelTypeNone)
                                            ? "," + Fmi::to_string(paramLevelId)
                                            : "";

                throw Fmi::Exception(BCP,
                                     "GridMetaData: Multiple leveltypes: " + param.name() + "," +
                                         Fmi::to_string(pm.mParameterLevelId) + itsLevelTypeId +
                                         "," + Fmi::to_string(itsGridMetaData.paramLevelId));
              }
              // clang-format on
            }
          }

          // Collect origintimes and available parameters, times and levels for each of them

          if (paramMapping.mTimes.empty())
            throw Fmi::Exception(BCP, "GridMetaData: Mapping with no times: " + param.name());

          forecastType = paramDetail.mForecastType;
          forecastNumber = paramDetail.mForecastNumber;

          for (auto const &dataTimes : paramMapping.mTimes)
          {
            if ((!originTimeStr.empty()) && (originTimeStr != dataTimes.first))
              continue;
            else if (dataTimes.second.empty())
              throw Fmi::Exception(BCP,
                                   "GridMetaData: Mapping with no validtimes: " + param.name());

            if (itsGridMetaData.paramLevelId == GridFmiLevelTypeNone)
            {
              // With radon parameters leveltype, level and geometry are taken from name.
              //
              // Since metadata's paramLevelId (grid level type) is tested later against None
              // to check if data is available, set it from 1'st parameter

              itsGridMetaData.paramLevelId = pm.mParameterLevelId;
              itsGridMetaData.geometryId = pm.mGeometryId;

              itsLevelType = mappingLevelType;
            }

            if (paramLevelId == GridFmiLevelTypeNone)
              paramLevelId = pm.mParameterLevelId;

            using GeometryLevels = GridMetaData::GeometryLevels;
            using LevelOriginTimes = GridMetaData::LevelOriginTimes;
            using OriginTimeTimes = GridMetaData::OriginTimeTimes;

            auto paramGeom = itsGridMetaData.paramGeometries.insert(
                make_pair(param.name(), GeometryLevels()));
            auto geomLevels = paramGeom.first->second.insert(
                make_pair(itsGridMetaData.geometryId, LevelOriginTimes()));
            auto levelTimes =
                geomLevels.first->second.insert(make_pair(level, OriginTimeTimes()));
            auto originTimes =
                levelTimes.first->second.insert(make_pair(dataTimes.first, set<string>()));
            originTimes.first->second.insert(dataTimes.second.begin(), dataTimes.second.end());

            auto otp = itsGridMetaData.originTimeParams.insert(
                make_pair(dataTimes.first, set<string>()));
            otp.first->second.insert(param.name());

            // Store level 0 for surface data for level iteration; parameter specific
            // level is used when fetching or storing parameter data

            bool surfaceLevel = (gridContent ? false : isSurfaceLevel(itsLevelType));

            auto otl = itsGridMetaData.originTimeLevels.insert(
                make_pair(dataTimes.first, set<T::ParamLevel>()));
            auto levels = otl.first->second.insert(surfaceLevel ? 0 : level);

            (void) levels;
            /*
            TODO: Why this check for origintime scoped data, should be at param level if at all ?

            if ((!levels.second) && (!surfaceLevel))
              throw Fmi::Exception(BCP,
                                   "GridMetaData: Duplicate level; " + param.name() + "," +
                                       Fmi::to_string(level));
            */

            auto ott = itsGridMetaData.originTimeTimes.insert(
                make_pair(dataTimes.first, set<string>()));
            ott.first->second.insert(dataTimes.second.begin(), dataTimes.second.end());

            hasParam = true;
          }
        }

        if (hasParam)
        {
          // The first valid detail is selected when using newbase parameter names.
          // With radon names there is only 1 detail set from content records.
          //
          // Store producer from 1'st radon parameter, it is used when naming output file

          itsGridMetaData.paramKeys.insert(make_pair(param.name(), paramKey));
          itsGridMetaData.paramLevelIds.insert(make_pair(param.name(), paramLevelId));

          if (gridContent && itsReqParams.producer.empty())
            itsReqParams.producer = paramDetail.mProducerName;

          break;
        }
      }

      // Count leading missing parameters and erase their scaling information

      if (itsGridMetaData.paramLevelId == GridFmiLevelTypeNone)
      {
        nMissingParam++;

        if (!itsValScaling.empty())
          itsValScaling.pop_front();
        else
          throw Fmi::Exception(BCP, "GridMetaData: internal: No more scaling data");
      }
    }

    if ((!hasFuncParam) && itsGridMetaData.paramGeometries.empty())
      return false;

    // Erase leading missing parameters

    if (nMissingParam > 0)
      itsDataParams.erase(itsDataParams.begin(), itsDataParams.begin() + nMissingParam);

    // If origintime is not given and fetching at least one data parameter, select latest
    // common valid origintime from metadata

    if (originTimeStr.empty() && (!itsGridMetaData.paramGeometries.empty()))
      itsGridMetaData.selectGridLatestValidOriginTime();

    // Generate list of validtimes for the data to be loaded.

    generateGridValidTimeList(itsQuery, oTime, sTime, eTime);

    // Set request levels

    setGridLevels(producer, itsQuery);

    return resetDataSet();
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Check if (any) requested data is available.
 *
 */
// ----------------------------------------------------------------------

bool DataStreamer::hasRequestedData(
    const Producer &producer, Fmi::DateTime &originTime, Fmi::DateTime &startTime, Fmi::DateTime &endTime)
{
  try
  {
    if (itsReqParams.dataSource != QueryData)
      return hasRequestedGridData(producer, originTime, startTime, endTime);

    auto q = itsQ;
    bool hasData = false;

    // Get grid's origo

    if (!q->isGrid())
      throw Fmi::Exception(BCP, "Nongrid data for producer + '" + itsReqParams.producer + "'");

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

      if (itsReqParams.outputFormat != QD)
      {
        if (!itsValScaling.empty())
          itsValScaling.pop_front();
        else
          throw Fmi::Exception(BCP, "Internal error in skipping missing parameters");
      }
    }

    if (!hasData)
      return false;

    // Erase leading missing parameters. The first parameter in 'itsDataParams' must be the first
    // valid/existent parameter after the initialization phase.
    // At the end of initialization parameter iterator is set to the start of 'itsDataParams', which
    // must be the same as itsQ's current parameter set in loop above; data chunking starts with it

    if (nMissingParam > 0)
      itsDataParams.erase(itsDataParams.begin(), itsDataParams.begin() + nMissingParam);

    // Check if any of the requested levels exist or is interpolatable.

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
        else if (!isSurfaceLevel(itsLevelType))
        {
          if (level != queryLevel)
          {
            if (queryLevel > level)
              if (itsRisingLevels)
                continue;
              else
              {
                if (first || (!isPressureLevel(itsLevelType)) || (!producer.verticalInterpolation))
                  break;
              }
            else if (itsRisingLevels)
            {
              if (first || (!isPressureLevel(itsLevelType)) || (!producer.verticalInterpolation))
                break;
            }
            else
              continue;
          }
        }

        return resetDataSet();
      }  // for available levels
    }    // for queried levels

    return false;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Parse bbox from input
 */
// ----------------------------------------------------------------------

void DataStreamer::getBBox(const std::string &bbox)
{
  try
  {
    std::vector<std::string> parts;
    boost::algorithm::split(parts, bbox, boost::algorithm::is_any_of(","));
    if (parts.size() != 4)
      throw Fmi::Exception(BCP, "bbox must contain four comma separated values");

    NFmiPoint bottomLeft{std::stod(parts[0]), std::stod(parts[1])};
    NFmiPoint topRight{std::stod(parts[2]), std::stod(parts[3])};
    itsRegBoundingBox = BBoxCorners{bottomLeft, topRight};
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Failed to parse bbox '" + bbox + "'");
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
    // Note: Q object latLonCache is in WGS84, but what we are looking for is the native
    // bounding box. Alternatively anyone using this method should switch to using WGS84
    // bboxes.

    double blLon = 0.0, blLat = 0.0, trLon = 0.0, trLat = 0.0;
    std::size_t gridSizeX = q->grid().XNumber();
    std::size_t gridSizeY = q->grid().YNumber();

    // Loop all columns of first and last row and first and last columns of other rows.

    for (std::size_t y = 1, n = 0, dx = (gridSizeX - 1); (y <= gridSizeY); y++, n++)
      for (std::size_t x = 1; (x <= gridSizeX);)
      {
        const NFmiPoint &p = q->latLon(n);

        auto px = p.X(), py = p.Y();

        if (n == 0)
        {
          blLon = trLon = px;
          blLat = trLat = py;
        }
        else
        {
          blLon = std::min(px, blLon);
          trLon = std::max(px, trLon);
          blLat = std::min(py, blLat);
          trLat = std::max(py, trLat);
        }

        size_t dn = (((y == 1) || (y == gridSizeY)) ? 1 : dx);

        x += dn;
        if (x <= gridSizeX)
          n += dn;
      }

    itsRegBoundingBox = BBoxCorners{NFmiPoint(blLon, blLat), NFmiPoint(trLon, trLat)};
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Get projected or latlon source area bbox for target projection
 *
 */
// ----------------------------------------------------------------------

void DataStreamer::getBBox(Engine::Querydata::Q q,
                           const NFmiArea &sourceArea,
                           OGRSpatialReference &targetSRS)
{
  try
  {
    OGRSpatialReference sourceSRS;
    OGRErr err;

    if ((err = sourceSRS.SetFromUserInput(sourceArea.ProjStr().c_str())) != OGRERR_NONE)
      throw Fmi::Exception(BCP, "srs.Set(ProjStr) error " + boost::lexical_cast<string>(err));

    sourceSRS.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);

    OGRCoordinateTransformation *ct =
        itsResources.getCoordinateTransformation(&sourceSRS, &targetSRS);
    if (!ct)
      throw Fmi::Exception(BCP, "OGRCreateCoordinateTransformation failed");

    // Loop all columns of first and last row and first and last columns of other rows.

    const auto worldRect = sourceArea.WorldRect();
    const auto &grid = q->grid();
    size_t gridSizeX = grid.XNumber(), x;
    size_t gridSizeY = grid.YNumber(), y;
    auto dX = (worldRect.Right() - worldRect.Left()) / (gridSizeX - 1);
    auto dY = (worldRect.Top() - worldRect.Bottom()) / (gridSizeY - 1);
    double blX = 0.0, blY = 0.0, trX = 0.0, trY = 0.0, xc, yc;
    bool first = true;

    for (y = 1, yc = worldRect.Bottom(); (y <= gridSizeY); y++, yc += dY)
      for (x = 1, xc = worldRect.Left(); (x <= gridSizeX);)
      {
        double txc = xc, tyc = yc;

        if (!(ct->Transform(1, &txc, &tyc)))
          throw Fmi::Exception(BCP, "Transform failed");

        if (first)
        {
          blX = trX = txc;
          blY = trY = tyc;

          first = false;
        }
        else
        {
          blX = std::min(txc, blX);
          trX = std::max(txc, trX);
          blY = std::min(tyc, blY);
          trY = std::max(tyc, trY);
        }

        x += (((y == 1) || (y == gridSizeY)) ? 1 : gridSizeX);
        xc = (((y == 1) || (y == gridSizeY)) ? xc + dX : worldRect.Right());
      }

    itsBoundingBox = BBoxCorners{NFmiPoint(blX, blY), NFmiPoint(trX, trY)};
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Get projected or latlon source area bbox or requested bbox for target projection
 *
 */
// ----------------------------------------------------------------------

void DataStreamer::getBBox(Engine::Querydata::Q q,
                           const NFmiArea &sourceArea,
                           OGRSpatialReference &targetSRS,
                           OGRSpatialReference *targetLLSRS)
{
  try
  {
    if (itsReqParams.bbox.empty() && itsReqParams.gridCenter.empty())
    {
      getBBox(q, sourceArea, targetSRS);
      return;
    }

    OGRCoordinateTransformation *LL2Prct = nullptr;

    if (targetLLSRS)
    {
      if (!(LL2Prct = itsResources.getCoordinateTransformation(targetLLSRS, &targetSRS)))
        throw Fmi::Exception(BCP, "OGRCreateCoordinateTransformation failed");
    }

    double blX, blY, trX, trY;

    if (itsReqParams.bbox.empty())
    {
      if (!targetLLSRS)
        throw Fmi::Exception(BCP, "gridcenter not supported with geographic epsg cs");

      double xc = (*itsReqParams.gridCenterLL)[0].first;
      double yc = (*itsReqParams.gridCenterLL)[0].second;

      if (!(LL2Prct->Transform(1, &xc, &yc)))
        throw Fmi::Exception(BCP, "Transform failed");

      double width = (*itsReqParams.gridCenterLL)[1].first;
      double height = (*itsReqParams.gridCenterLL)[1].second;

      blX = xc - (width / 2);
      blY = yc - (height / 2);
      trX = xc + (width / 2);
      trY = yc + (height / 2);
    }
    else
    {
      getBBox(itsReqParams.bbox);

      blX = (*itsRegBoundingBox).bottomLeft.X();
      blY = (*itsRegBoundingBox).bottomLeft.Y();
      trX = (*itsRegBoundingBox).topRight.X();
      trY = (*itsRegBoundingBox).topRight.Y();

      if (targetLLSRS)
      {
        double c[] = {blX, blY, blX, trY, trX, trY, trX, blY};
        double *x = c, *y = c + 1;

        for (int i = 0; (i < 4); i++, x += 2, y += 2)
        {
          if (!(LL2Prct->Transform(1, x, y)))
            throw Fmi::Exception(BCP, "Transform failed");

          if (i == 0)
          {
            blX = trX = *x;
            blY = trY = *y;
          }
          else
          {
            if (*x < blX)
              blX = *x;
            else if (*x > trX)
              trX = *x;

            if (*y < blY)
              blY = *y;
            else if (*y > trY)
              trY = *y;
          }
        }
      }
    }

    itsBoundingBox = BBoxCorners{NFmiPoint(blX, blY), NFmiPoint(trX, trY)};
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Get source area latlon bbox for target projection
 *
 */
// ----------------------------------------------------------------------

void DataStreamer::getRegLLBBox(Engine::Querydata::Q q,
                                const NFmiArea &sourceArea,
                                OGRSpatialReference &targetSRS)
{
  try
  {
    targetSRS.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);

    getBBox(q, sourceArea, targetSRS);

    if (targetSRS.IsProjected())
    {
      OGRSpatialReference *targetLLSRS = itsResources.cloneGeogCS(targetSRS);

      targetLLSRS->SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);

      OGRCoordinateTransformation *ct =
          itsResources.getCoordinateTransformation(&targetSRS, targetLLSRS);

      if (!ct)
        throw Fmi::Exception(BCP, "OGRCreateCoordinateTransformation failed");

      double blLon = itsBoundingBox.bottomLeft.X();
      double blLat = itsBoundingBox.bottomLeft.Y();
      double trLon = itsBoundingBox.topRight.X();
      double trLat = itsBoundingBox.topRight.Y();

      if ((!(ct->Transform(1, &blLon, &blLat))) || (!(ct->Transform(1, &trLon, &trLat))))
        throw Fmi::Exception(BCP, "Transform failed");

      itsBoundingBox = BBoxCorners{NFmiPoint(blLon, blLat), NFmiPoint(trLon, trLat)};
    }
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Get latlon bbox string for (projected) target projection
 *
 */
// ----------------------------------------------------------------------

string DataStreamer::getRegLLBBoxStr(Engine::Querydata::Q q,
                                     const NFmiArea &sourceArea,
                                     const string &targetProjection)
{
  try
  {
    OGRSpatialReference targetSRS;
    OGRErr err;

    auto targetArea = itsResources.createArea(targetProjection);

    if ((err = targetSRS.importFromWkt(targetArea->WKT().c_str())) != OGRERR_NONE)
      throw Fmi::Exception(
          BCP,
          "srs.importFromWKT(" + targetArea->WKT() + ") error " + boost::lexical_cast<string>(err));

    getRegLLBBox(q, sourceArea, targetSRS);

    ostringstream os;
    os << fixed << setprecision(8) << itsBoundingBox.bottomLeft.X() << ","
       << itsBoundingBox.bottomLeft.Y() << "," << itsBoundingBox.topRight.X() << ","
       << itsBoundingBox.topRight.Y();

    return os.str();
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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
    size_t xCnt = (itsCropping.cropped ? itsCropping.gridSizeX : itsReqGridSizeX),
           yCnt = (itsCropping.cropped ? itsCropping.gridSizeY : itsReqGridSizeY);
    size_t xStep = (itsReqParams.gridStepXY ? (*(itsReqParams.gridStepXY))[0].first : 1),
           yStep = (itsReqParams.gridStepXY ? (*(itsReqParams.gridStepXY))[0].second : 1);

    itsNX = xCnt / xStep;
    itsNY = yCnt / yStep;

    if (xStep > 1)
    {
      if (xCnt % xStep)
        itsNX++;

      if (itsCropping.cropped)
      {
        itsCropping.topRightX = itsCropping.bottomLeftX + ((itsNX - 1) * xStep);
        itsCropping.gridSizeX = ((itsCropping.topRightX - itsCropping.bottomLeftX) + 1);
      }
    }

    if (yStep > 1)
    {
      if (yCnt % yStep)
        itsNY++;

      if (itsCropping.cropped)
      {
        itsCropping.topRightY = itsCropping.bottomLeftY + ((itsNY - 1) * yStep);
        itsCropping.gridSizeY = ((itsCropping.topRightY - itsCropping.bottomLeftY) + 1);
      }
    }

    if ((itsNX < 2) || (itsNY < 2))
      throw Fmi::Exception(BCP, "Minimum gridsize is 2x2, adjust bbox and/or gridstep");
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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
      // divisor was multiplied by 1000 before WGS84 change
      //
      // TODO: check multiplication despite the comment above
      //
      gridSizeX = boost::numeric_cast<size_t>(
          fabs(ceil(area.WorldXYWidth() / ((*itsReqParams.gridResolutionXY)[0].first * 1000))));
      gridSizeY = boost::numeric_cast<size_t>(
          fabs(ceil(area.WorldXYHeight() / ((*itsReqParams.gridResolutionXY)[0].second * 1000))));

      if ((gridSizeX <= 1) || (gridSizeY <= 1))
        throw Fmi::Exception(BCP, "Invalid gridsize for producer '" + itsReqParams.producer + "'")
            .addParameter("xsize", Fmi::to_string(gridSizeX))
            .addParameter("ysize", Fmi::to_string(gridSizeY));

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
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Get projection string for gridcenter bounding.
 *
 */
// ----------------------------------------------------------------------

std::string DataStreamer::getGridCenterBBoxStr() const
{
  try
  {
    ostringstream os;

    os << fixed << setprecision(8) << (*itsReqParams.gridCenterLL)[0].first << ","
       << (*itsReqParams.gridCenterLL)[0].second << ",1|" << (*itsReqParams.gridCenterLL)[1].first
       << "," << (*itsReqParams.gridCenterLL)[1].second;

    return os.str();
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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
    NFmiPoint bl;
    NFmiPoint tr;
    string bboxStr;

    if (itsReqParams.gridCenterLL)
    {
      const auto &gridcenter = *itsReqParams.gridCenterLL;

      NFmiPoint center(gridcenter[0].first, gridcenter[0].second);
      auto width = gridcenter[1].first;  // kilometers
      auto height = gridcenter[1].second;

      boost::shared_ptr<NFmiArea> area(NFmiArea::CreateFromCenter(
          grid.Area()->SpatialReference(), "WGS84", center, 2 * 1000 * width, 2 * 1000 * height));

      bl = area->BottomLeftLatLon();
      tr = area->TopRightLatLon();
    }
    else
    {
      itsReqParams.bboxRect = nPairsOfValues<double>(itsReqParams.origBBox, "bboxstr", 2);
      bl = NFmiPoint((*itsReqParams.bboxRect)[BOTTOMLEFT].first,
                     (*itsReqParams.bboxRect)[BOTTOMLEFT].second);
      tr = NFmiPoint((*itsReqParams.bboxRect)[TOPRIGHT].first,
                     (*itsReqParams.bboxRect)[TOPRIGHT].second);
    }

    NFmiPoint xy1 = grid.LatLonToGrid(bl);
    NFmiPoint xy2 = grid.LatLonToGrid(tr);

    itsCropping.bottomLeftX = boost::numeric_cast<int>(floor(xy1.X()));
    itsCropping.bottomLeftY = boost::numeric_cast<int>(floor(xy1.Y()));
    itsCropping.topRightX = boost::numeric_cast<int>(ceil(xy2.X()));
    itsCropping.topRightY = boost::numeric_cast<int>(ceil(xy2.Y()));

    if (itsCropping.bottomLeftX < 0)
      itsCropping.bottomLeftX = 0;
    if (itsCropping.bottomLeftY < 0)
      itsCropping.bottomLeftY = 0;
    if (itsCropping.topRightX >= (int)grid.XNumber())
      itsCropping.topRightX = grid.XNumber() - 1;
    if (itsCropping.topRightY >= (int)grid.YNumber())
      itsCropping.topRightY = grid.YNumber() - 1;

    if ((itsCropping.bottomLeftX >= itsCropping.topRightX) ||
        (itsCropping.bottomLeftY >= itsCropping.topRightY))
      throw Fmi::Exception(BCP, "Bounding box does not intersect the grid").disableStackTrace();

    itsCropping.gridSizeX = ((itsCropping.topRightX - itsCropping.bottomLeftX) + 1);
    itsCropping.gridSizeY = ((itsCropping.topRightY - itsCropping.bottomLeftY) + 1);

    itsCropping.crop = itsCropping.cropped = true;

    // Take stepping (gridstep=dx,dy) into account

    setSteppedGridSize();

    bl = grid.GridToLatLon(NFmiPoint(itsCropping.bottomLeftX, itsCropping.bottomLeftY));
    tr = grid.GridToLatLon(NFmiPoint(itsCropping.topRightX, itsCropping.topRightY));

    ostringstream os;
    os << fixed << setprecision(8) << bl.X() << "," << bl.Y() << "," << tr.X() << "," << tr.Y();

    itsReqParams.bbox = os.str();
    itsReqParams.bboxRect = nPairsOfValues<double>(itsReqParams.bbox, "bbox", 2);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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
        {SRS_PT_TRANSVERSE_MERCATOR, A_TransverseMercator, false, false, true},
        {SRS_PT_LAMBERT_CONFORMAL_CONIC_1SP, A_LambertConformalConic, true, true, true},
        {SRS_PT_LAMBERT_CONFORMAL_CONIC_2SP, A_LambertConformalConic, true, true, true},
        {nullptr, A_Native, false, false, false}};

    if (!projection)
      throw Fmi::Exception(BCP, "Projection name is undefined");

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

    throw Fmi::Exception(BCP, "Unsupported projection '" + proj + "'");
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Coordinate transformation from querydata 'datum'/projection to
 * 		requested projection with or without datum shift to wgs84
 *
 *		The transformed coordinates are stored to 'itsSrcLatLons' -member;
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

    if ((err = qdProjectedSrs.importFromWkt(area->WKT().c_str())) != OGRERR_NONE)
      throw Fmi::Exception(BCP,
                           "transform: srs.Import(WKT) error " + boost::lexical_cast<string>(err));

    qdProjectedSrs.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);

    // qd geographic cs

    qdLLSrsPtr = itsResources.cloneGeogCS(qdProjectedSrs);
    if (!qdLLSrsPtr)
      throw Fmi::Exception(BCP, "transform: qdsrs.cloneGeogCS() failed");

    qdLLSrsPtr->SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);

    // Helmert transformation parameters for wgs84 output

    if (Datum::isDatumShiftToWGS84(itsReqParams.datumShift))
    {
      double htp[7];
      Datum::getHelmertTransformationParameters(itsReqParams.datumShift, area, qdProjectedSrs, htp);

      qdLLSrsPtr->SetTOWGS84(htp[0], htp[1], htp[2], htp[3], htp[4], htp[5], htp[6]);
    }
    //  else
    //    qdLLSrsPtr->SetTOWGS84(0, 0, 0, 0, 0, 0, 0);

    OGRSpatialReference wgs84ProjectedSrs, *wgs84PrSrsPtr = &wgs84ProjectedSrs,
                                           *wgs84LLSrsPtr = &wgs84ProjectedSrs;
    bool wgs84ProjLL = false;
    bool qdProjLL = (area->SpatialReference().isGeographic() ||
                     (area->AreaStr().find("rotlatlon") != string::npos));

    bool useNativeBBox = (itsReqParams.bbox.empty() && itsReqParams.gridCenter.empty());
    bool useNativeResolution = ((!itsReqParams.gridSizeXY) && (!itsReqParams.gridResolutionXY));

    auto sourceArea = area;
    const auto areaStr = sourceArea->AreaStr();
    size_t bboxPos = areaStr.find(":");

    if ((bboxPos == string::npos) || (bboxPos == 0) || (bboxPos >= (areaStr.length() - 1)))
      throw Fmi::Exception(
          BCP, "Unrecognized area '" + areaStr + "' for producer '" + itsReqParams.producer + "'");

    string sourceProjection = areaStr.substr(0, bboxPos);

    if ((!useNativeBBox) || (!useNativeResolution))
    {
      // Get bbox'ed source area and/or grid size

      if (!useNativeBBox)
      {
        auto bbox = (itsReqParams.bbox.empty() ? getGridCenterBBoxStr() : itsReqParams.bbox);
        sourceArea = itsResources.createArea(sourceProjection + "|" + bbox);
      }

      if (useNativeResolution)
      {
        auto xScale = sourceArea->WorldXYWidth() / (area->WorldXYWidth() - 1);
        auto yScale = sourceArea->WorldXYHeight() / (area->WorldXYHeight() - 1);

        itsReqGridSizeX = ceil(xScale * itsReqGridSizeX);
        itsReqGridSizeY = ceil(yScale * itsReqGridSizeY);
      }

      setRequestedGridSize(*sourceArea, itsReqGridSizeX, itsReqGridSizeY);
    }

    if (itsReqParams.projType == P_Epsg)
    {
      // Epsg projection
      //
      if ((err = wgs84PrSrsPtr->importFromEPSG(itsReqParams.epsgCode)) != OGRERR_NONE)
        throw Fmi::Exception(BCP,
                             "transform: srs.importFromEPSG(" +
                                 boost::lexical_cast<string>(itsReqParams.epsgCode) + ") error " +
                                 boost::lexical_cast<string>(err));

      if (!(wgs84ProjLL = (!(wgs84PrSrsPtr->IsProjected()))))
        itsReqParams.areaClassId =
            getProjectionType(itsReqParams, wgs84PrSrsPtr->GetAttrValue("PROJECTION"));
      else
        itsReqParams.areaClassId = A_LatLon;
    }
    else
    {
      // qd projection
      //
      if (itsReqParams.projection.empty() || (sourceProjection.find(itsReqParams.projection) == 0))
      {
        // Native

        wgs84ProjectedSrs = qdProjectedSrs;
        wgs84ProjLL = qdProjLL;
      }
      else
      {
        auto targetArea = itsResources.createArea(itsReqParams.projection);

        if ((err = wgs84PrSrsPtr->importFromWkt(targetArea->WKT().c_str())) != OGRERR_NONE)
          throw Fmi::Exception(BCP,
                               "srs.importFromWKT(" + targetArea->WKT() + ") error " +
                                   boost::lexical_cast<string>(err));

        wgs84ProjLL = (targetArea->SpatialReference().isGeographic() ||
                       (targetArea->AreaStr().find("rotlatlon") != string::npos));
      }
    }

    // Clone/store target cs to be used later when setting output geometry

    if (!(wgs84PrSrsPtr = itsResources.cloneCS(wgs84ProjectedSrs, true)))
      throw Fmi::Exception(BCP, "transform: wgs84.cloneCS() failed");

    wgs84PrSrsPtr->SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);

    // If selected set wgs84 geographic output cs

    if ((itsReqParams.projType != P_Epsg) && Datum::isDatumShiftToWGS84(itsReqParams.datumShift))
      if ((err = wgs84PrSrsPtr->SetWellKnownGeogCS("WGS84")) != OGRERR_NONE)
        throw Fmi::Exception(BCP,
                             "transform: srs.Set(WGS84) error " + boost::lexical_cast<string>(err));

    // If projected output cs, get geographic output cs

    if (!wgs84ProjLL)
    {
      if (!(wgs84LLSrsPtr = itsResources.cloneGeogCS(*wgs84PrSrsPtr)))
        throw Fmi::Exception(BCP, "transform: wgs84.cloneGeogCS() failed");

      wgs84LLSrsPtr->SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    }

    // Get native area or requested bbox/gridcenter bounding

    getBBox(q, *sourceArea, *wgs84PrSrsPtr, !wgs84ProjLL ? wgs84LLSrsPtr : nullptr);

    // Transform output cs grid cell projected (or latlon) coordinates to qd latlons.
    //
    // Get transformations from projected (or latlon) target cs to qd latlon cs and from projected
    // target cs to geographic target cs (to get target grid corner latlons).

    OGRCoordinateTransformation *wgs84Pr2QDLLct =
        itsResources.getCoordinateTransformation(wgs84PrSrsPtr, qdLLSrsPtr);
    if (!wgs84Pr2QDLLct)
      throw Fmi::Exception(BCP, "transform: OGRCreateCoordinateTransformation(wgs84,qd) failed");

    OGRCoordinateTransformation *wgs84Pr2LLct = nullptr;
    if ((!wgs84ProjLL) &&
        (!(wgs84Pr2LLct = itsResources.getCoordinateTransformation(wgs84PrSrsPtr, wgs84LLSrsPtr))))
      throw Fmi::Exception(BCP, "transform: OGRCreateCoordinateTransformation(wgs84,wgs84) failed");

    typedef NFmiDataMatrix<float>::size_type sz_t;
    double xc, yc;

    NFmiPoint bl = itsBoundingBox.bottomLeft;
    NFmiPoint tr = itsBoundingBox.topRight;

    itsSrcLatLons = Fmi::CoordinateMatrix(itsReqGridSizeX, itsReqGridSizeY);
    const sz_t xs = itsSrcLatLons.width();
    const sz_t ys = itsSrcLatLons.height();
    const sz_t xN = xs - 1;
    const sz_t yN = ys - 1;
    sz_t x, y;

    if (itsReqParams.outputFormat == NetCdf)
    {
      itsTargetLatLons = Fmi::CoordinateMatrix(itsReqGridSizeX, itsReqGridSizeY);
      itsTargetWorldXYs = Fmi::CoordinateMatrix(itsReqGridSizeX, itsReqGridSizeY);
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
          itsSrcLatLons.set(x, y, xc, (y == 0) ? -90.0 : 90.0);

        continue;
      }

      for (x = 0; x < xs; x++, xc += itsDX)
      {
        double txc = xc;
        double tyc = yc;

        if (!(wgs84Pr2QDLLct->Transform(1, &txc, &tyc)))
          throw Fmi::Exception(BCP, "transform: Transform(wgs84,qd) failed");

        itsSrcLatLons.set(x, y, txc, tyc);

        if (!wgs84ProjLL)
        {
          if (((y == 0) && (x == 0)) || ((y == yN) && (x == xN)))
          {
            // Output cs grid bottom left and top right projected coordinates to latlons
            //
            txc = xc;
            tyc = yc;

            if (!(wgs84Pr2LLct->Transform(1, &txc, &tyc)))
              throw Fmi::Exception(BCP, "transform: Transform(wgs84,wgs84) failed");

            if (y == 0)
              itsBoundingBox.bottomLeft = NFmiPoint(txc, tyc);
            else
              itsBoundingBox.topRight = NFmiPoint(txc, tyc);
          }

          if (itsReqParams.outputFormat == NetCdf)
          {
            // Output cs world xy coordinates for netcdf output
            //
            itsTargetWorldXYs.set(x, y, xc, yc);
          }
        }

        if (itsReqParams.outputFormat == NetCdf)
        {
          // Output cs grid (projected coordinates to) latlons for netcdf output
          //
          txc = xc;
          tyc = yc;

          if ((!wgs84ProjLL) && (!(wgs84Pr2LLct->Transform(1, &txc, &tyc))))
            throw Fmi::Exception(BCP, "transform: Transform(wgs84,wgs84) failed");

          itsTargetLatLons.set(x, y, txc, tyc);
        }
      }
    }

    itsDX = fabs((tr.X() - bl.X()) / xs);
    itsDY = fabs((tr.Y() - bl.Y()) / ys);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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
    if (itsMetaFlag)
    {
      // Set geometry
      //
      NFmiPoint bl, tr;

      // TODO: Why itsCropping.cropped tested with DatumShift::None ?
      //
      if (((!itsCropping.cropped) && (itsReqParams.datumShift == Datum::DatumShift::None)) ||
          (!itsReqParams.bboxRect))
      {
        // Using the native or projected area's corners

        bl = area->BottomLeftLatLon();
        tr = area->TopRightLatLon();

        if ((bl.X() >= tr.X()) || (bl.Y() >= tr.Y()))
          throw Fmi::Exception::Trace(BCP, "Area is flipped");
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

      if (itsReqParams.datumShift == Datum::DatumShift::None)
      {
        itsDX = area->WorldXYWidth() / (itsReqGridSizeX - 1);
        itsDY = area->WorldXYHeight() / (itsReqGridSizeY - 1);
      }
      else
      {
        // Transform the coordinates to 'itsSrcLatLons' -member
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
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Get wkt from geometry
 *
 */
// ----------------------------------------------------------------------

string DataStreamer::getWKT(OGRSpatialReference *geometrySRS) const
{
  try
  {
    OGRErr err;
    const char *const papszOptions[] = {"FORMAT=WKT2", nullptr};
    char *ppszResult;

    if ((err = geometrySRS->exportToWkt(&ppszResult, papszOptions)) != OGRERR_NONE)
      throw Fmi::Exception(BCP, "exportToWkt error " + boost::lexical_cast<string>(err));

    string WKT = ppszResult;
    CPLFree(ppszResult);

    return WKT;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Extract spheroid from geometry
 *
 */
// ----------------------------------------------------------------------

void DataStreamer::extractSpheroidFromGeom(OGRSpatialReference *geometrySRS,
                                           const string &areaWKT,
                                           string &ellipsoid,
                                           double &radiusOrSemiMajor,
                                           double &invFlattening,
                                           const char *crsName)
{
  try
  {
    OGRSpatialReference areaSRS;
    auto &srs = (geometrySRS ? *geometrySRS : areaSRS);

    if (!geometrySRS)
    {
      OGRErr err;

      if ((err = areaSRS.importFromWkt(areaWKT.c_str())) != OGRERR_NONE)
        throw Fmi::Exception(
            BCP, "srs.importFromWKT(" + areaWKT + ") error " + boost::lexical_cast<string>(err));
    }

    const char *ellipsoidAttr = "SPHEROID";
    auto ellipsoidPtr = srs.GetAttrValue(ellipsoidAttr);

    if (!ellipsoidPtr)
    {
      ellipsoidAttr = "ELLIPSOID";
      ellipsoidPtr = srs.GetAttrValue(ellipsoidAttr);
    }

    auto radiusOrSemiMajorPtr = srs.GetAttrValue(ellipsoidAttr, 1);
    auto invFlatteningPtr = srs.GetAttrValue(ellipsoidAttr, 2);

    if (!(ellipsoidPtr && radiusOrSemiMajorPtr && invFlatteningPtr))
      throw Fmi::Exception(BCP, string(crsName) + ": geometry " + ellipsoidAttr + " not set");

    ellipsoid = ellipsoidPtr;
    radiusOrSemiMajor = Fmi::stod(radiusOrSemiMajorPtr);
    invFlattening = Fmi::stod(invFlatteningPtr);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Build a new NFmiVPlaceDescriptor
 *
 */
// ----------------------------------------------------------------------

NFmiVPlaceDescriptor DataStreamer::makeVPlaceDescriptor(Engine::Querydata::Q q,
                                                        bool requestLevels,
                                                        bool nativeLevels) const
{
  try
  {
    if (nativeLevels)
    {
      auto info = q->info();
      return NFmiVPlaceDescriptor(((NFmiQueryInfo *)&(*info))->VPlaceDescriptor());
    }

    NFmiLevelBag lbag;
    auto levelIndex = q->levelIndex();
    bool levelInterpolation =
        (isPressureLevel(itsNativeLevelType) && itsProducer.verticalInterpolation);

    if (requestLevels)
    {
      if (levelInterpolation)
      {
        for (auto reqLevel = itsSortedDataLevels.begin(); (reqLevel != itsSortedDataLevels.end());
             reqLevel++)
        {
          lbag.AddLevel(NFmiLevel(itsNativeLevelType, *reqLevel));

          if (itsReqParams.outputFormat != QD)
          {
            // Only one level for querydata created for cached projection handling
            //
            break;
          }
        }
      }
      else
      {
        for (q->resetLevel(); q->nextLevel();)
        {
          if (find(itsSortedDataLevels.begin(), itsSortedDataLevels.end(), q->levelValue()) !=
              itsSortedDataLevels.end())
          {
            lbag.AddLevel(q->level());

            if (itsReqParams.outputFormat != QD)
              break;
          }
        }

        if (lbag.GetSize() == 0)
          throw Fmi::Exception(BCP, "No requested level available in data");

        q->levelIndex(levelIndex);
      }

      return NFmiVPlaceDescriptor(lbag);
    }

    // Requested native levels and native levels needed for interpolation

    auto reqLevel = itsSortedDataLevels.begin();
    boost::optional<NFmiLevel> prevNativeLevel;

    for (q->resetLevel(); q->nextLevel();)
    {
      bool hasReqLevel = (reqLevel != itsSortedDataLevels.end());
      bool isNativeLevel = ((!hasReqLevel) || (q->levelValue() == *reqLevel));
      bool isInterpolatedLevel = (isNativeLevel || (!levelInterpolation)) ? (!hasReqLevel)
                                 : itsRisingLevels ? (q->levelValue() > *reqLevel)
                                                   : (q->levelValue() < *reqLevel);

      if (isInterpolatedLevel && prevNativeLevel)
        lbag.AddLevel(*prevNativeLevel);

      if (!hasReqLevel)
        break;

      if (!(isNativeLevel || isInterpolatedLevel))
      {
        // Skip native levels preceeding first requested level

        prevNativeLevel = q->level();
        continue;
      }

      lbag.AddLevel(q->level());

      if (isNativeLevel)
        prevNativeLevel.reset();
      else
        prevNativeLevel = q->level();

      float level1 = 0, level2 = 0;

      // Skip requested levels preceeding current native level

      do
      {
        if ((++reqLevel != itsSortedDataLevels.end()) && (!isNativeLevel))
        {
          level1 = (itsRisingLevels ? q->levelValue() : *reqLevel);
          level2 = (itsRisingLevels ? *reqLevel : q->levelValue());
        }
      } while ((!isNativeLevel) && (reqLevel != itsSortedDataLevels.end()) && (level1 > level2));
    }

    if (lbag.GetSize() == 0)
      throw Fmi::Exception(BCP, "No requested level available in data");

    q->levelIndex(levelIndex);

    return NFmiVPlaceDescriptor(lbag);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Build a new NFmiParamDescriptor
 *
 */
// ----------------------------------------------------------------------

NFmiParamDescriptor DataStreamer::makeParamDescriptor(
    Engine::Querydata::Q q, const std::list<FmiParameterName> &currentParams) const
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
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Build a new NFmiTimeDescriptor
 *
 */
// ----------------------------------------------------------------------

NFmiTimeDescriptor DataStreamer::makeTimeDescriptor(Engine::Querydata::Q q,
                                                    bool requestTimes,
                                                    bool nativeTimes) const
{
  try
  {
    // Note: Origintime is taken from the first querydata; firstTime() (by generateValidTimeList())
    // and possibly Time(itsDataTimes.begin()) (by extractData()) has been called for q.

    if (nativeTimes)
    {
      auto info = q->info();
      return NFmiTimeDescriptor(((NFmiQueryInfo *)&(*info))->TimeDescriptor());
    }

    NFmiMetTime ot = q->originTime();
    TimeSeries::TimeSeriesGenerator::LocalTimeList::const_iterator timeIter = itsDataTimes.begin();
    NFmiTimeList dataTimes;

    for (; (timeIter != itsDataTimes.end()); timeIter++)
    {
      dataTimes.Add(new NFmiMetTime(timeIter->utc_time()));

      if ((!requestTimes) && (itsReqParams.outputFormat != QD))
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
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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
    NFmiVPlaceDescriptor vdesc = makeVPlaceDescriptor(itsQ, true);
    NFmiFastQueryInfo qi(pdesc, tdesc, hdesc, vdesc, itsQ->infoVersion());

    itsQueryData.reset(NFmiQueryDataUtil::CreateEmptyData(qi));
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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

    if (itsLocCache.NX() == 0)
    {
      NFmiFastQueryInfo tqi(itsQueryData.get());
      q->calcLatlonCachePoints(tqi, itsLocCache);
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

    bool cropxy = (itsCropping.cropped && itsCropping.cropMan);
    size_t x0 = (cropxy ? itsCropping.bottomLeftX : 0), y0 = (cropxy ? itsCropping.bottomLeftY : 0);
    size_t xN = (itsCropping.cropped ? (x0 + itsCropping.gridSizeX) : itsReqGridSizeX),
           yN = (itsCropping.cropped ? (y0 + itsCropping.gridSizeY) : itsReqGridSizeY);

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
        throw Fmi::Exception(BCP, "Data does not contain Wind U-component");
      if (!q->param(kFmiWindVMS))
        throw Fmi::Exception(BCP, "Data does not contain Wind V-component");

      q->setIsSubParamUsed(isSubParamUsed);

      // Get V values

      typedef std::unique_ptr<float, void (*)(float *)> valBuf;

      valBuf vValues(new float[xs * wantedGrid.YNumber()], valBufDeleter);
      float *vPtr0 = vValues.get();

      for (y = y0; (y < yN); y += yStep)
        for (x = x0; (x < xN); x += xStep)
        {
          NFmiLocationCache &lc = itsLocCache[x][y];
          *(vPtr0 + ((y * xs) + x)) =
              (mt ? q->cachedInterpolation(lc, tc) : q->cachedInterpolation(lc));
        }

      // Get U values

      if (!q->param(kFmiWindUMS))
        throw Fmi::Exception(BCP, "Internal error: could not switch to parameter U");
      q->setIsSubParamUsed(isSubParamUsed);

      valBuf uValues(new float[xs * wantedGrid.YNumber()], valBufDeleter);
      float *uPtr0 = uValues.get();

      for (y = y0; (y < yN); y += yStep)
        for (x = x0; (x < xN); x += xStep)
        {
          NFmiLocationCache &lc = itsLocCache[x][y];
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
              throw Fmi::Exception(BCP, "Internal error: could not set grid index");

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
        throw Fmi::Exception(BCP,
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
                *theDEM, *theLandCover, resolution, itsLocCache, demMatrix, waterFlagMatrix))))
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

      itsGridValues = q->landscapeCachedInterpolation(itsLocCache, tc, demMatrix, waterFlagMatrix);
    }
    else
    {
      // Normal access

      for (y = y0; (y < yN); y += yStep)
      {
        for (x = x0; (x < xN); x += xStep)
        {
          NFmiLocationCache &lc = itsLocCache[x][y];
          itsGridValues[x][y] = (mt ? q->cachedInterpolation(lc, tc) : q->cachedInterpolation(lc));
        }
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
 * \brief Check if level is requested by the query
 *
 */
// ----------------------------------------------------------------------

bool DataStreamer::isGridLevelRequested(const Producer &producer,
                                        const Query &query,
                                        FmiLevelType mappingLevelType,
                                        int level) const
{
  try
  {
    auto queryLevels =
        (query.levels.begin() == query.levels.end()) ? producer.gridDefaultLevels : query.levels;

    if (isSurfaceLevel(mappingLevelType) ||
        ((queryLevels.begin() == queryLevels.end()) &&
         ((itsHeightRng || (!itsLevelRng)) ||
          ((level >= itsReqParams.minLevel) && (level <= itsReqParams.maxLevel)))))
      return true;

    // Level interpolation is possible for pressure data only.

    bool interpolatable = (isPressureLevel(mappingLevelType) && itsProducer.verticalInterpolation);

    for (auto it = queryLevels.begin(); (it != queryLevels.end()); it++)
      if (*it == level)
        return true;
      else
      {
        // Currently all data levels are selected if level interpolation is allowed, but
        // could select only 2 surrounding (logically nearest, but any lower and higher,
        // e.g. the lowest and highest level for each parameter/validtime would do) data
        // levels for each requested level needing interpolation to later check if
        // interpolation is possible when extracting the data.
        //
        // TODO: If (pressure) level interpolation would be allowed (has not been so) and
        // there could be quite a lot of levels in the data, this should be changed to avoid
        // collecting unneeded metadata.
      }

    return interpolatable;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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
      throw Fmi::Exception(BCP, "isLevelAvailable: internal: no levels in data");

    if (isSurfaceLevel(itsLevelType))
    {
      // Just set the one and only level from the surface data.
      //
      requestedLevel = abs(boost::numeric_cast<int>(q->levelValue()));
      exactLevel = true;

      return true;
    }

    // Level interpolation is possible for pressure data only.

    bool interpolatable = (isPressureLevel(itsLevelType) && itsProducer.verticalInterpolation);
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
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Test if the newbase projection name matches (and there are no parameters to it)
 *
 * - stereographic matches PROJ.4 stere
 * - stereographic,20 does not match due to the extra parameter
 */
// ----------------------------------------------------------------------

bool projectionMatches(const std::string &projection, const NFmiArea &area)
{
  if (projection.empty())
    return false;
  if (projection.find(',') != std::string::npos)
    return false;

  // Match projection names to projection types. Note that +towgs84 etc
  // checks are done automatically by DetectClassId, we do not need
  // to check them separately.

  auto id = area.ClassId();
  auto sr = area.SpatialReference();

  switch (id)
  {
    // clang-format off
    case kNFmiLatLonArea:                return (projection == "latlon");
    case kNFmiMercatorArea:              return (projection == "mercator");
    case kNFmiStereographicArea:         return (projection == "stereographic");
    case kNFmiEquiDistArea:              return (projection == "equidist");
    case kNFmiLambertConformalConicArea: return (projection == "lcc");
    case kNFmiRotatedLatLonArea:         return (projection == "rotlatlon" || projection == "invrotlatlon");
    case kNFmiYKJArea:                   return (projection == "ykj");
    default:                             return (projection == sr.projInfo().getString("proj"));
      // clang-format on
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Inspect request's gridsize and projection related parameters
 *	  and create target projection (area object) if needed.
 *
 * 	  Note: If area is created, it is owned by resource manager.
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
    itsRetainNativeGridResolution = itsCropping.crop = false;

    if (itsReqParams.datumShift != Datum::DatumShift::None)
    {
      // With datum shift the data is read using transformed coordinates and native projected data.
      //
      // The only mercator qd seen so far had equal bottom left and top right projected X
      // coordinate; currently not supported.
      //

      if ((itsReqParams.areaClassId == A_Mercator) ||
          ((itsReqParams.areaClassId == A_Native) && (nativeClassId == kNFmiMercatorArea)))
        throw Fmi::Exception(BCP, "Mercator not supported when using gdal transformation");

      return;
    }

    // No datum shift; nonnative target projection, bounding or gridsize ?

    if (itsReqParams.projection.empty() && itsReqParams.bbox.empty() &&
        itsReqParams.gridCenter.empty() && itsUseNativeGridSize)
      return;

    // Clear the projection request if it is identical to the data:

    if (projectionMatches(itsReqParams.projection, nativeArea))
      itsReqParams.projection.clear();

    string projection = nativeArea.AreaStr();
    boost::replace_all(projection, ":", "|");

    if ((!itsReqParams.projection.empty()) && (projection.find(itsReqParams.projection) == 0))
      itsReqParams.projection.clear();

    if (itsReqParams.projection.empty() && itsReqParams.bbox.empty() &&
        itsReqParams.gridCenter.empty())
      return;

    size_t bboxPos = projection.find("|");

    if ((bboxPos == string::npos) || (bboxPos == 0) || (bboxPos >= (projection.length() - 1)))
      throw Fmi::Exception(BCP,
                           "Unrecognized projection '" + projection + "' for producer '" +
                               itsReqParams.producer + "'");

    string projStr = projection.substr(0, bboxPos);
    string bboxStr = projection.substr(bboxPos + 1);

    itsUseNativeProj = (itsReqParams.projection.empty() || (itsReqParams.projection == projStr));

    if (!itsUseNativeProj)
      projStr = itsReqParams.projection;  // Creating nonnative projection

    itsUseNativeBBox = ((itsReqParams.bbox.empty() || (itsReqParams.bbox == bboxStr)) &&
                        itsReqParams.gridCenter.empty());

    if (!itsUseNativeBBox &&
        (((itsReqParams.outputFormat == QD) && (!itsUseNativeProj)) || (!itsUseNativeGridSize)))
    {
      // Creating native or nonnative projection with given bounding to load data using
      // absolute gridsize
      if (itsUseNativeGridSize)
      {
        // Projected qd output; set native gridresolution for output querydata
        setNativeGridResolution(nativeArea, nativeGridSizeX, nativeGridSizeY);
        itsUseNativeGridSize = false;
      }

      itsUseNativeProj = false;
    }
    else if ((!itsUseNativeProj) && (nativeClassId != kNFmiLatLonArea))
    {
      // If bbox is not given, get native area latlon bounding box for nonnative projection.
      // Set itsRetainNativeGridResolution to retain native gridresolution if projecting to
      // latlon.

      if (itsUseNativeBBox)
      {
        // In WGS84 mode or with rotlat area transform source grid egde projected coordinates to
        // projected target coordinates and then projected target corners to latlon.
        //
        // bbox produced by rotlat grid egde to latlon transformation (getRegLLBBoxStr(q),
        // q->latLon()) has resulted flipped target area and output data has not been valid.

        if (itsCfg.getLegacyMode() &&
            ((itsReqParams.projType == P_LatLon) || (nativeClassId != A_RotLatLon)))
          bboxStr = getRegLLBBoxStr(q);
        else
          bboxStr = getRegLLBBoxStr(q, nativeArea, itsReqParams.projection);
      }

      if ((itsCfg.getLegacyMode() || (nativeClassId != A_RotLatLon)) &&
          (itsReqParams.projType == P_LatLon))
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
      {
        // bbox from the request or set by setCropping()

        bboxStr = itsReqParams.bbox;
      }
      else if (!itsReqParams.gridCenter.empty())
      {
        // lon,lat,xkm,ykm

        bboxStr = getGridCenterBBoxStr();
      }
      else
      {
        // Native area latlon bounding box from getRegLLBBoxStr()
      }

      projection = projStr + "|" + bboxStr;
      itsResources.createArea(projection);
    }

    itsCropping.crop |= (itsUseNativeProj && (!itsUseNativeBBox) && itsUseNativeGridSize);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Inspect request's gridsize related parameters and create new
 *	  grid with requested size if needed.
 *
 *	  Note: The grid is owned by resource manager.
 */
// ----------------------------------------------------------------------

void DataStreamer::createGrid(const NFmiArea &area,
                              size_t gridSizeX,
                              size_t gridSizeY,
                              bool interpolation)
{
  try
  {
    NFmiGrid *grid = itsResources.getGrid(area, gridSizeX, gridSizeY);

    if (itsCropping.crop)
    {
      if (!itsCropping.cropped)
      {
        // Set cropped grid xy area
        //
        setCropping(*grid);
      }

      // Must use manual cropping (loading entire grid and manually extracting data within given
      // bounding) if nonnative projection or level/pressure interpolated data; CroppedValues()
      // does not support level/pressure interpolation.

      itsCropping.cropMan = ((!itsUseNativeProj) || interpolation);
    }
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief 	Inspect request's gridsize and projection related parameters
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

    std::size_t nativeGridSizeX = q->grid().XNumber();
    std::size_t nativeGridSizeY = q->grid().YNumber();

    // All data has same projection, gridsize and bounding box; thus target projection (area object)
    // and grid needs to be checked/created only once.
    //
    // Note: itsUseNativeProj, itsUseNativeBBox, itsRetainNativeGridResolution and cropping are set
    // by createArea(). itsReqGridSizeX and itsReqGridSizeY are set by setRequestedGridSize.

    if (!itsProjectionChecked)
    {
      itsUseNativeGridSize = setRequestedGridSize(nativeArea, nativeGridSizeX, nativeGridSizeY);
      createArea(q, nativeArea, nativeClassId, nativeGridSizeX, nativeGridSizeY);
    }

    // Note: area is set to the native area or owned by the resource manager; *DO NOT DELETE*

    if (!(*area = itsResources.getArea()))
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
      if ((itsReqParams.datumShift == Datum::DatumShift::None) &&
          (nonNativeGrid || (!itsUseNativeBBox)))
      {
        // Create grid if using nonnative grid size. Use the cropped size for cropped querydata.
        //
        size_t gridSizeX = ((itsReqParams.outputFormat == QD) && itsCropping.cropped)
                               ? itsCropping.gridSizeX
                               : itsReqGridSizeX;
        size_t gridSizeY = ((itsReqParams.outputFormat == QD) && itsCropping.cropped)
                               ? itsCropping.gridSizeY
                               : itsReqGridSizeY;

        createGrid(**area, gridSizeX, gridSizeY, interpolation);
      }

      auto gs = (itsCropping.crop ? itsCropping.gridSizeX * itsCropping.gridSizeY
                                  : itsReqGridSizeX * itsReqGridSizeY);
      unsigned long numValues =
          itsDataParams.size() * itsDataLevels.size() * itsDataTimes.size() * gs;

      if (numValues > itsCfg.getMaxRequestDataValues())
      {
        throw Fmi::Exception(
            BCP,
            "Too much data requested (" + Fmi::to_string(numValues) + " values, max " +
                Fmi::to_string(itsCfg.getMaxRequestDataValues()) +
                "); adjust area/grid and/or number of parameters, levels and times");
      }
      else
      {
        auto logValues = itsCfg.getLogRequestDataValues();

        if ((logValues > 0) && (numValues > logValues))
          fprintf(stderr,
                  "Query for %lu (p=%lu,l=%lu,t=%lu,g=%lu) values; '%s'\n",
                  numValues,
                  itsDataParams.size(),
                  itsDataLevels.size(),
                  itsDataTimes.size(),
                  gs,
                  itsRequest.getURI().c_str());
      }

      itsProjectionChecked = true;
    }

    // Note: grid is set to nullptr or owned by the resource manager; *DO NOT DELETE*

    *grid = itsResources.getGrid();

    if ((!nonNativeGrid) && landscaping && (itsDEMMatrix.NX() == 0))
    {
      // Load dem values and water flags for the native grid
      //
      int x1 = 0, y1 = 0, x2 = 0, y2 = 0;

      if (itsCropping.cropped && (!itsCropping.cropMan))
      {
        x1 = itsCropping.bottomLeftX;
        y1 = itsCropping.bottomLeftY;
        x2 = itsCropping.topRightX;
        y2 = itsCropping.topRightY;
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
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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

    size_t nextParamOffset = 1;

    for (itsParamIterator++; (itsParamIterator != itsDataParams.end()); itsParamIterator++)
    {
      if ((itsReqParams.outputFormat != QD) && (itsScalingIterator != itsValScaling.end()))
      {
        itsScalingIterator++;

        if (itsScalingIterator == itsValScaling.end())
          throw Fmi::Exception(BCP, "nextParam: internal: No more scaling data");
      }

      if (q->param(itsParamIterator->number()))
        break;

      nextParamOffset++;
    }

    // In-memory qd needs to be reloaded if it does not contain current parameter

    if ((itsParamIterator != itsDataParams.end()) && itsCPQ &&
        (!itsCPQ->param(itsParamIterator->number())))
      itsCPQ.reset();

    paramChanged(nextParamOffset);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Get Q for in-memory querydata object containing only current parameter(s)
 *        (both U and V if true north azimuth adjustment needed)
 *
 */
// ----------------------------------------------------------------------

Engine::Querydata::Q DataStreamer::getCurrentParamQ(
    const std::list<FmiParameterName> &currentParams) const
{
  try
  {
    NFmiParamDescriptor paramDescriptor = makeParamDescriptor(itsQ, currentParams);
    NFmiVPlaceDescriptor levelDescriptor = makeVPlaceDescriptor(itsQ);
    NFmiTimeDescriptor timeDescriptor = makeTimeDescriptor(itsQ, true);
    auto srcInfo = itsQ->info();

    NFmiFastQueryInfo info(paramDescriptor,
                           timeDescriptor,
                           srcInfo->HPlaceDescriptor(),
                           levelDescriptor,
                           itsQ->infoVersion());

    boost::shared_ptr<NFmiQueryData> data(NFmiQueryDataUtil::CreateEmptyData(info));
    NFmiFastQueryInfo dstInfo(data.get());
    auto levelIndex = itsQ->levelIndex();

    // Establish output timeindexes up front for speed. -1 implies time is not available
    std::vector<long> timeindexes(timeDescriptor.Size(), -1);

    for (unsigned long i = 0; i < timeDescriptor.Size(); i++)
      if (dstInfo.TimeIndex(i))
        if (srcInfo->Time(dstInfo.Time()))
          timeindexes[i] = srcInfo->TimeIndex();

    for (dstInfo.ResetParam(); dstInfo.NextParam();)
    {
      srcInfo->Param(dstInfo.Param());

      for (dstInfo.ResetLocation(), srcInfo->ResetLocation();
           dstInfo.NextLocation() && srcInfo->NextLocation();)
      {
        for (dstInfo.ResetLevel(); dstInfo.NextLevel();)
        {
          if (srcInfo->Level(*dstInfo.Level()))
          {
            for (unsigned long i = 0; i < timeDescriptor.Size(); i++)
            {
              if (timeindexes[i] >= 0)
              {
                dstInfo.TimeIndex(i);
                srcInfo->TimeIndex(timeindexes[i]);
                dstInfo.FloatValue(srcInfo->FloatValue());
              }
            }
          }
        }
      }
    }

    itsQ->levelIndex(levelIndex);

    std::size_t hash = 0;
    auto model = boost::make_shared<Engine::Querydata::Model>(data, hash);

    return boost::make_shared<Engine::Querydata::QImpl>(model);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
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
    // First chunk is loaded at initialization

    if (!itsDataChunk.empty())
    {
      chunk = itsDataChunk;
      itsDataChunk.clear();

      return;
    }

    chunk.clear();

    if (itsReqParams.dataSource != QueryData)
    {
      extractGridData(chunk);
      return;
    }

    auto theParamsEnd = itsDataParams.end();
    auto theLevelsBegin = itsSortedDataLevels.begin();
    auto theLevelsEnd = itsSortedDataLevels.end();
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
        //     (isSurfaceLevel(itsLevelType) && (itsParamIterator->type() ==
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

        if ((itsLevelType == kFmiDepth) && (itsNativeLevelType == kFmiHeight))
          level = 0 - level;

        NFmiMetTime mt(itsTimeIterator->utc_time());

        // Set target projection geometry data (to 'itsBoundingBox' and 'dX'/'dY' members) and if
        // using gdal/proj4 projection, transform target projection grid coordinates to
        // 'itsSrcLatLons' -member to get the grid values.

        coordTransform(q, area);

        if (!itsMultiFile)
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

          // Set level if not interpolated, time index gets set (or is not used) below

          if (exactLevel)
            isLevelAvailable(itsCPQ, level, exactLevel);

          q = itsCPQ;
        }

        if (itsReqParams.datumShift == Datum::DatumShift::None)
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
                itsCropping.cropMan = itsCropping.crop;
                itsGridValues = q->values(mt, demValues, waterFlags);
              }
            }
            else
            {
              if (itsCropping.cropped && (!itsCropping.cropMan))
                itsGridValues = q->croppedValues(itsCropping.bottomLeftX,
                                                 itsCropping.bottomLeftY,
                                                 itsCropping.topRightX,
                                                 itsCropping.topRightY,
                                                 demValues,
                                                 waterFlags);
              else
                itsGridValues = q->values(demValues, waterFlags);
            }
          }
          else if (nonNativeGrid)
            itsGridValues = q->pressureValues(*grid, mt, level, q->isRelativeUV());
          else
            itsGridValues = q->pressureValues(mt, level);
        }
        else
          // Using gdal/proj4 projection.
          //
          itsGridValues = q->values(itsSrcLatLons, mt, exactLevel ? kFloatMissing : level);

        // Load the data chunk from 'itsGridValues'.
        //
        // Note: With querydata and netcdf output the data is taken (and buffered) from the
        // 'itsGridValues' member instead of 'chunk' by the upper level (e.g. the format
        // specific getChunk() method).

        if ((itsGridValues.NX() == 0) || (itsGridValues.NY() == 0))
          throw Fmi::Exception(BCP,
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
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Build grid query object for querying data for
 *        current parameter, level and validtime
 *
 */
// ----------------------------------------------------------------------

void DataStreamer::buildGridQuery(QueryServer::Query &gridQuery,
                                  T::ParamLevelId gridLevelType,
                                  int level)
{
  // If reprojecting and bbox/area is not given, get target bbox

  bool nativeArea = (!(itsReqParams.bboxRect || itsReqParams.gridCenterLL));

  if ((!itsReqParams.projection.empty()) && nativeArea)
    getGridBBox();

  if (itsReqParams.bboxRect)
  {
    string bbox = Fmi::to_string((*itsReqParams.bboxRect)[0].first) + "," +
                  Fmi::to_string((*itsReqParams.bboxRect)[0].second) + "," +
                  Fmi::to_string((*itsReqParams.bboxRect)[1].first) + "," +
                  Fmi::to_string((*itsReqParams.bboxRect)[1].second);

    if ((fabs((*itsReqParams.bboxRect)[0].first) <= 360) &&
        (fabs((*itsReqParams.bboxRect)[0].second) <= 180) &&
        (fabs((*itsReqParams.bboxRect)[1].first) <= 360) &&
        (fabs((*itsReqParams.bboxRect)[1].second) <= 180))
      gridQuery.mAttributeList.addAttribute("grid.llbox", bbox);
    else
      gridQuery.mAttributeList.addAttribute("grid.bbox", bbox);
  }
  else if (itsReqParams.gridCenterLL)
  {
    string gridCenter = Fmi::to_string((*itsReqParams.gridCenterLL)[0].first) + "," +
                        Fmi::to_string((*itsReqParams.gridCenterLL)[0].second);
    string gridMetricWidth = Fmi::to_string((*itsReqParams.gridCenterLL)[1].first);
    string gridMetricHeight = Fmi::to_string((*itsReqParams.gridCenterLL)[1].second);

    gridQuery.mAttributeList.addAttribute("grid.center", gridCenter);
    gridQuery.mAttributeList.addAttribute("grid.metricWidth", gridMetricWidth);
    gridQuery.mAttributeList.addAttribute("grid.metricHeight", gridMetricHeight);
  }

  bool nativeResolution = (itsReqParams.gridSize.empty() && (!itsReqParams.gridResolutionXY));

  if (itsReqParams.gridSizeXY)
  {
    itsReqGridSizeX = (*itsReqParams.gridSizeXY)[0].first;
    itsReqGridSizeY = (*itsReqParams.gridSizeXY)[0].second;

    if (!itsReqParams.gridSize.empty())
    {
      string gridWidth = Fmi::to_string(itsReqGridSizeX);
      string gridHeight = Fmi::to_string(itsReqGridSizeY);

      gridQuery.mAttributeList.addAttribute("grid.width", gridWidth);
      gridQuery.mAttributeList.addAttribute("grid.height", gridHeight);
    }
  }
  else if (itsReqParams.gridResolutionXY)
  {
    string gridCellWidth = Fmi::to_string((*itsReqParams.gridResolutionXY)[0].first);
    string gridCellHeight = Fmi::to_string((*itsReqParams.gridResolutionXY)[0].second);

    gridQuery.mAttributeList.addAttribute("grid.cell.width", gridCellWidth);
    gridQuery.mAttributeList.addAttribute("grid.cell.height", gridCellHeight);
  }

  if (itsGridMetaData.gridOriginTime.is_not_a_date_time())
  {
    // Function parameter

    gridQuery.mAnalysisTime.clear();

   // Currently LatestGeneration does not work with StartTimeFromData
   //
   /*
    gridQuery.mFlags = (QueryServer::Query::Flags::LatestGeneration |
                        QueryServer::Query::Flags::SameAnalysisTime);
   */

    gridQuery.mFlags = QueryServer::Query::Flags::SameAnalysisTime;
  }
  else
  {
    gridQuery.mAnalysisTime = to_iso_string(itsGridMetaData.gridOriginTime);
    gridQuery.mFlags = 0;
  }

  if (!itsGridMetaData.paramGeometries.empty())
  {
    // Fetching at least one data parameter, forecast times to query were loaded from
    // content records

    gridQuery.mSearchType = QueryServer::Query::SearchType::TimeSteps;

    uint nTimes = ((itsReqParams.gridTimeBlockSize > 0) ? itsReqParams.gridTimeBlockSize : 1);
    uint nT = 1;

    for (auto it = itsTimeIterator; ((nT <= nTimes) && (it != itsDataTimes.end())); nT++, it++)
      gridQuery.mForecastTimeList.insert(toTimeT(it->utc_time()));
  }
  else
  {
    // Fetching function parameters only, query with user given time range or all time instants
    // looping the time instants returned by each query

    gridQuery.mSearchType = QueryServer::Query::SearchType::TimeRange;
    gridQuery.mTimesteps = itsReqParams.timeSteps;

    if (itsReqParams.timeStep != 0)
      gridQuery.mTimestepSizeInMinutes = itsDataTimeStep;
    else
      gridQuery.mFlags |= QueryServer::Query::Flags::TimeStepIsData;

    if (!itsReqParams.startTime.empty())
      gridQuery.mStartTime = toTimeT(Fmi::DateTime::from_iso_string(itsReqParams.startTime));
    else
      gridQuery.mFlags |= QueryServer::Query::Flags::StartTimeFromData;

    if (!itsReqParams.endTime.empty())
      gridQuery.mEndTime = toTimeT(Fmi::DateTime::from_iso_string(itsReqParams.endTime));
    else
    {
      // Bug, mEndTime needs to be set even when EndTimeFromData is set

      gridQuery.mFlags |= QueryServer::Query::Flags::EndTimeFromData;
      gridQuery.mEndTime = toTimeT(Fmi::DateTime::from_iso_string("99991231T235959"));
    }
  }

  gridQuery.mTimezone = "UTC";

  if (itsReqParams.projection.empty())
  {
    auto crs = (((!nativeArea) && nativeResolution) ? "crop" : "data");
    gridQuery.mAttributeList.addAttribute("grid.crs", crs);

    if (nativeArea && nativeResolution)
      gridQuery.mAttributeList.addAttribute("grid.size", "1");
  }
  else
    gridQuery.mAttributeList.addAttribute("grid.crs", itsReqParams.projection);

  for (auto paramIter = itsParamIterator; (paramIter != itsDataParams.end()); paramIter++)
  {
    QueryServer::QueryParameter queryParam;

    queryParam.mType = QueryServer::QueryParameter::Type::Vector;
    queryParam.mLocationType = QueryServer::QueryParameter::LocationType::Geometry;
    queryParam.mFlags = 0;

    if (itsQuery.isFunctionParameter(paramIter->name(), queryParam.mParam))
    {
      queryParam.mOrigParam = queryParam.mParam;
      queryParam.mSymbolicName = queryParam.mParam;
      queryParam.mParameterKey = queryParam.mParam;
    }
    else
    {
      queryParam.mParam = itsGridMetaData.paramKeys.find(paramIter->name())->second;

      queryParam.mParameterLevelId = gridLevelType;
      if ((itsReqParams.dataSource != GridContent) && (itsLevelType == kFmiPressureLevel))
        level *= 100;
      queryParam.mParameterLevel = level;

      if (itsReqParams.dataSource == GridContent)
      {
        vector<string> paramParts;
        itsQuery.parseRadonParameterName(paramIter->name(), paramParts);

        queryParam.mForecastType = getForecastType(paramIter->name(), paramParts);
        queryParam.mForecastNumber = getForecastNumber(paramIter->name(), paramParts);
        queryParam.mGeometryId = getGeometryId(paramIter->name(), paramParts);
      }
      else
      {
        queryParam.mForecastType = itsGridMetaData.forecastType;
        queryParam.mForecastNumber = itsGridMetaData.forecastNumber;
        queryParam.mGeometryId = itsGridMetaData.geometryId;
      }
    }

    queryParam.mParameterKeyType = T::ParamKeyTypeValue::FMI_NAME;

    queryParam.mAreaInterpolationMethod = -1;
    queryParam.mTimeInterpolationMethod = -1;
    queryParam.mLevelInterpolationMethod = -1;

    if (itsReqParams.outputFormat == NetCdf)
    {
      // Get grid coordinates for netcdf output

      queryParam.mFlags = (QueryServer::QueryParameter::Flags::ReturnCoordinates);  // |
//                         QueryServer::QueryParameter::Flags::NoReturnValues);
    }

    gridQuery.mQueryParameterList.push_back(queryParam);

    if (
        (itsReqParams.dataSource != GridContent) ||
        (gridQuery.mQueryParameterList.size() >= itsReqParams.gridParamBlockSize)
       )
      return;
  }
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
      crsAttr = gridQuery.mAttributeList.getAttribute(attr);
    }

    if ((!crsAttr) || crsAttr->mValue.empty())
      throw Fmi::Exception(BCP, string(attr) + " not set in query result");

    if (crsAttr->mValue == itsGridMetaData.crs)
      return;

    Fmi::SpatialReference fsrs(crsAttr->mValue);
    auto &srs = *fsrs;

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

      auto projection = srs.GetAttrValue("PROJECTION");
      if (!projection)
        throw Fmi::Exception(BCP, string(attr) + ": PROJECTION not set");

      itsGridMetaData.projection = projection;

      auto p4Extension = srs.GetExtension("PROJCS", "PROJ4", "");

      if (strstr(p4Extension, "+proj=ob_tran") &&
          (strstr(p4Extension, "+o_proj=latlon") || strstr(p4Extension, "+o_proj=lonlat") ||
           strstr(p4Extension, "+o_proj=longlat")))
      {
        auto o_lat_p = strstr(p4Extension, "+o_lat_p=");
        auto o_lon_p = strstr(p4Extension, "+o_lon_p=");

        if (o_lat_p)
          o_lat_p += strlen("+o_lat_p=");
        if (o_lon_p)
          o_lon_p += strlen("+o_lon_p=");

        if (o_lat_p && *o_lat_p && o_lon_p && *o_lon_p)
        {
          char olatpbuf[strcspn(o_lat_p, " ") + 1];
          char olonpbuf[strcspn(o_lon_p, " ") + 1];

          strncpy(olatpbuf, o_lat_p, sizeof(olatpbuf) - 1);
          strncpy(olonpbuf, o_lon_p, sizeof(olonpbuf) - 1);

          olatpbuf[sizeof(olatpbuf) - 1] = '\0';
          olonpbuf[sizeof(olonpbuf) - 1] = '\0';

          itsGridMetaData.southernPoleLat = 0 - Fmi::stod(olatpbuf);
          itsGridMetaData.southernPoleLon = Fmi::stod(olonpbuf);

          if (itsGridMetaData.southernPoleLat != 0)
            gridProjection = T::GridProjectionValue::RotatedLatLon;
          else
            throw Fmi::Exception(
                BCP,
                "rotlat grid crs proj4 extension is expected to have nonzero o_lat_p: " +
                    crsAttr->mValue);
        }
        else
          throw Fmi::Exception(
              BCP,
              "rotlat grid crs proj4 extension is expected to have o_lat_p and o_lon_p: " +
                  crsAttr->mValue);
      }
      else if (*p4Extension)
        throw Fmi::Exception(BCP, "Unḱnown grid crs proj4 extension: " + string(p4Extension));
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
      else if (EQUAL(projection, SRS_PT_LAMBERT_AZIMUTHAL_EQUAL_AREA))
        gridProjection = T::GridProjectionValue::LambertAzimuthalEqualArea;
      else
        throw Fmi::Exception(BCP, "Unsupported projection in input data: " + crsAttr->mValue);
    }
    else if (!srs.IsGeographic())
      throw Fmi::Exception(BCP, "Grid crs is neither projected nor geographic: " + crsAttr->mValue);
    else if (srs.IsDerivedGeographic())
    {
      auto plat = fsrs.projInfo().getDouble("o_lat_p");
      auto plon = fsrs.projInfo().getDouble("o_lon_p");

      if ((!plat) || (!plon))
        throw Fmi::Exception(
            BCP, "rotlat grid crs is expected to have o_lat_p and o_lon_p: " + fsrs.projStr());

      itsGridMetaData.southernPoleLat = 0 - *plat;
      itsGridMetaData.southernPoleLon = *plon;

      gridProjection = T::GridProjectionValue::RotatedLatLon;
    }
    else
      gridProjection = T::GridProjectionValue::LatLon;

    // Clone/save crs

    itsResources.cloneCS(srs, true);

    itsGridMetaData.projType = gridProjection;
    itsGridMetaData.crs = crsAttr->mValue;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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
      string gridSize = os.str();

      itsReqParams.gridSizeXY = nPairsOfValues<unsigned int>(gridSize, "gridsize", 1);
    }
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Get native area bbox for requested projection
 *
 */
// ----------------------------------------------------------------------

void DataStreamer::getGridBBox()
{
  try
  {
    auto gridDef =
        Identification::gridDef.getGrib2DefinitionByGeometryId(itsGridMetaData.geometryId);

    if (!gridDef)
      throw Fmi::Exception(BCP, "Native grid definition is unavailable");

    // Try to avoid unnecessary projection handling in queryserver if native projection is used;
    // queryserver may return interpolated/nonnative grid with slightly changed resolution
    //
    // This does not catch epsg:nnnn projections, only exact wkt/proj4 crs matches

    if ((itsReqParams.projection == gridDef->getWKT()) ||
        (itsReqParams.projection == gridDef->getProj4()))
    {
      itsReqParams.projection.clear();
      return;
    }

    // Currently geometry is fixed

    if (itsGridMetaData.targetBBox)
      return;

    OGRLinearRing exterior;
    auto inputSRS = gridDef->getSpatialReference();
    auto coords = gridDef->getGridOriginalCoordinates();
    auto it = coords->begin();
    auto gridSizeX = gridDef->getGridColumnCount();
    auto gridSizeY = gridDef->getGridRowCount();

    inputSRS->SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    exterior.assignSpatialReference(inputSRS);

    for (size_t y = 1, dx = (gridSizeX - 1); (y <= gridSizeY); y++, it++)
      for (size_t x = 1; (x <= gridSizeX);)
      {
        exterior.addPoint(it->x(), it->y());

        size_t dn = (((y == 1) || (y == gridSizeY)) ? 1 : dx);

        x += dn;

        if (x <= gridSizeX)
          it += dn;
      }

    OGRSpatialReference toSRS;
    OGRErr err = toSRS.SetFromUserInput(itsReqParams.projection.c_str());

    if (err != OGRERR_NONE)
      throw Fmi::Exception(BCP, "Could not initialize target crs: " + itsReqParams.projection);

    inputSRS->SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    toSRS.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);

    err = exterior.transformTo(&toSRS);
    if (err != OGRERR_NONE)
      throw Fmi::Exception(BCP, "Failed to transform bbox: " + itsReqParams.projection);

    OGREnvelope psEnvelope;
    exterior.getEnvelope(&psEnvelope);

    string bboxStr = Fmi::to_string(psEnvelope.MinX) + "," + Fmi::to_string(psEnvelope.MinY) + "," +
                     Fmi::to_string(psEnvelope.MaxX) + "," + Fmi::to_string(psEnvelope.MaxY);

    itsGridMetaData.targetBBox = BBoxCorners(NFmiPoint(psEnvelope.MinX, psEnvelope.MinY),
                                             NFmiPoint(psEnvelope.MaxX, psEnvelope.MaxY));

    double lon[] = {psEnvelope.MinX, psEnvelope.MaxX};
    double lat[] = {psEnvelope.MinY, psEnvelope.MaxY};

    if (!toSRS.IsGeographic())
    {
      OGRSpatialReference llSRS;
      llSRS.CopyGeogCSFrom(&toSRS);
      llSRS.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);

      OGRCoordinateTransformation *ct = itsResources.getCoordinateTransformation(&toSRS, &llSRS);

      int pabSuccess[2];

      int status = ct->Transform(2, lon, lat, nullptr, pabSuccess);

      if (!(status && pabSuccess[0] && pabSuccess[1]))
        throw Fmi::Exception(BCP, "Failed to transform bbox to llbbox: " + itsReqParams.projection);
    }

    bboxStr = Fmi::to_string(lon[0]) + "," + Fmi::to_string(lat[0]) + "," + Fmi::to_string(lon[1]) +
              "," + Fmi::to_string(lat[1]);

    itsReqParams.bboxRect = nPairsOfValues<double>(bboxStr, "bboxstr", 2);

    itsRegBoundingBox = BBoxCorners(NFmiPoint(lon[0], lat[0]), NFmiPoint(lon[1], lat[1]));
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Transform (native) grid's regular latlon coords to rotated
 *
 */
// ----------------------------------------------------------------------

void DataStreamer::regLLToGridRotatedCoords(const QueryServer::Query &gridQuery)
{
  try
  {
    auto coords = gridQuery.mQueryParameterList.front().mCoordinates;

    if (coords.empty())
      throw Fmi::Exception(BCP, "No coordinates to transform");

    itsGridMetaData.rotLongitudes.reset(new double[coords.size()]);
    itsGridMetaData.rotLatitudes.reset(new double[coords.size()]);
    std::unique_ptr<int> pS(new int[coords.size()]);

    auto rotLons = itsGridMetaData.rotLongitudes.get();
    auto rotLon = rotLons;
    auto rotLats = itsGridMetaData.rotLatitudes.get();
    auto rotLat = rotLats;
    auto pabSuccess = pS.get();

    for (auto const &coord : coords)
    {
      *rotLon = coord.x();
      rotLon++;
      *rotLat = coord.y();
      rotLat++;
    }

    auto rotLLSRS = itsResources.getGeometrySRS();
    rotLLSRS->SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);

    OGRSpatialReference regLLSRS;
    regLLSRS.CopyGeogCSFrom(rotLLSRS);
    regLLSRS.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);

    OGRCoordinateTransformation *ct = itsResources.getCoordinateTransformation(&regLLSRS, rotLLSRS);

    int status = ct->Transform(coords.size(), rotLons, rotLats, nullptr, pabSuccess);

    if (status != 0)
      for (size_t n = 0; (n < coords.size()); n++, pabSuccess++)
        if (*pabSuccess == 0)
        {
          status = 0;
          break;
        }

    if (!status)
      throw Fmi::Exception(BCP, "Failed to transform regular latlon coords to rotated");
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Get grid data parameter value list item
 *
 */
// ----------------------------------------------------------------------

QueryServer::ParameterValues_sptr DataStreamer::getValueListItem(
    const QueryServer::Query &gridQuery) const
{
  try
  {
    if (gridQuery.mQueryParameterList.size() == 0)
      return nullptr;

    if (itsGridIndex > 0)
    {
      if (gridQuery.mQueryParameterList.size() > 1)
      {
        if (itsGridIndex >= gridQuery.mQueryParameterList.size())
          throw Fmi::Exception(BCP, "getValueListItem: internal: parameter index out of bounds");

        auto itp = gridQuery.mQueryParameterList.begin();
        advance(itp, itsGridIndex);

        if (itp->mValueList.size() == 0)
          return nullptr;

        return itp->mValueList.front();
      }

      if (itsGridIndex >= gridQuery.mQueryParameterList.begin()->mValueList.size())
        throw Fmi::Exception(BCP, "getValueListItem: internal: time index out of bounds");

      auto itt = gridQuery.mQueryParameterList.begin()->mValueList.begin();
      advance(itt, itsGridIndex);

      return *itt;
    }

    if (gridQuery.mQueryParameterList.front().mValueList.size() == 0)
      return nullptr;

    return gridQuery.mQueryParameterList.front().mValueList.front();
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Get grid origo
 *
 */
// ----------------------------------------------------------------------

void DataStreamer::getGridOrigo(const QueryServer::Query &gridQuery)
{
  try
  {
    auto rXAttr = gridQuery.mAttributeList.getAttribute("grid.original.reverseXDirection");

    if ((!rXAttr) || ((rXAttr->mValue != "0") && (rXAttr->mValue != "1")))
      throw Fmi::Exception::Trace(BCP,
                                  "grid.original.reverseXDirection is missing or has unkown value");

    auto rYAttr = gridQuery.mAttributeList.getAttribute("grid.original.reverseYDirection");

    if ((!rYAttr) || ((rYAttr->mValue != "0") && (rYAttr->mValue != "1")))
      throw Fmi::Exception::Trace(
          BCP, "grid.original.reverseYDirection is missing or has unknown value");

    bool iNegative = (rXAttr->mValue == "1");
    bool jPositive = (rYAttr->mValue == "0");

    if ((!iNegative) && (!jPositive))
      itsGridOrigo = kTopLeft;
    else if (iNegative && (!jPositive))
      itsGridOrigo = kTopRight;
    else if ((!iNegative) && jPositive)
      itsGridOrigo = kBottomLeft;
    else
      itsGridOrigo = kBottomRight;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Set data times for looping using function parameter's query result
 *
 */
// ----------------------------------------------------------------------

bool DataStreamer::setDataTimes(const QueryServer::Query &gridQuery)
{
  try
  {
    if (gridQuery.mForecastTimeList.empty())
    {
      itsFirstDataTime = itsLastDataTime = Fmi::DateTime(Fmi::DateTime::NOT_A_DATE_TIME);
      return false;
    }

    itsDataTimes.clear();
    itsFirstDataTime = Fmi::date_time::from_time_t(*itsGridQuery.mForecastTimeList.begin());
    itsLastDataTime = Fmi::date_time::from_time_t(*itsGridQuery.mForecastTimeList.rbegin());

    Fmi::TimeZonePtr& UTC = Fmi::TimeZonePtr::utc;

    for (const auto &forecastTime : itsGridQuery.mForecastTimeList)
    {
      auto t = Fmi::date_time::from_time_t(forecastTime);
      itsDataTimes.push_back(Fmi::LocalDateTime(t, UTC));
    }

    itsReqParams.gridTimeBlockSize = itsDataTimes.size();
    itsTimeIterator = itsDataTimes.begin();

    return true;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Get query result grid infomation (projection, grid size etc).
 *        Return false on empty result (missing data assumed),
 *        throw on errors
 *
 */
// ----------------------------------------------------------------------

bool DataStreamer::getGridQueryInfo(const QueryServer::Query &gridQuery)
{
  try
  {
    // Can't rely on returned query status, check first if got any data

    if ((itsGridIndex == 0) && itsGridMetaData.paramGeometries.empty())
    {
      // Fetching function parameters only, looping time instants returned by each query

      if (!setDataTimes(gridQuery))
        return false;
    }

    const auto valueListItem = getValueListItem(gridQuery);
    if (!valueListItem)
      return false;

    const auto vVec = &(valueListItem->mValueVector);
    if (vVec->empty())
      return false;

    // Origintime is not set if it was not given in request and all parameters are
    // function parameters

    if (!valueListItem->mAnalysisTime.empty())
      itsGridMetaData.gridOriginTime = Fmi::DateTime::from_iso_string(valueListItem->mAnalysisTime);

    // Projection and spheroid

    getGridProjection(gridQuery);

    // Grid origo/direction

    getGridOrigo(gridQuery);

    // Note: grid.crop.llbox is assumed to reflect source grid y -axis direction and
    //       grib.llbox corners are assumed to have increasing latitude order regardless
    //       of source grid y -axis direction

    string bboxStr;

    const char *attr;

    if (
        itsReqParams.projection.empty() &&
        (itsCropping.crop || (!itsReqParams.gridSizeXY)) &&
        (!itsReqParams.gridResolutionXY) &&
        ((!itsReqParams.bbox.empty()) || (!itsReqParams.gridCenter.empty()))
       )
    {
      itsCropping.crop = true;
      attr = "grid.crop.llbox";
    }
    else
      attr = "grid.llbox";

    auto bboxAttr = gridQuery.mAttributeList.getAttribute(attr);

    if (bboxAttr)
      bboxStr = bboxAttr->mValue;
    else if (itsGridMetaData.projType == T::GridProjectionValue::LatLon)
      bboxStr = getGridLLBBoxStr();

    auto bbox = nPairsOfValues<double>(bboxStr, attr, 2);

    if (!bbox)
      throw Fmi::Exception(BCP, string(attr) + " is empty in query result");

    // Take y -axis direction into account when setting bbox (reverse x -axis is not supported)

    auto BL = (((itsGridOrigo != kTopLeft) || itsCropping.crop) ? BOTTOMLEFT : TOPRIGHT);
    auto TR = (((itsGridOrigo != kTopLeft) || itsCropping.crop) ? TOPRIGHT : BOTTOMLEFT);
    auto bb = BBoxCorners(NFmiPoint((*bbox)[BL].first, (*bbox)[BL].second),
                          NFmiPoint((*bbox)[TR].first, (*bbox)[TR].second));

    if (itsGridMetaData.projType != T::GridProjectionValue::RotatedLatLon)
      itsBoundingBox = bb;
    else
      itsGridMetaData.targetBBox = bb;

    // Grid size

    auto widthAttr = gridQuery.mAttributeList.getAttribute("grid.width");
    auto heightAttr = gridQuery.mAttributeList.getAttribute("grid.height");

    if ((!widthAttr) || (!heightAttr))
      throw Fmi::Exception(BCP, "Grid width/height not set in query result");

    auto gridSizeX = Fmi::stoul(widthAttr->mValue.c_str());
    auto gridSizeY = Fmi::stoul(heightAttr->mValue.c_str());

    if (vVec->size() != (gridSizeX * gridSizeY))
      throw Fmi::Exception(BCP,
                           "Grid size " + Fmi::to_string(vVec->size()) + " and width/height " +
                               Fmi::to_string(gridSizeX) + "/" + Fmi::to_string(gridSizeY) +
                               " mismatch");
    else if (itsReqParams.gridSizeXY &&
             ((gridSizeX != itsReqGridSizeX) || (gridSizeY != itsReqGridSizeY)))
      throw Fmi::Exception(BCP,
                           "Invalid grid width/height " + Fmi::to_string(gridSizeX) + "/" +
                               Fmi::to_string(gridSizeY) + ", expecting " +
                               Fmi::to_string(itsReqGridSizeX) + "/" +
                               Fmi::to_string(itsReqGridSizeY));

    // Set/use constant grid size if size/resolution was not set

    itsReqGridSizeX = gridSizeX;
    itsReqGridSizeY = gridSizeY;

    setGridSize(itsReqGridSizeX, itsReqGridSizeY);

    // Take stepping (gridstep=dx,dy) into account

    setSteppedGridSize();

    // Grid resolution

    auto xResolAttr = gridQuery.mAttributeList.getAttribute("grid.cell.width");
    auto yResolAttr = gridQuery.mAttributeList.getAttribute("grid.cell.height");

    if ((!xResolAttr) || (!yResolAttr))
    {
      xResolAttr = gridQuery.mAttributeList.getAttribute("grid.original.cell.width");
      yResolAttr = gridQuery.mAttributeList.getAttribute("grid.original.cell.height");
    }

    if ((!xResolAttr) || (!yResolAttr))
      throw Fmi::Exception(BCP, "Grid cell width/height not set in query result");

    itsDX = Fmi::stod(xResolAttr->mValue.c_str());
    itsDY = Fmi::stod(yResolAttr->mValue.c_str());

    // Adjust resolution by grid step
    //
    // TODO: bbox should also be adjusted if axis dimension is not multipe of step

    size_t xStep = (itsReqParams.gridStepXY ? (*(itsReqParams.gridStepXY))[0].first : 1),
           yStep = (itsReqParams.gridStepXY ? (*(itsReqParams.gridStepXY))[0].second : 1);

    if ((itsGridMetaData.projType != T::GridProjectionValue::LatLon) &&
        (itsGridMetaData.projType != T::GridProjectionValue::RotatedLatLon))
    {
      itsDX *= 1000;
      itsDY *= 1000;
    }

    if (xStep > 1)
      itsDX *= xStep;

    if (yStep > 1)
      itsDY *= yStep;

    // Wind component direction

    auto uvAttr = gridQuery.mAttributeList.getAttribute("grid.original.relativeUV");

    if (uvAttr && (uvAttr->mValue != "0") && (uvAttr->mValue != "1"))
      throw Fmi::Exception::Trace(BCP, "grid.original.relativeUV has unknown value");

    itsGridMetaData.relativeUV = (uvAttr && (uvAttr->mValue == "1"));

    if ((itsGridMetaData.projType == T::GridProjectionValue::RotatedLatLon) &&
        (itsReqParams.outputFormat == NetCdf) && (!itsGridMetaData.rotLongitudes.get()))
    {
      // Transform regular latlon coords to rotated

      regLLToGridRotatedCoords(gridQuery);
    }

    // Ensemble

    itsGridMetaData.forecastType = valueListItem->mForecastType;
    itsGridMetaData.forecastNumber = valueListItem->mForecastNumber;

    return true;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Get grid query object index for current grid
 *
 */
// ----------------------------------------------------------------------

size_t DataStreamer::bufferIndex() const
{
  try
  {
    if (!((itsReqParams.gridTimeBlockSize > 1) || (itsReqParams.gridParamBlockSize > 0)))
      return 0;

    size_t index = 0;

    if (itsReqParams.gridTimeBlockSize > 1)
    {
      if (itsGridQuery.mForecastTimeList.empty())
        return 0;

      auto validTime = toTimeT(itsTimeIterator->utc_time());
      auto forecastTime = itsGridQuery.mForecastTimeList.begin();

      index = (itsTimeIndex % itsReqParams.gridTimeBlockSize);

      if (index >= itsGridQuery.mForecastTimeList.size())
        throw Fmi::Exception(BCP, "bufferIndex: internal: time index out of bounds");

      if (index > 0)
        advance(forecastTime, index);

      bool timeMatch = (*forecastTime == validTime);

      if ((!timeMatch) && itsGridMetaData.paramGeometries.empty())
      {
        // Time should have matched since looping forecast times returned by the query

        throw Fmi::Exception(BCP, "bufferIndex: internal: time index and iterator do not match");
      }

      return (timeMatch ? index : 0);
    }

    auto param = itsGridQuery.mQueryParameterList.begin();

    for (; (param != itsGridQuery.mQueryParameterList.end()); param++, index++)
      if (param->mParam == itsParamIterator->name())
        return index;

    return 0;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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
    auto &gridIterator = itsGridMetaData.getGridIterator();

    for (gridIterator++; !gridIterator.atEnd(); gridIterator++)
    {
      // Parameter specific level type and level for surface data
      //
      // Note: itsGridMetaData.geometryId is set according to result parameter if
      //       parameter is a function parameter, but currently all data and result
      //       parameters are required to have the same geometryid

      T::ParamLevelId gridLevelType;
      int level;

      if (!gridIterator.hasData(itsGridMetaData.geometryId, gridLevelType, level))
        continue;

      itsGridIndex = bufferIndex();

      if (! itsGridIndex)
      {
        // Fetch next block of parameters or timesteps

        itsGridQuery = QueryServer::Query();

        buildGridQuery(itsGridQuery, gridLevelType, level);

        // printf("\n*** Query:\n"); itsGridQuery.print(std::cout,0,0);
        int result = itsGridEngine->executeQuery(itsGridQuery);
        // printf("\n*** Result:\n"); itsGridQuery.print(std::cout,0,0);

        if (result != 0)
        {
          Fmi::Exception exception(BCP, "The query server returns an error message!");
          exception.addParameter("Result", std::to_string(result));
          exception.addParameter("Message", QueryServer::getResultString(result));
          throw exception;
        }
      }

      // Unfortunately no usable status is returned by gridengine query.
      //
      // If no data was returned getGridQueryInfo returs false, assuming the data is just
      // missing because it got cleaned. Otherwise if the returned grid e.g. does not match
      // requested grid size etc, an error is thrown

      if (!getGridQueryInfo(itsGridQuery))
        continue;

      // Load the data chunk from itsGridQuery
      //
      // Note: With netcdf output the data is taken (and buffered) from the 'itsGridValues' member
      // instead of 'chunk' by the upper level (i.e. the format specific getChunk() method).

      NFmiMetTime mt(itsTimeIterator->utc_time());

      getGridDataChunk(itsGridQuery, level, mt, chunk);

      return;
    }
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

bool DataStreamer::resetDataSet()
{
  // Set parameter and level iterators etc. to their start positions and load first available grid

  itsLevelIterator = itsSortedDataLevels.begin();
  itsParamIterator = itsDataParams.begin();
  itsTimeIterator = itsDataTimes.begin();
  itsScalingIterator = itsValScaling.begin();

  itsTimeIndex = itsLevelIndex = 0;
  if (itsQ)
    itsQ->resetTime();

  itsDataChunk.clear();

  extractData(itsDataChunk);

  return !itsDataChunk.empty();
}

void DataStreamer::setEngines(const Engine::Querydata::Engine *theQEngine,
                              const Engine::Grid::Engine *theGridEngine,
                              const Engine::Geonames::Engine *theGeoEngine)
{
  itsQEngine = theQEngine;
  itsGridEngine = theGridEngine;
  itsGeoEngine = theGeoEngine;
}

}  // namespace Download
}  // namespace Plugin
}  // namespace SmartMet

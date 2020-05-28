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
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/foreach.hpp>
#include <gis/DEM.h>
#include <gis/LandCover.h>
#include <gis/ProjInfo.h>
#include <macgyver/StringConversion.h>
#include <newbase/NFmiAreaFactory.h>
#include <newbase/NFmiEnumConverter.h>
#include <newbase/NFmiQueryData.h>
#include <newbase/NFmiQueryDataUtil.h>
#include <newbase/NFmiTimeList.h>
#include <spine/Exception.h>
#include <sys/types.h>
#include <string>
#include <unistd.h>
#include <unordered_set>

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
// ----------------------------------------------------------------------
/*!
 * \brief Chunked data streaming
 */
// ----------------------------------------------------------------------

DataStreamer::DataStreamer(const Spine::HTTP::Request &req,
                           const Config &config,
                           const Producer &producer)
    : Spine::HTTP::ContentStreamer(),
      itsRequest(req),
      itsCfg(config),
      itsProducer(producer),
      itsChunkLength(maxChunkLengthInBytes),
      itsMaxMsgChunks(maxMsgChunks)
{
}

DataStreamer::~DataStreamer() {}

// ----------------------------------------------------------------------
/*!
 * \brief Determine data timestep
 *
 */
// ----------------------------------------------------------------------

void DataStreamer::checkDataTimeStep()
{
  try
  {
    const long minMinutesInMonth = 28 * minutesInDay;
    const long maxMinutesInMonth = 31 * minutesInDay;
    const long minMinutesInYear = 365 * minutesInDay;
    const long maxMinutesInYear = 366 * minutesInDay;

    auto q = itsQ;

    itsDataTimeStep = 0;

    if (q->firstTime())
    {
      NFmiTime t1 = q->validTime();
      itsDataTimeStep = (q->nextTime() ? q->validTime().DifferenceInMinutes(t1) : 60);

      q->firstTime();
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

void DataStreamer::setLevels(const Query &query)
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
        ((query.levels.begin() == query.levels.end()) && ((!itsLevelRng) && (!itsHeightRng)))
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
 * \brief Check if (any) requested data is available.
 *
 */
// ----------------------------------------------------------------------

bool DataStreamer::hasRequestedData(const Producer &producer)
{
  try
  {
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

    bool exactLevel = (itsLevelRng || isSurfaceLevel(itsLevelType));

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
          exactLevel = (level == queryLevel);

          if (!exactLevel)
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
 * \brief Parse bbox from input
 */
// ----------------------------------------------------------------------

void DataStreamer::getBBoxStr(const std::string &bbox)
{
  try
  {
    std::vector<std::string> parts;
    boost::algorithm::split(parts, bbox, boost::algorithm::is_any_of(","));
    if (parts.size() != 4)
      throw Spine::Exception(BCP, "bbox must contain four comma separated values");

    NFmiPoint bottomLeft{std::stod(parts[0]), std::stod(parts[1])};
    NFmiPoint topRight{std::stod(parts[2]), std::stod(parts[3])};
    itsRegBoundingBox = BBoxCorners{bottomLeft, topRight};
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Failed to parse bbox '" + bbox + "'");
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

#ifndef WGS84
    auto shared_latlons = q->latLonCache();
    const auto &llc = *shared_latlons;
#else
    const auto &area = q->area();
    const auto &grid = q->grid();
#endif

    double blLon = 0.0, blLat = 0.0, trLon = 0.0, trLat = 0.0;
    std::size_t gridSizeX = q->grid().XNumber();
    std::size_t gridSizeY = q->grid().YNumber();

    // Loop all columns of first and last row and first and last columns of other rows.

#ifndef WGS84
    for (std::size_t y = 1, n = 0, dx = (gridSizeX - 1); (y <= gridSizeY); y++, n++)
      for (std::size_t x = 1; (x <= gridSizeX);)
#else
    bool first = true;
    for (std::size_t y = 0, dx = gridSizeX - 1; y < gridSizeY; y++)
      for (std::size_t x = 0; x < gridSizeX;)
#endif
      {
#ifndef WGS84
        const NFmiPoint &p = llc[n];
#else
        const NFmiPoint p = area.ToNativeLatLon(grid.GridToXY(NFmiPoint(x, y)));
#endif

        auto px = p.X(), py = p.Y();

#ifndef WGS84
        if (n == 0)
        {
          blLon = trLon = px;
          blLat = trLat = py;
        }
#else
        if (first)
        {
          first = false;
          blLon = trLon = px;
          blLat = trLat = py;
        }
#endif
        else
        {
          blLon = std::min(px, blLon);
          trLon = std::max(px, trLon);
          blLat = std::min(py, blLat);
          trLat = std::max(py, trLat);
        }

#ifndef WGS84
        size_t dn = (((y == 1) || (y == gridSizeY)) ? 1 : dx);
#else
        size_t dn = (((y == 0) || (y == gridSizeY - 1)) ? 1 : dx);
#endif

        x += dn;
#ifndef WGS84
        if (x <= gridSizeX)
          n += dn;
#endif
      }

    itsRegBoundingBox = BBoxCorners{NFmiPoint(blLon, blLat), NFmiPoint(trLon, trLat)};
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
      // divisor was multiplied by 1000 before WGS84 change
      gridSizeX = boost::numeric_cast<size_t>(
          fabs(ceil(area.WorldXYWidth() / ((*itsReqParams.gridResolutionXY)[0].first * 1000))));
      gridSizeY = boost::numeric_cast<size_t>(
          fabs(ceil(area.WorldXYHeight() / ((*itsReqParams.gridResolutionXY)[0].second * 1000))));

      if ((gridSizeX <= 1) || (gridSizeY <= 1))
        throw Spine::Exception(BCP, "Invalid gridsize for producer '" + itsReqParams.producer + "'")
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

    // string bboxStr(itsReqParams.gridCenterLL ? getGridCenterBBoxStr(itsUseNativeProj, grid)
    //                                          : itsReqParams.origBBox);

    NFmiPoint bl;
    NFmiPoint tr;

    if (itsReqParams.gridCenterLL)
    {
      const auto &gridcenter = *itsReqParams.gridCenterLL;

      NFmiPoint center(gridcenter[0].first, gridcenter[0].second);
      auto width = gridcenter[1].first;  // kilometers
      auto height = gridcenter[1].second;

      boost::shared_ptr<NFmiArea> area(NFmiArea::CreateFromCenter(
          itsReqParams.projection, "FMI", center, 2 * 1000 * width, 2 * 1000 * height));

      bl = area->ToNativeLatLon(area->BottomLeft());
      tr = area->ToNativeLatLon(area->TopRight());
    }
    else
    {
      itsReqParams.bboxRect = nPairsOfValues<double>(itsReqParams.origBBox, "bboxstr", 2);
      bl = NFmiPoint((*itsReqParams.bboxRect)[BOTTOMLEFT].first,
                     (*itsReqParams.bboxRect)[BOTTOMLEFT].second);
      tr = NFmiPoint((*itsReqParams.bboxRect)[TOPRIGHT].first,
                     (*itsReqParams.bboxRect)[TOPRIGHT].second);
    }

#ifdef WGS84
    NFmiPoint xy1 = grid.XYToGrid(grid.Area()->NativeToXY(bl));
    NFmiPoint xy2 = grid.XYToGrid(grid.Area()->NativeToXY(tr));
#else
    NFmiPoint xy1 = grid.LatLonToGrid(bl);
    NFmiPoint xy2 = grid.LatLonToGrid(tr);
#endif

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
      throw Spine::Exception(BCP, "Bounding box does not intersect the grid").disableStackTrace();

    itsCropping.gridSizeX = ((itsCropping.topRightX - itsCropping.bottomLeftX) + 1);
    itsCropping.gridSizeY = ((itsCropping.topRightY - itsCropping.bottomLeftY) + 1);

    itsCropping.crop = itsCropping.cropped = true;

    // Take stepping (gridstep=dx,dy) into account

    setSteppedGridSize();

#ifdef WGS84
    bl = grid.Area()->ToNativeLatLon(
        grid.GridToXY(NFmiPoint(itsCropping.bottomLeftX, itsCropping.bottomLeftY)));
    tr = grid.Area()->ToNativeLatLon(
        grid.GridToXY(NFmiPoint(itsCropping.topRightX, itsCropping.topRightY)));
#else
    bl = grid.GridToLatLon(NFmiPoint(itsCropping.bottomLeftX, itsCropping.bottomLeftY));
    tr = grid.GridToLatLon(NFmiPoint(itsCropping.topRightX, itsCropping.topRightY));
#endif

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

    if ((err = qdProjectedSrs.SetFromUserInput(area->ProjStr().c_str())) != OGRERR_NONE)
      throw Spine::Exception(BCP,
                             "transform: srs.Set(WKT) error " + boost::lexical_cast<string>(err));

    // qd geographic cs

    qdLLSrsPtr = itsResources.cloneGeogCS(qdProjectedSrs);
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
    bool wgs84ProjLL = false;
#ifdef WGS84
    bool qdProjLL = area->SpatialReference().isGeographic();
#else
    bool qdProjLL =
        ((area->AreaStr().find("rotlatlon") == 0) || (area->AreaStr().find("latlon") == 0));
#endif

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
                                 ? itsResources.cloneCS(qdProjectedSrs)
                                 : itsResources.cloneGeogCS(qdProjectedSrs))))
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

      if (!(wgs84LLSrsPtr = itsResources.cloneGeogCS(*wgs84PrSrsPtr)))
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

    OGRCoordinateTransformation *qdLL2Wgs84Prct = itsResources.getCoordinateTransformation(
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
        itsResources.getCoordinateTransformation(wgs84PrSrsPtr, qdLLSrsPtr);
    if (!wgs84Pr2QDLLct)
      throw Spine::Exception(BCP, "transform: OGRCreateCoordinateTransformation(wgs84,qd) failed");

    OGRCoordinateTransformation *wgs84Pr2LLct = nullptr;
    if ((!wgs84ProjLL) &&
        (!(wgs84Pr2LLct = itsResources.getCoordinateTransformation(wgs84PrSrsPtr, wgs84LLSrsPtr))))
      throw Spine::Exception(BCP,
                             "transform: OGRCreateCoordinateTransformation(wgs84,wgs84) failed");

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
          throw Spine::Exception(BCP, "transform: Transform(wgs84,qd) failed");

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
            throw Spine::Exception(BCP, "transform: Transform(wgs84,wgs84) failed");

          itsTargetLatLons.set(x, y, txc, tyc);
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
    if (itsMetaFlag)
    {
      // Set geometry
      //
      NFmiPoint bl, tr;

      if (((!itsCropping.cropped) && (itsReqParams.datumShift == Datum::DatumShift::None)) ||
          (!itsReqParams.bboxRect))
      {
        // Using the native or projected area's corners

#if 0        
        bl = area->WorldXYToNativeLatLon(area->WorldRect().TopLeft());
        tr = area->WorldXYToNativeLatLon(area->WorldRect().BottomRight());
#else
        bl = area->ToNativeLatLon(area->BottomLeft());
        tr = area->ToNativeLatLon(area->TopRight());
#endif
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
      return NFmiVPlaceDescriptor(((NFmiQueryInfo *)&(*info))->VPlaceDescriptor());
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
      return NFmiTimeDescriptor(((NFmiQueryInfo *)&(*info))->TimeDescriptor());
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
          NFmiLocationCache &lc = itsLocCache[x][y];
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
    throw Spine::Exception::Trace(BCP, "Operation failed!");
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

  auto id = area.DetectClassId();

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
    default:                             return (projection == area.ProjInfo().getString("proj"));
      // clang-format on
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
    itsRetainNativeGridResolution = itsCropping.crop = false;

    if (itsReqParams.datumShift != Datum::DatumShift::None)
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

      if ((itsReqParams.areaClassId == A_Mercator) ||
          ((itsReqParams.areaClassId == A_Native) && (nativeClassId == kNFmiMercatorArea)))
        throw Spine::Exception(BCP, "Mercator not supported when using gdal transformation");

      return;
    }

    // No datum shift; nonnative target projection, bounding or gridsize ?

    if ((itsReqParams.projection.empty()) && (itsReqParams.bbox.empty()) &&
        (itsReqParams.gridCenter.empty()) && (itsUseNativeGridSize))
      return;

    // Clear the projection request if it is identical to the data:

    if (projectionMatches(itsReqParams.projection, nativeArea))
      itsReqParams.projection.clear();

    // if ((!itsReqParams.projection.empty()) && (projection.find(itsReqParams.projection) == 0))
    //  itsReqParams.projection.clear();

    if ((itsReqParams.projection.empty()) && (itsReqParams.bbox.empty()) &&
        (itsReqParams.gridCenter.empty()))
      return;

    string projStr = nativeArea.ProjStr();
    string bboxStr;  // !!!!!!!!!!!!!!!!! SHOUDL NOT BE UNINITIALIZED IN THE CODE BELOW!

    NFmiPoint bottomLeft, topRight;

#ifndef WGS84
    itsUseNativeProj = (itsReqParams.projection.empty() || (itsReqParams.projection == projStr));
#else
    itsUseNativeProj = true;
    if (!itsReqParams.projection.empty())
    {
      // Use native projection if generated PROJ.4 would be the same
      NFmiPoint center = nativeArea.CenterLatLon();  // WGS84, doesn't matter, it's close enough
      boost::shared_ptr<NFmiArea> reqarea(
          NFmiAreaFactory::CreateFromCenter(itsReqParams.projection, center, 1000, 1000));
      auto reqProjStr = reqarea->ProjStr();
      itsUseNativeProj = (projStr == reqProjStr);
    }
#endif

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
      // Get native area latlon bounding box for nonnative projection.
      // Set itsRetainNativeGridResolution to retain native gridresolution if projecting to
      // latlon.

      getRegLLBBox(q);

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
      {
        // bbox from the request or set by setCropping()
        getBBoxStr(itsReqParams.bbox);
        itsResources.createArea(
            projStr, itsRegBoundingBox->bottomLeft, itsRegBoundingBox->topRight);
      }
      else if (!itsReqParams.gridCenter.empty())
      {
        NFmiPoint center((*itsReqParams.gridCenterLL)[0].first,
                         (*itsReqParams.gridCenterLL)[0].second);
        auto width = (*itsReqParams.gridCenterLL)[1].first;
        auto height = (*itsReqParams.gridCenterLL)[1].second;

        itsResources.createArea(projStr, center, width, height);
      }
      else
      {
        getRegLLBBox(q);
        itsResources.createArea(
            projStr, itsRegBoundingBox->bottomLeft, itsRegBoundingBox->topRight);
      }
    }

    itsCropping.crop |= (itsUseNativeProj && (!itsUseNativeBBox) && itsUseNativeGridSize);
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
      // bounding)
      // if nonnative projection or level/pressure interpolated data; CroppedValues() does not
      // support
      // level/pressure interpolation.

      itsCropping.cropMan = ((!itsUseNativeProj) || interpolation);
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

    std::size_t nativeGridSizeX = q->grid().XNumber();
    std::size_t nativeGridSizeY = q->grid().YNumber();
    std::size_t gridSizeX = itsReqGridSizeX;
    std::size_t gridSizeY = itsReqGridSizeY;

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
        gridSizeX = ((itsReqParams.outputFormat == QD) && itsCropping.cropped)
                        ? itsCropping.gridSizeX
                        : itsReqGridSizeX;
        gridSizeY = ((itsReqParams.outputFormat == QD) && itsCropping.cropped)
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
        throw Spine::Exception(
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

    size_t nextParamOffset = 1;

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

Engine::Querydata::Q DataStreamer::getCurrentParamQ(
    const std::list<FmiParameterName> &currentParams) const
{
  NFmiParamDescriptor paramDescriptor = makeParamDescriptor(itsQ, currentParams);
  auto srcInfo = itsQ->info();

  NFmiFastQueryInfo info(paramDescriptor,
                         srcInfo->TimeDescriptor(),
                         srcInfo->HPlaceDescriptor(),
                         srcInfo->VPlaceDescriptor(),
                         itsQ->infoVersion());

  boost::shared_ptr<NFmiQueryData> data(NFmiQueryDataUtil::CreateEmptyData(info));
  NFmiFastQueryInfo dstInfo(data.get());
  auto levelIndex = itsQ->levelIndex();

  for (dstInfo.ResetParam(); dstInfo.NextParam();)
  {
    srcInfo->Param(dstInfo.Param());

    for (dstInfo.ResetLocation(), srcInfo->ResetLocation();
         dstInfo.NextLocation() && srcInfo->NextLocation();)
    {
      for (dstInfo.ResetLevel(), srcInfo->ResetLevel();
           dstInfo.NextLevel() && srcInfo->NextLevel();)
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
        // using gdal/proj4 projection,
        // transform target projection grid coordinates to 'itsSrcLatLons' -member to get the grid
        // values.

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

          // Set level index from main data, time index gets set (or is not used) below

          itsCPQ->levelIndex(itsQ->levelIndex());
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

void DataStreamer::resetDataSet(bool getFirstChunk)
{
  itsLevelIterator = itsDataLevels.begin();
  itsParamIterator = itsDataParams.begin();
  itsTimeIterator = itsDataTimes.begin();
  itsScalingIterator = itsValScaling.begin();

  itsTimeIndex = itsLevelIndex = 0;
  itsQ->resetTime();

  itsMultiFile = itsQEngine->getProducerConfig(itsReqParams.producer).ismultifile;

  itsDataChunk.clear();

  if (getFirstChunk)
  {
    extractData(itsDataChunk);
  }
}

void DataStreamer::setEngines(const Engine::Querydata::Engine *theQEngine,
                              const Engine::Geonames::Engine *theGeoEngine)
{
  itsQEngine = theQEngine;
  itsGeoEngine = theGeoEngine;
}

}  // namespace Download
}  // namespace Plugin
}  // namespace SmartMet

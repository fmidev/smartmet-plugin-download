// ======================================================================
/*!
 * \brief SmartMet download service plugin; data streaming
 */
// ======================================================================

#pragma once

#include "Config.h"
#include "Query.h"
#include "Resources.h"
#include "Tools.h"
#include <engines/geonames/Engine.h>
#include <engines/grid/Engine.h>
#include <engines/querydata/Model.h>
#include <engines/querydata/ValidTimeList.h>
#include <gis/CoordinateMatrix.h>
#include <gis/SpatialReference.h>
#include <grid-content/contentServer/corba/server/ServerInterface.h>
#include <grid-content/queryServer/definition/ParameterValues.h>
#include <grid-files/grid/Typedefs.h>
#include <macgyver/DateTime.h>
#include <newbase/NFmiGrid.h>
#include <spine/HTTP.h>
#include <timeseries/TimeSeriesGenerator.h>
#include <ogr_spatialref.h>

namespace SmartMet
{
namespace Plugin
{
namespace Download
{
// Data streaming
//

class DataStreamer : public Spine::HTTP::ContentStreamer
{
 public:
  static const long minutesInDay = 24 * 60;
  static const long minutesInMonth = 31 * minutesInDay;
  static const long minutesInYear = 365 * minutesInDay;

  DataStreamer(const Spine::HTTP::Request &req,
               const Config &config,
               const Query &query,
               const Producer &producer,
               const ReqParams &regParams);
  virtual ~DataStreamer();

  void generateValidTimeList(const Engine::Querydata::Q &q,
                             Fmi::DateTime &oTime,
                             Fmi::DateTime &sTime,
                             Fmi::DateTime &eTime);

  void setMultiFile(bool multiFile) { itsMultiFile = multiFile; }
  void sortLevels();
  void setLevels();
  void setParams(const TimeSeries::OptionParsers::ParameterList &params, const Scaling &scaling);

  void setEngines(const Engine::Querydata::Engine *theQEngine,
                  const Engine::Grid::Engine *theGridEngine,
                  const Engine::Geonames::Engine *theGeoEngine);

  const Config &getConfig() const { return itsCfg; }
  bool hasRequestedData(const Producer &producer,
                        Fmi::DateTime &oTime,
                        Fmi::DateTime &sTime,
                        Fmi::DateTime &eTime);

  virtual std::string getChunk() = 0;

  virtual void getDataChunk(Engine::Querydata::Q q,
                            const NFmiArea *area,
                            NFmiGrid *grid,
                            int level,
                            const NFmiMetTime &mt,
                            NFmiDataMatrix<float> &values,
                            std::string &chunk) = 0;

  virtual void getGridDataChunk(const QueryServer::Query &gridQuery,
                                int level,
                                const NFmiMetTime &mt,
                                std::string &chunk){};

 protected:
  void createQD(const NFmiGrid &g);
  void extractData(std::string &chunk);
  virtual void paramChanged(size_t nextParamOffset = 1) {}

  const Spine::HTTP::Request &itsRequest;

  const Config &itsCfg;
  Query itsQuery;
  ReqParams itsReqParams;
  Resources itsResources;
  const Producer &itsProducer;

  FmiDirection itsGridOrigo;

  bool itsDoneFlag = false;
  NFmiDataMatrix<float> itsGridValues;
  unsigned int itsChunkLength;
  unsigned int itsMaxMsgChunks;

  bool itsMetaFlag = true;
  FmiLevelType itsLevelType;  // Data level type; height level data with negative levels is stored
                              // as kFmiDepth
  FmiLevelType itsNativeLevelType;  // Native data level type
  bool itsPositiveLevels = true;    // true if (depth) levels are positive
  Query::Levels itsDataLevels;
  std::list<int> itsSortedDataLevels;  // Levels in source data order (order needed for qd -output)

  BBoxCorners itsBoundingBox;               // Target projection latlon bounding box
  Fmi::CoordinateMatrix itsSrcLatLons;      // Source grid latlons
  Fmi::CoordinateMatrix itsTargetLatLons;   // Target grid latlons
  Fmi::CoordinateMatrix itsTargetWorldXYs;  // Target grid projected coordinates
  std::size_t itsReqGridSizeX = 0;
  std::size_t itsReqGridSizeY = 0;
  std::size_t itsNX = 0;
  std::size_t itsNY = 0;
  double itsDX = 0;
  double itsDY = 0;

  struct Cropping
  {
    bool crop = false;     // Is cropping in use ?
    bool cropped = false;  // Is grid cropped ?
    bool cropMan = false;  // If set, crop data manually from the grid (CroppedValues() was not used
                           // to get the data)

    // For manual cropping:
    int bottomLeftX = 0, bottomLeftY = 0;      // Cropped grid bottom left x and y coordinate
    int topRightX = 0, topRightY = 0;          // Cropped grid top right x and y coordinate
    std::size_t gridSizeX = 0, gridSizeY = 0;  // Cropped grid x and y size
  };

  Cropping itsCropping;

  std::shared_ptr<NFmiQueryData> itsQueryData;

  TimeSeries::OptionParsers::ParameterList::const_iterator itsParamIterator;
  TimeSeries::OptionParsers::ParameterList itsDataParams;
  TimeSeries::TimeSeriesGenerator::LocalTimeList itsDataTimes;
  Scaling::const_iterator itsScalingIterator;

  long itsDataTimeStep = 0;
  std::size_t itsTimeIndex = 0;
  std::size_t itsLevelIndex = 0;
  std::size_t itsGridIndex = 0;

  Engine::Querydata::Q itsQ;    // Q for input querydata file
  Engine::Querydata::Q itsCPQ;  // Q for in-memory querydata object containing current parameter
  Fmi::DateTime itsOriginTime;
  Fmi::DateTime itsFirstDataTime;
  Fmi::DateTime itsLastDataTime;

  std::string getWKT(OGRSpatialReference *geometrySRS) const;
  void extractSpheroidFromGeom(OGRSpatialReference *geometrySRS,
                               const std::string &areaWKT,
                               std::string &ellipsoid,
                               double &radiusOrSemiMajor,
                               double &invFlattening,
                               const char *crsName = "crs");

 private:
  DataStreamer();

  bool resetDataSet();

  void checkDataTimeStep(long timeStep = -1);

  void getBBox(const std::string &bbox);
  void getRegLLBBox(Engine::Querydata::Q q);
  void getBBox(Engine::Querydata::Q q, const NFmiArea &sourceArea, OGRSpatialReference &targetSRS);
  void getBBox(Engine::Querydata::Q q,
               const NFmiArea &sourceArea,
               OGRSpatialReference &targetSRS,
               OGRSpatialReference *targetLLSRS);
  void getRegLLBBox(Engine::Querydata::Q q,
                    const NFmiArea &sourceArea,
                    OGRSpatialReference &targetSRS);
  std::string getRegLLBBoxStr(Engine::Querydata::Q q,
                              const NFmiArea &sourceArea,
                              const std::string &projection);
  std::string getRegLLBBoxStr(Engine::Querydata::Q q);
  void getLLBBox(Engine::Querydata::Q q);

  void setSteppedGridSize();
  bool setRequestedGridSize(const NFmiArea &area,
                            std::size_t nativeGridSizeX,
                            std::size_t nativeGridSizeY);
  void setNativeGridResolution(const NFmiArea &nativeArea,
                               std::size_t nativeGridSizeX,
                               std::size_t nativeGridSizeY);
  void setCropping(const NFmiGrid &grid);

  void setTransformedCoordinates(Engine::Querydata::Q q, const NFmiArea *area);
  void coordTransform(Engine::Querydata::Q q, const NFmiArea *area);

  std::string getGridCenterBBoxStr() const;

  void cachedProjGridValues(Engine::Querydata::Q q, NFmiGrid &wantedGrid, const NFmiMetTime *mt);

  bool isLevelAvailable(Engine::Querydata::Q q, int &requestedLevel, bool &exactLevel) const;

  void createArea(Engine::Querydata::Q q,
                  const NFmiArea &nativeArea,
                  unsigned long nativeClassId,
                  std::size_t nativeGridSizeX,
                  std::size_t nativeGridSizeY);
  void createGrid(const NFmiArea &area,
                  std::size_t nativeGridSizeX,
                  std::size_t nativeGridSizeY,
                  bool interpolation);
  bool getAreaAndGrid(Engine::Querydata::Q q,
                      bool interpolation,
                      const NFmiArea **area,
                      NFmiGrid **grid);

  NFmiVPlaceDescriptor makeVPlaceDescriptor(Engine::Querydata::Q q,
                                            bool requestLevels = false,
                                            bool nativeLevels = false) const;
  NFmiParamDescriptor makeParamDescriptor(
      Engine::Querydata::Q q,
      const std::list<FmiParameterName> &currentParams = std::list<FmiParameterName>()) const;
  NFmiTimeDescriptor makeTimeDescriptor(Engine::Querydata::Q q,
                                        bool requestTimes = false,
                                        bool nativeTimes = false) const;

  Engine::Querydata::Q getCurrentParamQ(const std::list<FmiParameterName> &currentParams) const;

  void nextParam(Engine::Querydata::Q q);

  // data members

  TimeSeries::TimeSeriesGenerator::LocalTimeList::const_iterator itsTimeIterator;

  Scaling itsValScaling;

  std::optional<BBoxCorners> itsRegBoundingBox;

  bool itsLevelRng = false;
  bool itsHeightRng = false;
  bool itsRisingLevels = false;
  bool itsProjectionChecked = false;
  bool itsUseNativeProj = false;
  bool itsUseNativeBBox = false;
  bool itsUseNativeGridSize = false;
  bool itsRetainNativeGridResolution = false;

  std::list<int>::const_iterator itsLevelIterator;

  const Engine::Querydata::Engine *itsQEngine = nullptr;
  const Engine::Grid::Engine *itsGridEngine = nullptr;
  const Engine::Geonames::Engine *itsGeoEngine = nullptr;

  std::string itsDataChunk;

  bool itsMultiFile = false;

  NFmiDataMatrix<NFmiLocationCache> itsLocCache;

  // Grid support
  //

  class GridMetaData
  {
   public:
    class GridIterator
    {
     public:
      GridIterator(GridMetaData *gM) : gridMetaData(gM) { init = true; }

      bool atEnd();
      bool hasData(T::GeometryId &geometryId, T::ParamLevelId &gridLevelType, int &level, int test);
      GridIterator &nextParam();
      GridIterator &operator++();
      GridIterator operator++(int);

     private:
      bool init;
      GridMetaData *gridMetaData;
    };

    GridMetaData(DataStreamer *dS, std::string producerName, bool paramOrder) : gridIterator(this)
    {
      dataStreamer = dS;
      producer = producerName;
      paramLevelId = GridFmiLevelTypeNone;
      relativeUV = false;
      queryOrderParam = paramOrder;
    }

    std::string producer;
    std::string crs;                        // grid.crs/grid.original.crs
    T::GridProjection projType;             // wkt PROJECTION or p4 EXTENSION
    std::string projection;                 //
    bool relativeUV;                        // QueryServer::Query grid.original.relativeUV
    std::optional<BBoxCorners> targetBBox;  // target projection native coordinate bbox
    double southernPoleLat;                 // wkt p4 EXTENSION o_lat_p
    double southernPoleLon;                 // wkt p4 EXTENSION o_lon_p
    std::unique_ptr<double> rotLongitudes;  // rotated coords for rotlat grid
    std::unique_ptr<double> rotLatitudes;   //

    typedef std::map<std::string, std::set<std::string>> StringMapSet;
    typedef StringMapSet OriginTimeTimes;
    typedef std::map<T::ParamLevel, OriginTimeTimes> LevelOriginTimes;
    typedef std::map<T::GeometryId, LevelOriginTimes> GeometryLevels;
    typedef std::map<std::string, GeometryLevels> ParamGeometries;

    ParamGeometries paramGeometries;

    Fmi::DateTime originTime;      // Set if fixed (latest non-multifile or given) otime
    Fmi::DateTime gridOriginTime;  // otime of current grid (fixed or latest multifile)
    T::ForecastType forecastType;
    T::ForecastNumber forecastNumber;
    T::GeometryId geometryId;
    StringMapSet originTimeParams;
    std::map<std::string, std::set<T::ParamLevel>> originTimeLevels;
    StringMapSet originTimeTimes;
    std::map<std::string, std::string> paramKeys;
    std::map<std::string, T::ParamLevelId> paramLevelIds;
    T::ParamLevelId paramLevelId;

    bool queryOrderParam;

    Fmi::DateTime selectGridLatestValidOriginTime();
    const std::string &getLatestOriginTime(Fmi::DateTime *originTime = NULL,
                                           const Fmi::DateTime *validTime = NULL) const;
    bool getDataTimeRange(const std::string &originTimeStr,
                          Fmi::DateTime &firstTime,
                          Fmi::DateTime &lastTime,
                          long &timeStep) const;
    std::shared_ptr<SmartMet::Engine::Querydata::ValidTimeList> getDataTimes(
        const std::string &originTimeStr) const;

    GridIterator &getGridIterator() { return gridIterator; }

   private:
    DataStreamer *dataStreamer;  // To access chunking loop iterators
    GridIterator gridIterator;   //
  };

  void generateGridValidTimeList(Query &query,
                                 Fmi::DateTime &oTime,
                                 Fmi::DateTime &sTime,
                                 Fmi::DateTime &eTime);
  void setGridLevels(const Producer &producer, const Query &query);
  void getParameterDetailsFromContentData(
      const std::string &paramName, SmartMet::Engine::Grid::ParameterDetails_vec &parameterDetails);
  bool hasRequestedGridData(const Producer &producer,
                            Fmi::DateTime &oTime,
                            Fmi::DateTime &sTime,
                            Fmi::DateTime &eTime);
  bool isGridLevelRequested(const Producer &producer,
                            const Query &query,
                            FmiLevelType mappingLevelType,
                            int level) const;
  void buildGridQuery(SmartMet::QueryServer::Query &, T::ParamLevelId gridLevelType, int level);
  void getGridLLBBox();
  std::string getGridLLBBoxStr();
  void setGridSize(size_t gridSizeX, size_t gridSizeY);
  void getGridBBox();
  void getGridProjection(const QueryServer::Query &gridQuery);
  void regLLToGridRotatedCoords(const QueryServer::Query &gridQuery);
  void getGridOrigo(const QueryServer::Query &gridQuery);
  bool setDataTimes(const QueryServer::Query &gridQuery);
  bool getGridQueryInfo(const QueryServer::Query &gridQuery);
  std::size_t bufferIndex() const;
  void extractGridData(std::string &chunk);

 protected:
  static const long gribMissingValue = 9999;

  GridMetaData itsGridMetaData;
  QueryServer::Query itsGridQuery;

  QueryServer::ParameterValues_sptr getValueListItem(const QueryServer::Query &gridQuery) const;
};

}  // namespace Download
}  // namespace Plugin
}  // namespace SmartMet

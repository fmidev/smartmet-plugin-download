// ======================================================================
/*!
 * \brief SmartMet download service plugin; data streaming
 */
// ======================================================================

#pragma once

#include "Config.h"
#include "Query.h"

#include <engines/geonames/Engine.h>
#include <engines/querydata/Model.h>
#include <engines/querydata/ValidTimeList.h>
#include <engines/grid/Engine.h>
#include <grid-files/grid/Typedefs.h>
#include <spine/HTTP.h>
#include <spine/TimeSeriesGenerator.h>

#include <newbase/NFmiGrid.h>
#include <newbase/NFmiRotatedLatLonArea.h>

#include <boost/date_time/posix_time/posix_time.hpp>

#include <ogr_spatialref.h>

typedef std::list<std::pair<float, float>> Scaling;

typedef struct
{
  NFmiPoint bottomLeft;
  NFmiPoint topRight;
} BBoxCorners;

#define BOTTOMLEFT 0
#define TOPRIGHT 1

namespace SmartMet
{
namespace Plugin
{
namespace Download
{
// For external usage

bool isSurfaceLevel(FmiLevelType levelType);
bool isPressureLevel(FmiLevelType levelType);
bool isHybridLevel(FmiLevelType levelType);
bool isHeightOrDepthLevel(FmiLevelType levelType);
bool isHeightLevel(FmiLevelType levelType, int levelValue);
bool isDepthLevel(FmiLevelType levelType, int levelValue);
double getProjParam(const OGRSpatialReference &srs,
                    const char *param,
                    bool ignoreErr = false,
                    double defaultValue = 0.0);

// Resource management
//
// ResMgr class is the sole owner and thus responsible for releasing *ALL* objects created by
// calling its methods:
//
// 	createArea() 						NFmiArea object
// 	getGrid()							NFmiGrid object
// 	cloneGeogCS(), cloneCS() 			OGRSpatialReference objects
// 	getCoordinateTransformation()		OGRCoordinateTransformation objects
//
// Only one area and/or grid can exist at a given time; old object is released if a new object is
// created.
//
// Note: In download plugin implementation area is created only once (if at all) per processed
// query. Multiple grid's will
// be created during execution of a query if the query spans over multiple querydatas, native
// gridsize or given gridresolution
// is used and data gridsize changes.
//
// Note: OGRSpatialReference object pointed by geometrySRS (if nonnull) is one of the objects in
// 'spatialReferences' list;
// the object is *NOT* released using the geometrySRS pointer.

class ResMgr : private boost::noncopyable
{
 public:
  ResMgr();
  ~ResMgr();

  void createArea(std::string &projection);
  const NFmiArea *getArea();

  NFmiGrid *getGrid(const NFmiArea &a, size_t gsX, size_t gsY);
  NFmiGrid *getGrid() const { return grid.get(); }
  OGRSpatialReference *cloneGeogCS(const OGRSpatialReference &, bool isGeometrySRS = false);
  OGRSpatialReference *cloneCS(const OGRSpatialReference &, bool isGeometrySRS = false);
  OGRCoordinateTransformation *getCoordinateTransformation(OGRSpatialReference *,
                                                           OGRSpatialReference *,
                                                           bool isGeometrySRS = false);
  OGRSpatialReference *getGeometrySRS() const { return geometrySRS; }

 private:
  boost::shared_ptr<NFmiArea> area;
  boost::shared_ptr<NFmiGrid> grid;
  std::list<OGRSpatialReference *> spatialReferences;
  std::list<OGRCoordinateTransformation *> transformations;
  OGRSpatialReference *geometrySRS;

  void createGrid(const NFmiArea &a, size_t gsX, size_t gsY);
  bool hasGrid(const NFmiArea &a, size_t gsX, size_t gsY);
};

// Data streaming
//

class DataStreamer : public Spine::HTTP::ContentStreamer
{
 public:
  static const long minutesInDay = 24 * 60;
  static const long minutesInMonth = 31 * minutesInDay;
  static const long minutesInYear = 365 * minutesInDay;

  DataStreamer(const Spine::HTTP::Request &req, const Config &config, const Producer &producer,
               const ReqParams &regParams);
  virtual ~DataStreamer();

  void generateValidTimeList(const Engine::Querydata::Q &q,
                             Query &query,
                             boost::posix_time::ptime &oTime,
                             boost::posix_time::ptime &sTime,
                             boost::posix_time::ptime &eTime);

  void setMultiFile(bool multiFile) { itsMultiFile = multiFile; }
  void setLevels(const Query &query);
  void setParams(const Spine::OptionParsers::ParameterList &params, const Scaling &scaling);

  void setEngines(const Engine::Querydata::Engine *theQEngine,
                  const Engine::Grid::Engine *theGridEngine,
                  const Engine::Geonames::Engine *theGeoEngine)
  {
    itsQEngine = theQEngine;
    itsGridEngine = theGridEngine;
    itsGeoEngine = theGeoEngine;
  }
  const Config &getConfig() const { return itsCfg; }
  bool hasRequestedData(const Producer &producer,
                        Query &query,
                        boost::posix_time::ptime &oTime,
                        boost::posix_time::ptime &sTime,
                        boost::posix_time::ptime &eTime);

  void resetDataSet() { resetDataSet(true); }

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
                                std::string &chunk) { };

 protected:
  const Spine::HTTP::Request &itsRequest;

  const Config &itsCfg;
  ReqParams itsReqParams;
  ResMgr itsResMgr;
  const Producer &itsProducer;

  FmiDirection itsGridOrigo;

  bool isDone;
  NFmiDataMatrix<float> itsGridValues;
  unsigned int itsChunkLength;
  unsigned int itsMaxMsgChunks;

  bool setMeta;
  FmiLevelType levelType;  // Data level type; height level data with negative levels is stored as
                           // kFmiDepth
  FmiLevelType nativeLevelType;  // Native data level type
  bool itsPositiveLevels;        // true if (depth) levels are positive
  Query::Levels itsDataLevels;

  BBoxCorners itsBoundingBox;             // Target projection latlon bounding box
  NFmiDataMatrix<NFmiPoint> srcLatLons;   // Source grid latlons
  NFmiDataMatrix<NFmiPoint> tgtLatLons;   // Target grid latlons
  NFmiDataMatrix<NFmiPoint> tgtWorldXYs;  // Target grid projected coordinates
  size_t itsReqGridSizeX;
  size_t itsReqGridSizeY;
  size_t itsNX;
  size_t itsNY;
  double itsDX;
  double itsDY;
  struct
  {
    bool crop;     // Is cropping in use ?
    bool cropped;  // Is grid cropped ?
    bool cropMan;  // If set, crop data manually from the grid (CroppedValues() was not used to get
                   // the data)
                   //
                   // For manual cropping:
                   //
    int bottomLeftX, bottomLeftY;  // 		Cropped grid bottom left x and y coordinate
    int topRightX, topRightY;      // 		Cropped grid top right x and y coordinate
    size_t gridSizeX, gridSizeY;   // 		Cropped grid x and y size
  } cropping;

  boost::shared_ptr<NFmiQueryData> itsQueryData;
  void createQD(const NFmiGrid &g);

  void extractData(std::string &chunk);

  Spine::OptionParsers::ParameterList::const_iterator itsParamIterator;
  Spine::OptionParsers::ParameterList itsDataParams;
  Spine::TimeSeriesGenerator::LocalTimeList itsDataTimes;
  Scaling::const_iterator itsScalingIterator;

  virtual void paramChanged(size_t nextParamOffset = 1) {}
  long itsDataTimeStep;
  size_t itsTimeIndex;
  size_t itsLevelIndex;

  Engine::Querydata::Q itsQ;	// Q for input querydata file
  Engine::Querydata::Q itsCPQ;	// Q for in-memory querydata object containing current parameter
  boost::posix_time::ptime itsOriginTime;
  boost::posix_time::ptime itsFirstDataTime;
  boost::posix_time::ptime itsLastDataTime;

 private:
  DataStreamer();

  Spine::TimeSeriesGenerator::LocalTimeList::const_iterator itsTimeIterator;

  Scaling itsValScaling;

  boost::optional<BBoxCorners> itsRegBoundingBox;

  bool itsLevelRng;
  bool itsHeightRng;
  bool itsRisingLevels;
  bool itsProjectionChecked;
  bool itsUseNativeProj;
  bool itsUseNativeBBox;
  bool itsUseNativeGridSize;
  bool itsRetainNativeGridResolution;

  Query::Levels::const_iterator itsLevelIterator;

  const Engine::Querydata::Engine *itsQEngine;
  const Engine::Grid::Engine *itsGridEngine;
  const Engine::Geonames::Engine *itsGeoEngine;
  NFmiDataMatrix<float> itsDEMMatrix;
  NFmiDataMatrix<bool> itsWaterFlagMatrix;

  std::string itsDataChunk;

  bool itsMultiFile;

  void resetDataSet(bool getFirstChunk)
  {
    itsLevelIterator = itsDataLevels.begin();
    itsParamIterator = itsDataParams.begin();
    itsTimeIterator = itsDataTimes.begin();
    itsScalingIterator = itsValScaling.begin();

    itsTimeIndex = itsLevelIndex = 0;
    if (itsQ)
      itsQ->resetTime();

    itsDataChunk.clear();

    if (getFirstChunk)
    {
      extractData(itsDataChunk);
    }
  }

  void checkDataTimeStep(long timeStep = -1);

  void getRegLLBBox(Engine::Querydata::Q q);
  std::string getRegLLBBoxStr(Engine::Querydata::Q q);
  void getLLBBox(Engine::Querydata::Q q);

  void setSteppedGridSize();
  bool setRequestedGridSize(const NFmiArea &area, size_t nativeGridSizeX, size_t nativeGridSizeY);
  void setNativeGridResolution(const NFmiArea &nativeArea,
                               size_t nativeGridSizeX,
                               size_t nativeGridSizeY);
  void setCropping(const NFmiGrid &grid);

  void setTransformedCoordinates(Engine::Querydata::Q q, const NFmiArea *area);
  void coordTransform(Engine::Querydata::Q q, const NFmiArea *area);

  std::string getGridCenterBBoxStr(bool usenativeproj, const NFmiGrid &grid) const;

  NFmiDataMatrix<NFmiLocationCache> locCache;
  void cachedProjGridValues(Engine::Querydata::Q q,
                            NFmiGrid &wantedGrid,
                            const NFmiMetTime *mt,
                            NFmiDataMatrix<float> *demValues = nullptr,
                            NFmiDataMatrix<bool> *waterFlags = nullptr);

  bool isLevelAvailable(Engine::Querydata::Q q, int &requestedLevel, bool &exactLevel) const;

  void createArea(Engine::Querydata::Q q,
                  const NFmiArea &nativeArea,
                  unsigned long nativeClassId,
                  size_t nativeGridSizeX,
                  size_t nativeGridSizeY);
  void createGrid(const NFmiArea &area,
                  size_t nativeGridSizeX,
                  size_t nativeGridSizeY,
                  bool interpolation);
  bool getAreaAndGrid(Engine::Querydata::Q q,
                      bool interpolation,
                      bool landScaping,
                      const NFmiArea **area,
                      NFmiGrid **grid);

  NFmiVPlaceDescriptor makeVPlaceDescriptor(Engine::Querydata::Q q,
                                            bool allLevels = false) const;
  NFmiParamDescriptor makeParamDescriptor(Engine::Querydata::Q q,
                                          const std::list<FmiParameterName> &currentParams = std::list<FmiParameterName>()) const;
  NFmiTimeDescriptor makeTimeDescriptor(Engine::Querydata::Q q,
                                        bool nativeTimes = false);

  Engine::Querydata::Q getCurrentParamQ(const std::list<FmiParameterName> &currentParams) const;

  void nextParam(Engine::Querydata::Q q);

  // Grid support
  //

  class GridMetaData
  {
   public:
    /*
      1;GROUND;Ground or water surface;
      2;PRESSURE;Pressure level;
      3;HYBRID;Hybrid level;
      4;ALTITUDE;Altitude;
      5;TOP;Top of atmosphere;
      6;HEIGHT;Height above ground in meters;
      7;MEANSEA;Mean sea level;
      8;ENTATM;Entire atmosphere;
      9;GROUND_DEPTH;Layer between two depths below land surface;
      10;DEPTH;Depth below some surface;
      11;PRESSURE_DELTA;Level at specified pressure difference from ground to level;
      12;MAXTHETAE;Level where maximum equivalent potential temperature is found;
      13;HEIGHT_LAYER;Layer between two metric heights above ground;
      14;DEPTH_LAYER;Layer between two depths below land surface;
      15;ISOTHERMAL;Isothermal level, temperature in 1/100 K;
      16;MAXWIND;Maximum wind level;
    */
    static const T::ParamLevelIdType GridFMILevelTypeNone             = 0;
    static const T::ParamLevelIdType GridFMILevelTypeGround	      = 1;
    static const T::ParamLevelIdType GridFMILevelTypePressure         = 2;
    static const T::ParamLevelIdType GridFMILevelTypeHybrid	      = 3;
    static const T::ParamLevelIdType GridFMILevelTypeHeight	      = 6;
    static const T::ParamLevelIdType GridFMILevelTypeMeanSea	      = 7;
    static const T::ParamLevelIdType GridFMILevelTypeEntireAtmosphere = 8;
    static const T::ParamLevelIdType GridFMILevelTypeDepth	      = 10;

    class GridIterator
    {
     public:
      GridIterator(GridMetaData *gM) : gridMetaData(gM) { init = true; }

      bool atEnd();
      bool hasData(T::ParamLevelIdType &gridLevelType, int &level);
      GridIterator& operator++();
      GridIterator operator++(int);

     private:
      bool init;
      GridMetaData *gridMetaData;
    };

    GridMetaData(DataStreamer *dS, std::string producerName)
      : gridIterator(this)
    {
      dataStreamer = dS;
      producer = producerName;
      paramLevelId = GridFMILevelTypeNone;
      relativeUV = false;
    }

    std::string producer;
    std::string crs;		    	    // grid.crs/grid.original.crs
    T::GridProjection projType;		    // wkt PROJECTION or p4 EXTENSION
    std::string projection;		    //
    std::string ellipsoid;                  // wkt SPHEROID
    double earthRadiusOrSemiMajorInMeters;  //
    boost::optional<double> flattening;	    //
    std::string flatteningStr;		    //
    bool relativeUV;			    // QueryServer::Query grid.original.relativeUV
    boost::optional<BBoxCorners> rotLLBBox; // QueryServer::Query grid.bbox for rotlat
    double southernPoleLat;		    // wkt p4 EXTENSION o_lat_p
    double southernPoleLon;		    // wkt p4 EXTENSION o_lon_p
    std::unique_ptr<double> rotLongitudes;  // rotated coords for rotlat grid
    std::unique_ptr<double> rotLatitudes;   //

    typedef std::map<std::string, std::set<std::string>> StringMapSet;
    typedef StringMapSet OriginTimeTimes;
    typedef std::map<T::ParamLevel, OriginTimeTimes> LevelOriginTimes;
    typedef std::map<T::GeometryId, LevelOriginTimes> GeometryLevels;
    typedef std::map<std::string, GeometryLevels> ParamGeometries;

    ParamGeometries paramGeometries;

    boost::posix_time::ptime originTime;     // Set if fixed (latest non-multifile or given) otime
    boost::posix_time::ptime gridOriginTime; // otime of current grid (fixed or latest multifile)
    T::ForecastNumber gridEnsemble;          // ensemble of current grid
    T::GeometryId geometryId;
    StringMapSet originTimeParams;
    std::map<std::string, std::set<T::ParamLevel>> originTimeLevels;
    StringMapSet originTimeTimes;
    std::map<std::string, std::string> paramKeys;
    std::map<std::string, T::ParamLevelIdType> paramLevelIds;
    T::ParamLevelIdType paramLevelId;

    boost::posix_time::ptime selectGridLatestValidOriginTime();
    const std::string &getLatestOriginTime(boost::posix_time::ptime *originTime = NULL,
                                           const boost::posix_time::ptime *validTime = NULL) const;
    bool getDataTimeRange(const std::string &originTimeStr,
                          boost::posix_time::ptime &firstTime,
                          boost::posix_time::ptime &lastTime,
                          long &timeStep) const;
    boost::shared_ptr<SmartMet::Engine::Querydata::ValidTimeList>
      getDataTimes(const std::string &originTimeStr) const;

    GridIterator &getGridIterator() { return gridIterator; }

   private:
    DataStreamer *dataStreamer; // To access chunking loop iterators
    GridIterator gridIterator;  //
  };

  void generateGridValidTimeList(Query &query,
                                 boost::posix_time::ptime &oTime,
                                 boost::posix_time::ptime &sTime,
                                 boost::posix_time::ptime &eTime);
  void setGridLevels(const Producer &producer, const Query &query);
  bool hasRequestedGridData(const Producer &producer,
                            Query &query,
                            boost::posix_time::ptime &oTime,
                            boost::posix_time::ptime &sTime,
                            boost::posix_time::ptime &eTime);
  bool isGridLevelRequested(const Producer &producer,
                            const Query &query,
                            FmiLevelType mappingLevelType,
                            int level) const;
  bool buildGridQuery(SmartMet::QueryServer::Query&, T::ParamLevelIdType gridLevelType, int level);
  void getGridLLBBox();
  std::string getGridLLBBoxStr();
  void setGridSize(size_t gridSizeX, size_t gridSizeY);
  void getGridBBox(QueryServer::Query &gridQuery);
  void getGridProjection(const QueryServer::Query &gridQuery);
  void regLLToGridRotatedCoords(const QueryServer::Query &gridQuery);
  bool getGridQueryInfo(const QueryServer::Query &gridQuery);
  void extractGridData(std::string &chunk);

 protected:
  static const long gribMissingValue = 9999;

  GridMetaData itsGridMetaData;
  QueryServer::Query itsGridQuery;
};

}  // namespace Download
}  // namespace Plugin
}  // namespace SmartMet

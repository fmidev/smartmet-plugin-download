// ======================================================================
/*!
 * \brief SmartMet download service plugin; data streaming
 */
// ======================================================================

#pragma once

#include "Query.h"
#include "Config.h"

#include <spine/HTTP.h>
#include <spine/TimeSeriesGenerator.h>
#include <engines/querydata/Model.h>
#include <engines/geonames/Engine.h>

#include <newbase/NFmiRotatedLatLonArea.h>
#include <newbase/NFmiGrid.h>

#include <boost/date_time/posix_time/posix_time.hpp>

#include <gdal/ogr_spatialref.h>

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
  OGRSpatialReference *cloneGeogCS(const OGRSpatialReference &);
  OGRSpatialReference *cloneCS(const OGRSpatialReference &);
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

class DataStreamer : public SmartMet::Spine::HTTP::ContentStreamer
{
 public:
  static const long minutesInDay = 24 * 60;
  static const long minutesInMonth = 31 * minutesInDay;
  static const long minutesInYear = 365 * minutesInDay;

  DataStreamer(const SmartMet::Spine::HTTP::Request &req,
               const Config &config,
               const Producer &producer);
  virtual ~DataStreamer();

  void setRequestParams(const ReqParams &rp) { itsReqParams = rp; }
  void generateValidTimeList(const SmartMet::Engine::Querydata::Q &q,
                             Query &query,
                             boost::posix_time::ptime &oTime,
                             boost::posix_time::ptime &sTime,
                             boost::posix_time::ptime &eTime);

  void setLevels(const Query &query);
  void setParams(const SmartMet::Spine::OptionParsers::ParameterList &params,
                 const Scaling &scaling);

  void setGeonames(const SmartMet::Engine::Geonames::Engine *theGeoEngine)
  {
    itsGeoEngine = theGeoEngine;
  }

  const Config &getConfig() const { return itsCfg; }
  bool hasRequestedData(const Producer &producer);

  void resetDataSet()
  {
    itsLevelIterator = itsDataLevels.begin();
    itsParamIterator = itsDataParams.begin();
    itsTimeIterator = itsDataTimes.begin();
    itsScalingIterator = itsValScaling.begin();

    itsTimeIndex = itsLevelIndex = 0;
    itsQ->resetTime();
  }

  virtual std::string getChunk() = 0;

  virtual void getDataChunk(SmartMet::Engine::Querydata::Q q,
                            const NFmiArea *area,
                            NFmiGrid *grid,
                            int level,
                            const NFmiMetTime &mt,
                            NFmiDataMatrix<float> &values,
                            std::string &chunk) = 0;

 protected:
  const SmartMet::Spine::HTTP::Request &itsRequest;

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

  SmartMet::Spine::OptionParsers::ParameterList::const_iterator itsParamIterator;
  SmartMet::Spine::OptionParsers::ParameterList itsDataParams;
  SmartMet::Spine::TimeSeriesGenerator::LocalTimeList itsDataTimes;
  Scaling::const_iterator itsScalingIterator;

  virtual void paramChanged() {}
  long itsDataTimeStep;
  size_t itsTimeIndex;
  size_t itsLevelIndex;

  SmartMet::Engine::Querydata::Q itsQ;
  boost::posix_time::ptime itsOriginTime;
  boost::posix_time::ptime itsFirstDataTime;
  boost::posix_time::ptime itsLastDataTime;

 private:
  DataStreamer();

  SmartMet::Spine::TimeSeriesGenerator::LocalTimeList::const_iterator itsTimeIterator;

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

  const SmartMet::Engine::Geonames::Engine *itsGeoEngine;
  NFmiDataMatrix<float> itsDEMMatrix;
  NFmiDataMatrix<bool> itsWaterFlagMatrix;

  void checkDataTimeStep();

  void getRegLLBBox(SmartMet::Engine::Querydata::Q q);
  std::string getRegLLBBoxStr(SmartMet::Engine::Querydata::Q q);
  void getLLBBox(SmartMet::Engine::Querydata::Q q);

  void setSteppedGridSize();
  bool setRequestedGridSize(const NFmiArea &area, size_t nativeGridSizeX, size_t nativeGridSizeY);
  void setNativeGridResolution(const NFmiArea &nativeArea,
                               size_t nativeGridSizeX,
                               size_t nativeGridSizeY);
  void setCropping(const NFmiGrid &grid);

  void setTransformedCoordinates(SmartMet::Engine::Querydata::Q q, const NFmiArea *area);
  void coordTransform(SmartMet::Engine::Querydata::Q q, const NFmiArea *area);

  std::string getGridCenterBBoxStr(bool usenativeproj, const NFmiGrid &grid) const;

  NFmiDataMatrix<NFmiLocationCache> locCache;
  void cachedProjGridValues(SmartMet::Engine::Querydata::Q q,
                            NFmiGrid &wantedGrid,
                            const NFmiMetTime *mt,
                            NFmiDataMatrix<float> *demValues = NULL,
                            NFmiDataMatrix<bool> *waterFlags = NULL);

  bool isLevelAvailable(SmartMet::Engine::Querydata::Q q,
                        int &requestedLevel,
                        bool &exactLevel) const;

  void createArea(SmartMet::Engine::Querydata::Q q,
                  const NFmiArea &nativeArea,
                  unsigned long nativeClassId,
                  size_t nativeGridSizeX,
                  size_t nativeGridSizeY);
  void createGrid(const NFmiArea &area,
                  size_t nativeGridSizeX,
                  size_t nativeGridSizeY,
                  bool interpolation);
  bool getAreaAndGrid(SmartMet::Engine::Querydata::Q q,
                      bool interpolation,
                      bool landScaping,
                      const NFmiArea **area,
                      NFmiGrid **grid);

  NFmiVPlaceDescriptor makeVPlaceDescriptor(SmartMet::Engine::Querydata::Q q) const;
  NFmiParamDescriptor makeParamDescriptor(SmartMet::Engine::Querydata::Q q) const;
  NFmiTimeDescriptor makeTimeDescriptor(SmartMet::Engine::Querydata::Q q);

  void nextParam(SmartMet::Engine::Querydata::Q q);
};

}  // namespace Download
}  // namespace Plugin
}  // namespace SmartMet

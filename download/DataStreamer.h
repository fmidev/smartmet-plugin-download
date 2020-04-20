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
#include <boost/date_time/posix_time/posix_time.hpp>
#include <engines/geonames/Engine.h>
#include <engines/querydata/Model.h>
#include <gis/CoordinateMatrix.h>
#include <gis/SpatialReference.h>
#include <newbase/NFmiGrid.h>
#include <spine/HTTP.h>
#include <spine/TimeSeriesGenerator.h>

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

  virtual ~DataStreamer();
  DataStreamer(const Spine::HTTP::Request &req, const Config &config, const Producer &producer);

  void setRequestParams(const ReqParams &rp) { itsReqParams = rp; }

  void generateValidTimeList(const Engine::Querydata::Q &q,
                             Query &query,
                             boost::posix_time::ptime &oTime,
                             boost::posix_time::ptime &sTime,
                             boost::posix_time::ptime &eTime);

  void setLevels(const Query &query);
  void setParams(const Spine::OptionParsers::ParameterList &params, const Scaling &scaling);

  void setEngines(const Engine::Querydata::Engine *theQEngine,
                  const Engine::Geonames::Engine *theGeoEngine);

  const Config &getConfig() const { return itsCfg; }
  bool hasRequestedData(const Producer &producer);

  void resetDataSet() { resetDataSet(true); }

  virtual std::string getChunk() = 0;

  virtual void getDataChunk(Engine::Querydata::Q q,
                            const NFmiArea *area,
                            NFmiGrid *grid,
                            int level,
                            const NFmiMetTime &mt,
                            NFmiDataMatrix<float> &values,
                            std::string &chunk) = 0;

 protected:
  void createQD(const NFmiGrid &g);
  void extractData(std::string &chunk);
  virtual void paramChanged() {}

  const Spine::HTTP::Request &itsRequest;

  const Config &itsCfg;
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
  bool itsPositiveLevels = false;   // true if (depth) levels are positive
  Query::Levels itsDataLevels;

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

  boost::shared_ptr<NFmiQueryData> itsQueryData;

  Spine::OptionParsers::ParameterList::const_iterator itsParamIterator;
  Spine::OptionParsers::ParameterList itsDataParams;
  Spine::TimeSeriesGenerator::LocalTimeList itsDataTimes;
  Scaling::const_iterator itsScalingIterator;

  long itsDataTimeStep = 0;
  std::size_t itsTimeIndex = 0;
  std::size_t itsLevelIndex = 0;

  Engine::Querydata::Q itsQ;    // Q for input querydata file
  Engine::Querydata::Q itsCPQ;  // Q for in-memory querydata object containing current parameter
  boost::posix_time::ptime itsOriginTime;
  boost::posix_time::ptime itsFirstDataTime;
  boost::posix_time::ptime itsLastDataTime;

 private:
  DataStreamer();
  void resetDataSet(bool getFirstChunk);

  void checkDataTimeStep();

  void getBBoxStr(const std::string &bbox);
  void getRegLLBBox(Engine::Querydata::Q q);
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

  std::string getGridCenterBBoxStr(bool usenativeproj, const NFmiGrid &grid) const;

  void cachedProjGridValues(Engine::Querydata::Q q,
                            NFmiGrid &wantedGrid,
                            const NFmiMetTime *mt,
                            NFmiDataMatrix<float> *demValues = nullptr,
                            NFmiDataMatrix<bool> *waterFlags = nullptr);

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
                      bool landScaping,
                      const NFmiArea **area,
                      NFmiGrid **grid);

  NFmiVPlaceDescriptor makeVPlaceDescriptor(Engine::Querydata::Q q, bool allLevels = false) const;
  NFmiParamDescriptor makeParamDescriptor(
      Engine::Querydata::Q q,
      const std::list<FmiParameterName> &currentParams = std::list<FmiParameterName>()) const;
  NFmiTimeDescriptor makeTimeDescriptor(Engine::Querydata::Q q, bool nativeTimes = false);

  Engine::Querydata::Q getCurrentParamQ(const std::list<FmiParameterName> &currentParams) const;

  void nextParam(Engine::Querydata::Q q);

  // data members

  Spine::TimeSeriesGenerator::LocalTimeList::const_iterator itsTimeIterator;

  Scaling itsValScaling;

  boost::optional<BBoxCorners> itsRegBoundingBox;

  bool itsLevelRng = false;
  bool itsHeightRng = false;
  bool itsRisingLevels = false;
  bool itsProjectionChecked = false;
  bool itsUseNativeProj = false;
  bool itsUseNativeBBox = false;
  bool itsUseNativeGridSize = false;
  bool itsRetainNativeGridResolution = false;

  Query::Levels::const_iterator itsLevelIterator;

  const Engine::Querydata::Engine *itsQEngine = nullptr;
  const Engine::Geonames::Engine *itsGeoEngine = nullptr;
  NFmiDataMatrix<float> itsDEMMatrix;
  NFmiDataMatrix<bool> itsWaterFlagMatrix;

  std::string itsDataChunk;

  bool itsMultiFile = false;

  NFmiDataMatrix<NFmiLocationCache> itsLocCache;
};

}  // namespace Download
}  // namespace Plugin
}  // namespace SmartMet

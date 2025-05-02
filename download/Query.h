// ======================================================================
/*!
 * \brief Request parameter parsing
 *
 */
// ======================================================================

#pragma once

#include "Datum.h"
#include "Tools.h"

#include <optional>
#include <memory>
#include <engines/querydata/Engine.h>
#include <engines/grid/Engine.h>
#include <macgyver/TimeFormatter.h>
#include <newbase/NFmiPoint.h>
#include <spine/HTTP.h>
#include <spine/Location.h>
#include <spine/Parameter.h>
#include <timeseries/OptionParsers.h>
#include <timeseries/TimeSeriesGeneratorOptions.h>
#include <list>
#include <locale>
#include <map>
#include <set>
#include <string>
#include <vector>

// Currently used only for storing configured value for originating centre (setting 'centre = 98;');
// format (grib (for both 1 and 2), grib1, grib2 and netcdf) and value type (to be converted to) are
// not stored/used.
//
typedef std::map<std::string, long> NamedSettings;

namespace SmartMet
{
namespace Plugin
{
namespace Download
{

// ----------------------------------------------------------------------
/*!
 * \brief Individual producer settings
 */
// ----------------------------------------------------------------------

struct Producer
{
  std::set<std::string> disabledReqParams;  // Disabled url option names from config
  std::set<int> disabledDataParams;         // Disabled url 'param' option values from config
  std::set<int> gridDefaultLevels;          // Default 'level' values for grid data from config

  NamedSettings namedSettings;  // Named settings ('key = value;') to be set to output (used with
                                // grib formats only)

  bool verticalInterpolation;  // Set if vertical interpolation is allowed. Default: false

  Plugin::Download::Datum::DatumShift datumShift;  // Datum handling. Default: native
                                                   // datum (no shift). See Datum.h

  bool multiFile;  // If set, query can span over multiple grid origintimes

  Producer() : verticalInterpolation(false), multiFile(false) {}

  bool disabledReqParam(std::string param) const
  {
    return (disabledReqParams.find(param) != disabledReqParams.end());
  }

  bool disabledDataParam(int param) const
  {
    return (disabledDataParams.find(param) != disabledDataParams.end());
  }

  NamedSettings::const_iterator namedSettingsBegin() const { return (namedSettings.begin()); }
  NamedSettings::const_iterator namedSettingsEnd() const { return (namedSettings.end()); }
#ifdef USE_QENGINE_CONFIG
  std::optional<const Engine::Querydata::ProducerConfig &> qEngineProducerConfig;
#endif
};

typedef std::map<std::string, Producer> Producers;

// ----------------------------------------------------------------------
/*!
 * \brief Request parameters
 */
// ----------------------------------------------------------------------

typedef enum
{
  QueryData,
  GridMapping,  // Using newbase names and grid engine mappings
  GridContent   // Using radon names and content server data
} DataSource;

typedef enum
{
  Grib1,
  Grib2,
  NetCdf,
  QD
} OutputFormat;

typedef std::optional<std::vector<std::pair<double, double>>> BBox;
typedef std::optional<std::vector<std::pair<unsigned int, unsigned int>>> GridSize;
typedef std::optional<std::vector<std::pair<unsigned int, unsigned int>>> GridStep;
typedef std::optional<std::vector<std::pair<double, double>>> GridResolution;
typedef std::optional<std::vector<std::pair<double, double>>> GridCenter;

typedef enum
{
  P_Native,
  P_LatLon,
  P_RotLatLon,
  P_StereoGraphic,
  P_Mercator,
  P_TransverseMercator,
  P_LambertConformalConic,
  P_Epsg
} ProjType;
typedef enum
{
  A_Native = -9999999,
  A_LatLon = kNFmiLatLonArea,
  A_RotLatLon = kNFmiRotatedLatLonArea,
  A_PolarStereoGraphic = kNFmiStereographicArea,
  A_Mercator = kNFmiMercatorArea,
  A_TransverseMercator = kNFmiYKJArea,
  A_LambertConformalConic = kNFmiLambertConformalConicArea
} AreaClassId;
typedef unsigned long EpsgCode;

struct ReqParams
{
  //
  // Data source
  //
  std::string source;
  DataSource dataSource;
  //
  // Producer name
  //
  std::string producer;
  //
  // Time related parameters
  //
  std::string startTime;   // Data start time. Default: first validtime of the latest/'originTime'
                           // data
  std::string endTime;     // Data end time. Default: last validtime of the latest/'originTime' data
  std::string originTime;  // Data origin time. Default: the origin time of the latest data
  unsigned int timeSteps;  // Extract n validtimes. Default: 0; extract every validtime
  unsigned int timeStep;   // Extract every n'th validtime. Default: 0; extract every validtime
  unsigned int maxTimeSteps;  // Max # of validtimes extracted. Default: 0; extract every validtime.
                              // Currently not used

  unsigned int gridParamBlockSize;  // # of grid parameters fetched as a block (single timestep)
  unsigned int gridTimeBlockSize;   // # of grid timesteps fetched as a block (single parameter)
  unsigned int chunkSize;           // Minimum chunk length to return

  //
  // Level; pressure/hPa or hybrid or height level ranges/limits. Default: extract every level
  //
  int minLevel;  // First level to extract
  int maxLevel;  // Last level to extract
  //
  // Height (meters) ranges/limits
  //
  // Note: Currently not implemented
  //
  int minHeight;
  int maxHeight;
  //
  // Projection. Default: native projection
  //
  std::string projection;   // Newbase projection name (and parameters), e.g. 'latlon'
                            // or epsg projection code, e.g. 'epsg:4326'
                            //
  ProjType projType;        // Derived; projection type based on projection
  AreaClassId areaClassId;  // Derived; area class id based on projection
  EpsgCode epsgCode;        // Derived; epsg projection based on projection
  //
  // Bounding. Default: the native area
  //
  std::string bbox;      // Bounding box (applied to target projection); bottom left lon,lat and top
                         // right lon,lat,
                         // e.g. '6,51.3,49,70.2'
  std::string origBBox;  // Original (not adjusted to grid when cropped) bounding box; bottom left
                         // lon,lat and top right lon,lat,
                         // e.g. '6,51.3,49,70.2'
                         // - OR -
  std::string gridCenter;   // Bounding box (applied to target projection) defined by grid center
                            // lon,lat and
                            // width and height in km; e.g. '25,60,300,300'
                            //
  BBox bboxRect;            // Derived; bllon,bllat,trlon,trlat based on bbox
  GridCenter gridCenterLL;  // Derived; lon,lat,width,height based on gridCenter
  //
  // Grid size. Default: the native grid
  //
  std::string gridSize;        // Absolute gridsize (number of cells in x and y dimensions), e.g.
                               // '300,300'
                               // - OR -
  std::string gridResolution;  // Grid cell size (width,height) in km, e.g. '20,20'
                               //
  GridSize gridSizeXY;         // Derived; nx,ny based on gridSize
  GridResolution gridResolutionXY;  // Derived; width,height based on gridResolution
  //
  // Grid step. Default: extract every grid cell/value
  //
  std::string gridStep;  // Extract every x'th/y'th grid cell/value, e.g. '2,2'
                         //
  GridStep gridStepXY;   // Derived; nx,ny based on gridStep
  //
  // Output format
  //
  std::string format;         // OutputFormat value
                              //
  OutputFormat outputFormat;  // Derived; set based on format
  //
  // Packing type and tables version for grib
  //
  std::string packing;              // If given, set to grib as is
                                    //
  unsigned int grib2TablesVersion;  // If given (nonzero), set as grib2
                                    // 'gribMasterTablesVersionNumber'
  //
  // Datum handling. Default: native datum (no shift)
  //
  std::string datum;                               // DatumShift value; see Datum.h
                                                   //
  Plugin::Download::Datum::DatumShift datumShift;  // Derived; datum shift based on datum
  //
  // Misc testing
  //
  unsigned int test;

  ReqParams() {}
};

class Query
{
 public:
  Query(const Spine::HTTP::Request &req,
        const Engine::Grid::Engine *gridEngine,
        std::string &originTime,
        uint queryTestValue = 0);

  typedef std::set<int> Levels;
  Levels levels;

  std::string timeZone;

  TimeSeries::OptionParsers::ParameterOptions pOptions;
  TimeSeries::TimeSeriesGeneratorOptions tOptions;

  void parseRadonParameterName(
      const std::string &param, std::vector<std::string> &paramParts, bool expanding = false) const;
  bool parseRadonParameterName(
      const std::string &paramDef, std::vector<std::string> &paramParts, std::string &param,
      std::string &funcParamDef) const;
  bool isFunctionParameter(const std::string &param) const;
  bool isFunctionParameter(const std::string &param, std::string &funcParamDef) const;
  bool isFunctionParameter(
      const std::string &param, T::GeometryId &geometryId, T::ParamLevelId &gridLevelType, int &level) const;

  typedef std::map<uint, T::GenerationInfo> GenerationInfos;
  typedef std::map<std::string, T::ContentInfoList> ParameterContents;

  const GenerationInfos &getGenerationInfos() { return generationInfos; }
  const ParameterContents &getParameterContents() { return parameterContents; }

 private:
  Query();

  std::map<std::string, std::vector<std::string>> radonParameters;
  std::map<std::string, std::string> functionParameters;
  GenerationInfos generationInfos;
  ParameterContents parameterContents;
  typedef std::map<std::string, uint> OriginTimeGenerations;
  typedef std::map<std::string, OriginTimeGenerations> ProducerGenerations;
  ProducerGenerations producerGenerations;

  int parseIntValue(const std::string &paramName, const std::string &fieldName,
                    const std::string &fieldValue, bool negativeValueValid, int maxValue);
  std::pair<int, int> parseIntRange(const std::string &paramName, const std::string &fieldName,
                                    const std::string &fieldValue, size_t delimPos,
                                    bool negativeValueValid, int maxValue);
  std::list<std::pair<int, int>> parseIntValues(const std::string &paramName,
                                                const std::string &fieldName,
                                                const std::string &valueStr,
                                                bool negativeValueValid, int maxValue);
  void parseParameterLevelAndForecastNumberRanges(
      const std::string &paramDef,
      bool gribOutput,
      std::string &param,
      std::string &funcParamDef,
      std::vector<std::string> &paramParts,
      std::list<std::pair<int, int>> &levelRanges,
      std::list<std::pair<int, int>> &forecastNumberRanges);
  bool loadOriginTimeGenerations(Engine::Grid::ContentServer_sptr cS,
                                 const std::vector<std::string> &params,
                                 std::string &originTime);
  bool getOriginTimeGeneration(Engine::Grid::ContentServer_sptr cS,
                               const std::string &producer,
                               const std::string &originTime,
                               uint &generationId);
  void expandParameterFromRangeValues(const Engine::Grid::Engine *gridEngine,
                                      Fmi::DateTime originTime,
                                      bool gribOutput,
                                      bool blockQuery,
                                      const std::string &paramDef,
                                      TimeSeries::OptionParsers::ParameterOptions &pOptions);
  void parseParameters(const Spine::HTTP::Request &theReq,
                       const Engine::Grid::Engine *gridEngine,
                       std::string &originTime);
  void parseTimeOptions(const Spine::HTTP::Request &theReq);
  void parseModel(const Spine::HTTP::Request &theReq);
  void parseLevels(const Spine::HTTP::Request &theReq);

  uint expectedContentRecordCount;
};

}  // namespace Download
}  // namespace Plugin
}  // namespace SmartMet

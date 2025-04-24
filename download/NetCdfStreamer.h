// ======================================================================
/*!
 * \brief SmartMet download service plugin; netcdf streaming
 */
// ======================================================================

#pragma once

#include "DataStreamer.h"
#include <memory>
#include <typeindex>
#include <ncDim.h>
#include <ncFile.h>
#include <ncVar.h>

namespace SmartMet
{
namespace Plugin
{
namespace Download
{
class NetCdfStreamer : public DataStreamer
{
 public:
  NetCdfStreamer(const Spine::HTTP::Request &req,
                 const Config &config,
                 const Query &query,
                 const Producer &producer,
                 const ReqParams &reqParams);
  virtual ~NetCdfStreamer();

  virtual std::string getChunk();

  virtual void getDataChunk(Engine::Querydata::Q q,
                            const NFmiArea *area,
                            NFmiGrid *grid,
                            int level,
                            const NFmiMetTime &mt,
                            NFmiDataMatrix<float> &values,
                            std::string &chunk);

  // Grid support
  //

  virtual void getGridDataChunk(const QueryServer::Query &gridQuery,
                                int,
                                const NFmiMetTime &,
                                std::string &chunk);

 private:
  NetCdfStreamer();
  void requireNcFile();

  std::string itsFilename;
  std::unique_ptr<netCDF::NcFile> itsFile;
  std::ifstream itsStream;
  bool itsLoadedFlag;

  // Note: netcdf file object owns dimensions and variables (could use plain pointers instead of
  // shared_ptr:s)

  netCDF::NcDim itsEnsembleDim, itsTimeDim, itsTimeBoundsDim, itsLevelDim, itsYDim,
      itsXDim, itsLatDim, itsLonDim;
  netCDF::NcVar itsTimeVar;

  std::list<netCDF::NcVar>::iterator itsVarIterator;
  std::list<netCDF::NcVar> itsDataVars;

  typedef std::map<std::string, std::set<int>> DimensionLevels;
  DimensionLevels itsDimensionLevels;
  typedef std::map<std::string, std::string> LevelDimensions;
  LevelDimensions itsLevelDimensions;

  netCDF::NcDim addDimension(const std::string &dimName, long dimSize);
  netCDF::NcVar addVariable(const std::string &varName,
                            netCDF::NcType dataType,
                            const netCDF::NcDim& dim1 = netCDF::NcDim(),
                            const netCDF::NcDim& dim2 = netCDF::NcDim(),
                            const netCDF::NcDim& dim3 = netCDF::NcDim(),
                            const netCDF::NcDim& dim4 = netCDF::NcDim(),
                            const netCDF::NcDim& dim5 = netCDF::NcDim());
  netCDF::NcVar addCoordVariable(const std::string &dimName,
                                 long dimSize,
                                 netCDF::NcType dataType,
                                 std::string stdName,
                                 std::string unit,
                                 std::string axisType,
                                 netCDF::NcDim &dim);
  template <typename T1, typename T2>
  void addAttribute(T1 resource, std::string attrName, T2 attrValue);
  template <typename T1, typename T2>
  void addAttribute(T1 resource, std::string attrName, int nValues, T2 *attrValues);

  std::string getEnsembleDimensionName(
      T::ForecastType forecastType, T::ForecastNumber forecastNumber) const;
  netCDF::NcDim getEnsembleDimension(
      T::ForecastType forecastType, T::ForecastNumber forecastNumber,
      std::string &ensembleDimensionName) const;
  netCDF::NcDim getEnsembleDimension(
      T::ForecastType forecastType, T::ForecastNumber forecastNumber) const;
  void addEnsembleDimensions();
  void addEnsembleDimension();
  void addTimeDimension();
  netCDF::NcDim addTimeDimension(long periodLengthInMinutes,
                                 netCDF::NcVar &tVar);
  void getLevelTypeAttributes(FmiLevelType levelType,
                              std::string &name,
                              std::string &positive,
                              std::string &unit) const;
  netCDF::NcDim getLevelDimension(
      const std::string &paramName, std::string &levelDimName) const;
  netCDF::NcDim getLevelDimAndIndex(
      const std::string &paramName, int paramLevel, int &levelIndex) const;
  void addLevelDimensions();
  void addLevelDimension();

  void setSpheroidAndWKT(const netCDF::NcVar &crsVar,
                         OGRSpatialReference *geometrySRS,
                         const std::string &areaWKT = "");

  void setLatLonGeometry(const netCDF::NcVar &crsVar);
  void setRotatedLatlonGeometry(const netCDF::NcVar &crsVar);
  void setStereographicGeometry(const netCDF::NcVar &crsVar,
                                const NFmiArea *area = nullptr);
  void setMercatorGeometry(const netCDF::NcVar &crsVar);
  void setYKJGeometry(const netCDF::NcVar &crsVar);
  void setLambertConformalGeometry(const netCDF::NcVar &crsVar,
                                   const NFmiArea *area = nullptr);
  void setGeometry(Engine::Querydata::Q q, const NFmiArea *area, const NFmiGrid *grid);

  netCDF::NcDim addTimeBounds(long periodLengthInMinutes, std::string &timeDimName);

  bool hasParamVariable(const std::vector<std::string> &paramParts,
                        const std::map<std::string, netCDF::NcVar> &paramVariables);
  void addParamVariable(const netCDF::NcVar& var,
                        const std::vector<std::string> &paramParts,
                        std::map<std::string, netCDF::NcVar> &paramVariables);
  void addVariables(bool relative_uv);

  void storeParamValues();

  void paramChanged(size_t nextParamOffset = 1);

  // Grid support
  //

  void setGridGeometry(const QueryServer::Query &gridQuery);

  static std::map<std::type_index, netCDF::NcType> itsTypeMap;

  // Mappping from C++ types to netCDF types
  template <typename T>
  netCDF::NcType getNcType()
  {
    auto it = NetCdfStreamer::itsTypeMap.find(typeid(T));
    if (it != NetCdfStreamer::itsTypeMap.end())
      return it->second;

    throw Fmi::Exception(BCP, "Unsupported type");
  }

};

}  // namespace Download
}  // namespace Plugin
}  // namespace SmartMet

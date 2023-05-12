// ======================================================================
/*!
 * \brief SmartMet download service plugin; netcdf streaming
 */
// ======================================================================

#pragma once

#include "DataStreamer.h"
#include <memory>
#include <netcdfcpp.h>

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

  NcError itsError;
  std::string itsFilename;
  std::unique_ptr<NcFile> itsFile;
  std::ifstream itsStream;
  bool itsLoadedFlag;

  // Note: netcdf file object owns dimensions and variables (could use plain pointers instead of
  // shared_ptr:s)

  boost::shared_ptr<NcDim> itsEnsembleDim, itsTimeDim, itsTimeBoundsDim, itsLevelDim, itsYDim,
      itsXDim, itsLatDim, itsLonDim;
  boost::shared_ptr<NcVar> itsTimeVar;

  std::list<NcVar *>::iterator itsVarIterator;
  std::list<NcVar *> itsDataVars;

  typedef std::map<std::string, std::set<int>> DimensionLevels;
  DimensionLevels itsDimensionLevels;
  typedef std::map<std::string, std::string> LevelDimensions;
  LevelDimensions itsLevelDimensions;

  boost::shared_ptr<NcDim> addDimension(const std::string &dimName, long dimSize);
  boost::shared_ptr<NcVar> addVariable(const std::string &varName,
                                       NcType dataType,
                                       NcDim *dim1 = nullptr,
                                       NcDim *dim2 = nullptr,
                                       NcDim *dim3 = nullptr,
                                       NcDim *dim4 = nullptr,
                                       NcDim *dim5 = nullptr);
  boost::shared_ptr<NcVar> addCoordVariable(const std::string &dimName,
                                            long dimSize,
                                            NcType dataType,
                                            std::string stdName,
                                            std::string unit,
                                            std::string axisType,
                                            boost::shared_ptr<NcDim> &dim);
  template <typename T1, typename T2>
  void addAttribute(T1 resource, std::string attrName, T2 attrValue);
  template <typename T1, typename T2>
  void addAttribute(T1 resource, std::string attrName, int nValues, T2 *attrValues);

  std::string getEnsembleDimensionName(
      T::ForecastType forecastType, T::ForecastNumber forecastNumber) const;
  boost::shared_ptr<NcDim> getEnsembleDimension(
      T::ForecastType forecastType, T::ForecastNumber forecastNumber,
      std::string &ensembleDimensionName) const;
  boost::shared_ptr<NcDim> getEnsembleDimension(
      T::ForecastType forecastType, T::ForecastNumber forecastNumber) const;
  void addEnsembleDimensions();
  void addEnsembleDimension();
  void addTimeDimension();
  boost::shared_ptr<NcDim> addTimeDimension(long periodLengthInMinutes,
                                            boost::shared_ptr<NcVar> &tVar);
  void getLevelTypeAttributes(FmiLevelType levelType,
                              std::string &name,
                              std::string &positive,
                              std::string &unit) const;
  boost::shared_ptr<NcDim> getLevelDimension(
      const std::string &paramName, std::string &levelDimName) const;
  boost::shared_ptr<NcDim> getLevelDimAndIndex(
      const std::string &paramName, int paramLevel, int &levelIndex) const;
  void addLevelDimensions();
  void addLevelDimension();

  void setSpheroidAndWKT(const boost::shared_ptr<NcVar> &crsVar,
                         OGRSpatialReference *geometrySRS,
                         const std::string &areaWKT = "");

  void setLatLonGeometry(const boost::shared_ptr<NcVar> &crsVar);
  void setRotatedLatlonGeometry(const boost::shared_ptr<NcVar> &crsVar);
  void setStereographicGeometry(const boost::shared_ptr<NcVar> &crsVar,
                                const NFmiArea *area = nullptr);
  void setMercatorGeometry(const boost::shared_ptr<NcVar> &crsVar);
  void setYKJGeometry(const boost::shared_ptr<NcVar> &crsVar);
  void setLambertConformalGeometry(const boost::shared_ptr<NcVar> &crsVar,
                                   const NFmiArea *area = nullptr);
  void setGeometry(Engine::Querydata::Q q, const NFmiArea *area, const NFmiGrid *grid);

  boost::shared_ptr<NcDim> addTimeBounds(long periodLengthInMinutes, std::string &timeDimName);

  bool hasParamVariable(const std::vector<std::string> &paramParts,
                        std::map<std::string, NcVar *> &paramVariables);
  void addParamVariable(NcVar *var,
                        const std::vector<std::string> &paramParts,
                        std::map<std::string, NcVar *> &paramVariables);
  void addVariables(bool relative_uv);

  void storeParamValues();

  void paramChanged(size_t nextParamOffset = 1);

  // Grid support
  //

  void setGridGeometry(const QueryServer::Query &gridQuery);
};

}  // namespace Download
}  // namespace Plugin
}  // namespace SmartMet

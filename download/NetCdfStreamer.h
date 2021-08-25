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

  boost::shared_ptr<NcDim> itsEnsembleDim;
  boost::shared_ptr<NcDim> itsTimeDim;
  boost::shared_ptr<NcDim> itsTimeBoundsDim;
  boost::shared_ptr<NcDim> itsLevelDim;
  boost::shared_ptr<NcDim> itsYDim;
  boost::shared_ptr<NcDim> itsXDim;
  boost::shared_ptr<NcDim> itsLatDim;
  boost::shared_ptr<NcDim> itsLonDim;
  boost::shared_ptr<NcVar> itsTimeVar;

  std::list<NcVar *>::iterator itsVarIterator;
  std::list<NcVar *> itsDataVars;

  boost::shared_ptr<NcDim> addDimension(std::string dimName, long dimSize);
  boost::shared_ptr<NcVar> addVariable(std::string varName,
                                       NcType dataType,
                                       NcDim *dim1 = nullptr,
                                       NcDim *dim2 = nullptr,
                                       NcDim *dim3 = nullptr,
                                       NcDim *dim4 = nullptr,
                                       NcDim *dim5 = nullptr);
  boost::shared_ptr<NcVar> addCoordVariable(std::string dimName,
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

  void addEnsembleDimension();
  void addTimeDimension();
  boost::shared_ptr<NcDim> addTimeDimension(long periodLengthInMinutes,
                                            boost::shared_ptr<NcVar> &tVar);
  void addLevelDimension();

  void setLatLonGeometry(const NFmiArea *area, const boost::shared_ptr<NcVar> &crsVar);
  void setRotatedLatlonGeometry(const boost::shared_ptr<NcVar> &crsVar);
  void setStereographicGeometry(const NFmiArea *area, const boost::shared_ptr<NcVar> &crsVar);
  void setMercatorGeometry(const boost::shared_ptr<NcVar> &crsVar);
  void setLambertConformalGeometry(const boost::shared_ptr<NcVar> &crsVar);
  void setGeometry(Engine::Querydata::Q q, const NFmiArea *area, const NFmiGrid *grid);

  boost::shared_ptr<NcDim> addTimeBounds(long periodLengthInMinutes, std::string &timeDimName);
  void addParameters(bool relative_uv);

  void storeParamValues();

  void paramChanged(size_t nextParamOffset = 1);

  // Grid support
  //

  void setGridGeometry(const QueryServer::Query &gridQuery);
};

}  // namespace Download
}  // namespace Plugin
}  // namespace SmartMet

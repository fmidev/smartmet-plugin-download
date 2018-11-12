// ======================================================================
/*!
 * \brief SmartMet download service plugin; netcdf streaming
 */
// ======================================================================

#pragma once

#include "DataStreamer.h"
#include <netcdf>

namespace SmartMet
{
namespace Plugin
{
namespace Download
{
class NetCdfStreamer : public DataStreamer
{
 public:
  NetCdfStreamer(const Spine::HTTP::Request &req, const Config &config, const Producer &producer);
  virtual ~NetCdfStreamer();

  virtual std::string getChunk();

  virtual void getDataChunk(Engine::Querydata::Q q,
                            const NFmiArea *area,
                            NFmiGrid *grid,
                            int level,
                            const NFmiMetTime &mt,
                            NFmiDataMatrix<float> &values,
                            std::string &chunk);

 private:
  NetCdfStreamer();

  std::string file;
  netCDF::NcFile ncFile;
  std::ifstream ioStream;
  bool isLoaded = false;

  netCDF::NcDim timeDim, timeBoundsDim, levelDim, yDim, xDim, latDim, lonDim;
  netCDF::NcVar timeVar;

  std::list<netCDF::NcVar *>::iterator it_Var;
  std::list<netCDF::NcVar *> dataVars;

  netCDF::NcVar addCoordVariable(std::string dimName,
                                 long dimSize,
                                 netCDF::NcType dataType,
                                 std::string stdName,
                                 std::string unit,
                                 std::string axisType,
                                 netCDF::NcDim &dim);

  void addTimeDimension();
  netCDF::NcDim addTimeDimension(long periodLengthInMinutes, netCDF::NcVar &tVar);
  void addLevelDimension();

  void setLatLonGeometry(const NFmiArea *area, const netCDF::NcVar &crsVar);
  void setStereographicGeometry(const NFmiArea *area, const netCDF::NcVar &crsVar);
  void setGeometry(Engine::Querydata::Q q, const NFmiArea *area, const NFmiGrid *grid);

  netCDF::NcDim addTimeBounds(long periodLengthInMinutes, std::string &timeDimName);
  void addParameters(bool relative_uv);

  void storeParamValues();

  void paramChanged();
};

}  // namespace Download
}  // namespace Plugin
}  // namespace SmartMet

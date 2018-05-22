// ======================================================================
/*!
 * \brief SmartMet download service plugin; grib streaming
 */
// ======================================================================

#pragma once

#include "DataStreamer.h"
#include "GribTools.h"

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/thread.hpp>

namespace SmartMet
{
namespace Plugin
{
namespace Download
{
class GribStreamer : public DataStreamer
{
 public:
  GribStreamer(const Spine::HTTP::Request& req,
               const Config& config,
               const Producer& producer,
               OutputFormat outputFormat,
               unsigned int grib2TablesVersion = 0);
  virtual ~GribStreamer();

  virtual std::string getChunk();

  virtual void getDataChunk(Engine::Querydata::Q q,
                            const NFmiArea* area,
                            NFmiGrid* grid,
                            int level,
                            const NFmiMetTime& mt,
                            NFmiDataMatrix<float>& values,
                            std::string& chunk);

 private:
  GribStreamer();

  grib_handle* gribHandle;
  std::vector<double> valueArray;
  boost::posix_time::ptime gribOriginTime;
  bool grib1;

  void scanningDirections(long& iNegative, long& jPositive) const;

  void setLatlonGeometryToGrib() const;
  void setRotatedLatlonGeometryToGrib(const NFmiArea* Area) const;
  void setStereographicGeometryToGrib(const NFmiArea* Area) const;
  void setMercatorGeometryToGrib() const;
  void setNamedSettingsToGrib() const;
  void setGeometryToGrib(const NFmiArea* area, bool relative_uv);
  void setLevelAndParameterToGrib(int level,
                                  const NFmiParam& theParam,
                                  const ParamChangeTable& pTable,
                                  size_t& paramIdx);
  void setStepToGrib(const ParamChangeTable& pTable,
                     size_t paramIdx,
                     bool setOriginTime,
                     const boost::posix_time::ptime& validTime);
  void addValuesToGrib(Engine::Querydata::Q q,
                       const NFmiMetTime& vTime,
                       int level,
                       const NFmiDataMatrix<float>& dataValues,
                       float scale,
                       float offset);
  std::string getGribMessage(Engine::Querydata::Q q,
                             int level,
                             const NFmiMetTime& mt,
                             const NFmiDataMatrix<float>& values,
                             float scale,
                             float offset);
};

}  // namespace Download
}  // namespace Plugin
}  // namespace SmartMet

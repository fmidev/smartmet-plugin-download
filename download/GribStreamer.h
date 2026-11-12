// ======================================================================
/*!
 * \brief SmartMet download service plugin; grib streaming
 */
// ======================================================================

#pragma once

#include "Config.h"
#include "DataStreamer.h"
#include "ParamConfig.h"
#include <engines/querydata/Q.h>
#include <macgyver/DateTime.h>
#include <newbase/NFmiLevelType.h>
#include <newbase/NFmiMetTime.h>
#include <cstddef>
#include <grib_api.h>
#include <map>
#include <string>
#include <vector>

class NFmiArea;
class NFmiLevel;
class NFmiParam;
namespace SmartMet::Plugin::Download
{
class Query;
struct Producer;
struct ReqParams;
}  // namespace SmartMet::Plugin::Download
namespace SmartMet::QueryServer
{
class Query;
}  // namespace SmartMet::QueryServer
namespace SmartMet::Spine::HTTP
{
class Request;
}  // namespace SmartMet::Spine::HTTP
template <class T>
class NFmiDataMatrix;

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
               const Query& Query,
               const Producer& producer,
               const ReqParams& reqParams);
  ~GribStreamer() override;
  GribStreamer(const GribStreamer&) = delete;
  GribStreamer& operator=(const GribStreamer&) = delete;

  std::string getChunk() override;

  void getDataChunk(Engine::Querydata::Q q,
                    const NFmiArea* area,
                    NFmiGrid* grid,
                    int level,
                    const NFmiMetTime& mt,
                    NFmiDataMatrix<float>& values,
                    std::string& chunk) override;

  // Grid support
  //

  void getGridDataChunk(const QueryServer::Query& gridQuery,
                        int level,
                        const NFmiMetTime& mt,
                        std::string& chunk) override;

 private:
  GribStreamer() = delete;

  grib_handle* itsGribHandle = nullptr;
  std::vector<double> itsValueArray;
  Fmi::DateTime itsGribOriginTime;
  bool itsGrib1Flag;

  void scanningDirections(long& iNegative, long& jPositive) const;

  void setShapeOfTheEarth(const NFmiArea *area = nullptr);

  void setLatlonGeometryToGrib() const;
  void setRotatedLatlonGeometryToGrib(const NFmiArea *area = nullptr);
  void setStereographicGeometryToGrib(const NFmiArea *area = nullptr) const;
  void setMercatorGeometryToGrib() const;
  void setLambertConformalGeometryToGrib(const NFmiArea *area = nullptr) const;
  void setLambertAzimuthalEqualAreaGeometryToGrib() const;
  void setNamedSettingsToGrib() const;
  void setGeometryToGrib(const NFmiArea* area, bool relative_uv);
  std::string gribOtherLevelType(bool gridContent, FmiLevelType levelType, int &level) const;
  std::string gribLevelTypeAndLevel(bool gridContent, FmiLevelType levelType, NFmiLevel *cfgLevel,
                                    int &level) const;
  void setLevelAndParameterToGrib(int level,
                                  const NFmiParam& theParam,
                                  const std::string& paramName,
                                  const ParamChangeTable& pTable,
                                  std::size_t& paramIdx);
  void cacheParamConfigIndex(const std::string& radonParam,
                             const std::string& radonProducer,
                             std::size_t paramIdx);
  void setStepToGrib(const ParamChangeTable& pTable,
                     std::size_t paramIdx,
                     bool setOriginTime,
                     const Fmi::DateTime& validTime);
  void addValuesToGrib(const Engine::Querydata::Q& q,
                       const NFmiMetTime& vTime,
                       int level,
                       const NFmiDataMatrix<float>& dataValues,
                       float scale,
                       float offset);
  std::string getGribMessage(const Engine::Querydata::Q& q,
                             int level,
                             const NFmiMetTime& mt,
                             const NFmiDataMatrix<float>& values,
                             float scale,
                             float offset);

  // Grid support
  //

  std::string itsPreviousParam;
  using ParamConfigProducerIndexes = std::map<std::string, int>;
  using ParamConfigParamIndexes = std::map<std::string, ParamConfigProducerIndexes>;
  ParamConfigParamIndexes paramConfigIndexes;

  void setGridGeometryToGrib(const QueryServer::Query& gridQuery);
  void addGridValuesToGrib(const QueryServer::Query& gridQuery,
                           const NFmiMetTime& vTime,
                           int level,
                           float scale,
                           float offset);
  std::string getGridGribMessage(const QueryServer::Query& gridQuery,
                                 int level,
                                 const NFmiMetTime& mt,
                                 float scale,
                                 float offset);
};

}  // namespace Download
}  // namespace Plugin
}  // namespace SmartMet

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
               const Query& Query,
               const Producer& producer,
               const ReqParams& reqParams);
  virtual ~GribStreamer();

  virtual std::string getChunk();

  virtual void getDataChunk(Engine::Querydata::Q q,
                            const NFmiArea* area,
                            NFmiGrid* grid,
                            int level,
                            const NFmiMetTime& mt,
                            NFmiDataMatrix<float>& values,
                            std::string& chunk);

  // Grid support
  //

  virtual void getGridDataChunk(const QueryServer::Query& gridQuery,
                                int level,
                                const NFmiMetTime& mt,
                                uint gridIndex,
                                std::string& chunk);

 private:
  GribStreamer();

  grib_handle* itsGribHandle;
  std::vector<double> itsValueArray;
  boost::posix_time::ptime itsGribOriginTime;
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
  std::string gribLevelTypeAndLevel(bool gridContent, FmiLevelType levelType, NFmiLevel *cfgLevel,
                                    int &level) const;
  void setLevelAndParameterToGrib(int level,
                                  const NFmiParam& theParam,
                                  const std::string& paramName,
                                  const ParamChangeTable& pTable,
                                  std::size_t& paramIdx);
  void setStepToGrib(const ParamChangeTable& pTable,
                     std::size_t paramIdx,
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

  // Grid support
  //

  std::string itsPreviousParam;
  typedef std::map<std::string, int> ParamConfigProducerIndexes;
  typedef std::map<std::string, ParamConfigProducerIndexes> ParamConfigParamIndexes;
  ParamConfigParamIndexes paramConfigIndexes;

  void setGridOrigo(const QueryServer::Query& gridQuery);
  void setGridGeometryToGrib(const QueryServer::Query& gridQuery);
  void addGridValuesToGrib(const QueryServer::Query& gridQuery,
                           const NFmiMetTime& vTime,
                           int level,
                           uint gridIndex,
                           float scale,
                           float offset);
  std::string getGridGribMessage(const QueryServer::Query& gridQuery,
                                 int level,
                                 const NFmiMetTime& mt,
                                 uint gridIndex,
                                 float scale,
                                 float offset);
};

}  // namespace Download
}  // namespace Plugin
}  // namespace SmartMet

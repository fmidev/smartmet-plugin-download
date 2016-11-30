// ======================================================================
/*!
 * \brief Configuration file API
 */
// ======================================================================

#pragma once

#include "Query.h"
#include "ParamConfig.h"

#include <spine/Reactor.h>

#include <libconfig.h++>

#include <boost/utility.hpp>

#include <string>

namespace SmartMet
{
namespace Engine
{
namespace Querydata
{
class Engine;
}
}

namespace Plugin
{
namespace Download
{
class Config : private boost::noncopyable
{
 public:
  Config(const std::string& configfile);

  void init(SmartMet::Engine::Querydata::Engine* querydata);

  const Producer& getProducer(std::string& name,
                              const SmartMet::Engine::Querydata::Engine& querydata);
  const Producer& getProducer(const std::string& name) const;

  const std::string& defaultProducerName() const { return itsDefaultProducer->first; }
  const Producer& defaultProducer() const { return itsDefaultProducer->second; }
  const ParamChangeTable& getParamChangeTable(bool grib = true) const
  {
    return (grib ? itsGribPTable : itsNetCdfPTable);
  }

  const std::string& getTempDirectory() const { return itsTempDirectory; }
  const libconfig::Config& config() const { return itsConfig; }
  std::pair<unsigned int, unsigned int> getGrib2TablesVersionRange() const
  {
    return std::make_pair(itsGrib2TablesVersionMin, itsGrib2TablesVersionMax);
  }

 private:
  libconfig::Config itsConfig;

  Producers itsProducers;
  Producers::const_iterator itsDefaultProducer;

  std::string itsGribConfig;
  std::string itsNetCdfConfig;
  ParamChangeTable itsGribPTable;
  ParamChangeTable itsNetCdfPTable;
  std::string itsTempDirectory;
  unsigned int itsGrib2TablesVersionMin;
  unsigned int itsGrib2TablesVersionMax;

  void parseConfigProducers(const SmartMet::Engine::Querydata::Engine& querydata);
  void parseConfigProducer(const std::string& name, Producer& currentSettings);

  void setEnvSettings();

};  // class Config

}  // namespace Download
}  // namespace Plugin
}  // namespace SmartMet

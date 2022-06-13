// ======================================================================
/*!
 * \brief Configuration file API
 */
// ======================================================================

#pragma once

#include "ParamConfig.h"
#include "Query.h"

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
}  // namespace Engine

namespace Plugin
{
namespace Download
{
class Config : private boost::noncopyable
{
 public:
  Config(const std::string& configfile);

  void init(Engine::Querydata::Engine* querydata);

  const Producer& getProducer(std::string& name, const Engine::Querydata::Engine& querydata);
  const Producer& getProducer(const std::string& name) const;

  const std::string& defaultSource() const { return itsDefaultSource; }

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

  std::string packingErrorMessage(const std::string& thePackingType) const;

  unsigned long getMaxRequestDataValues() const { return itsMaxRequestDataValues; }
  unsigned long getLogRequestDataValues() const { return itsLogRequestDataValues; }

  bool getLegacyMode() const { return itsLegacyMode; }

 private:
  libconfig::Config itsConfig;

  std::string itsDefaultSource;

  Producers itsProducers;
  Producers::const_iterator itsDefaultProducer;

  std::string itsGribConfig;
  std::string itsNetCdfConfig;
  ParamChangeTable itsGribPTable;
  ParamChangeTable itsNetCdfPTable;
  std::string itsTempDirectory;
  unsigned int itsGrib2TablesVersionMin;
  unsigned int itsGrib2TablesVersionMax;

  unsigned long itsMaxRequestDataValues = 1024 * 1024 * 1024;
  unsigned long itsLogRequestDataValues = 0; // if 0, no logging

  void parseConfigProducers(const Engine::Querydata::Engine& querydata);
  void parseConfigProducer(const std::string& name, Producer& currentSettings);

  void setEnvSettings();

  // GRIB packing settings
  std::set<std::string> itsEnabledPackingTypes;
  std::set<std::string> itsDisabledPackingTypes;
  std::string itsPackingWarningMessage;
  std::string itsPackingErrorMessage;

  bool itsLegacyMode;

};  // class Config

}  // namespace Download
}  // namespace Plugin
}  // namespace SmartMet

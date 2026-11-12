// ======================================================================
/*!
 * \brief SmartMet download service plugin interface
 *
 *        Top-level plugin that routes requests to the appropriate handler:
 *        - /download   → DownloadHandler  (legacy SmartMet query interface)
 *        - /coverages  → CoveragesHandler (OGC API Coverages)
 */
// ======================================================================

#pragma once

#include "Config.h"
#include "coverages/Handler.h"
#include "download/Handler.h"
#include <spine/SmartMetPlugin.h>
#include <memory>
#include <string>

namespace SmartMet::Engine::Geonames
{
class Engine;
}  // namespace SmartMet::Engine::Geonames
namespace SmartMet::Engine::Grid
{
class Engine;
}  // namespace SmartMet::Engine::Grid
namespace SmartMet::Engine::Querydata
{
class Engine;
}  // namespace SmartMet::Engine::Querydata
namespace SmartMet::Spine::HTTP
{
class Request;
class Response;
}  // namespace SmartMet::Spine::HTTP
namespace SmartMet::Spine
{
class Reactor;
}  // namespace SmartMet::Spine

namespace SmartMet
{
namespace Plugin
{
namespace Download
{
class Plugin : public SmartMetPlugin
{
 public:
  Plugin(Spine::Reactor* theReactor, const char* theConfig);
  Plugin(const Plugin& other) = delete;
  Plugin& operator=(const Plugin& other) = delete;
  ~Plugin() override;

  const std::string& getPluginName() const override;
  int getRequiredAPIVersion() const override;
  bool queryIsFast(const Spine::HTTP::Request& theRequest) const override;

 protected:
  void init() override;
  void shutdown() override;
  void requestHandler(Spine::Reactor& theReactor,
                      const Spine::HTTP::Request& theRequest,
                      Spine::HTTP::Response& theResponse) override;

 private:
  Plugin() = delete;

  void coveragesRequestHandler(const Spine::Reactor& theReactor,
                               const Spine::HTTP::Request& theRequest,
                               Spine::HTTP::Response& theResponse);

  const std::string itsModuleName;
  Config itsConfig;

  Spine::Reactor* itsReactor;
  std::shared_ptr<Engine::Querydata::Engine> itsQEngine;
  std::shared_ptr<Engine::Grid::Engine> itsGridEngine;
  std::shared_ptr<Engine::Geonames::Engine> itsGeoEngine;

  DownloadHandler itsDownloadHandler;
  CoveragesHandler itsCoveragesHandler;
};

}  // namespace Download
}  // namespace Plugin
}  // namespace SmartMet

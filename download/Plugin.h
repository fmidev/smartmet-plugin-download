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
#include "download/Handler.h"
#include "coverages/Handler.h"
#include <memory>
#include <engines/geonames/Engine.h>
#include <engines/grid/Engine.h>
#include <spine/HTTP.h>
#include <spine/Reactor.h>
#include <spine/SmartMetPlugin.h>
#include <string>

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
  virtual ~Plugin();

  const std::string& getPluginName() const;
  int getRequiredAPIVersion() const;
  bool queryIsFast(const Spine::HTTP::Request& theRequest) const;

 protected:
  void init();
  void shutdown();
  void requestHandler(Spine::Reactor& theReactor,
                      const Spine::HTTP::Request& theRequest,
                      Spine::HTTP::Response& theResponse);

 private:
  Plugin();

  void coveragesRequestHandler(Spine::Reactor& theReactor,
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

// ======================================================================
/*!
 * \brief Download API request handler
 *
 *        Handles /download requests using the legacy SmartMet query
 *        string interface (format, producer, param, bbox, etc.).
 */
// ======================================================================

#pragma once

#include "Config.h"
#include "DataStreamer.h"
#include <engines/geonames/Engine.h>
#include <engines/grid/Engine.h>
#include <engines/querydata/Engine.h>
#include <spine/HTTP.h>
#include <spine/Reactor.h>

namespace SmartMet
{
namespace Plugin
{
namespace Download
{
class DownloadHandler
{
 public:
  DownloadHandler() = default;
  DownloadHandler(const DownloadHandler &other) = delete;
  DownloadHandler &operator=(const DownloadHandler &other) = delete;

  void init(Config &config,
            Engine::Querydata::Engine *qEngine,
            Engine::Grid::Engine *gridEngine,
            Engine::Geonames::Engine *geoEngine);

  void requestHandler(Spine::Reactor &theReactor,
                      const Spine::HTTP::Request &theRequest,
                      Spine::HTTP::Response &theResponse);

 private:
  Config *itsConfig = nullptr;
  Engine::Querydata::Engine *itsQEngine = nullptr;
  Engine::Grid::Engine *itsGridEngine = nullptr;
  Engine::Geonames::Engine *itsGeoEngine = nullptr;
};

}  // namespace Download
}  // namespace Plugin
}  // namespace SmartMet

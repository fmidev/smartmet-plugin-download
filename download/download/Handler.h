// ======================================================================
/*!
 * \brief Download API request handler
 *
 *        Handles /download requests using the legacy SmartMet query
 *        string interface (format, producer, param, bbox, etc.).
 */
// ======================================================================

#pragma once

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
namespace SmartMet::Plugin::Download
{
class Config;
}  // namespace SmartMet::Plugin::Download
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
class DownloadHandler
{
 public:
  DownloadHandler() = default;
  DownloadHandler(const DownloadHandler &other) = delete;
  DownloadHandler &operator=(const DownloadHandler &other) = delete;
  ~DownloadHandler() = default;

  void init(Config &config,
            Engine::Querydata::Engine *qEngine,
            Engine::Grid::Engine *gridEngine,
            Engine::Geonames::Engine *geoEngine);

  void requestHandler(const Spine::Reactor& theReactor,
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

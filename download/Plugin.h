// ======================================================================
/*!
 * \brief SmartMet download service plugin interface
 */
// ======================================================================

#pragma once

#include "Config.h"
#include "DataStreamer.h"

#include <engines/geonames/Engine.h>
#include <engines/grid/Engine.h>
#include <spine/HTTP.h>
#include <spine/Reactor.h>
#include <spine/SmartMetPlugin.h>

#include <boost/shared_ptr.hpp>
#include <boost/thread.hpp>
#include <boost/utility.hpp>

#include <map>
#include <string>

namespace SmartMet
{
namespace Plugin
{
namespace Download
{
class Plugin : public SmartMetPlugin, private boost::noncopyable
{
 public:
  Plugin(Spine::Reactor* theReactor, const char* theConfig);
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
  void query(const Spine::HTTP::Request& req, Spine::HTTP::Response& response);

  const std::string itsModuleName;
  Config itsConfig;

  Spine::Reactor* itsReactor;
  Engine::Querydata::Engine* itsQEngine;
  Engine::Grid::Engine* itsGridEngine;
  Engine::Geonames::Engine* itsGeoEngine;
};

}  // namespace Download
}  // namespace Plugin
}  // namespace SmartMet

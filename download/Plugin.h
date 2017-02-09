// ======================================================================
/*!
 * \brief SmartMet download service plugin interface
 */
// ======================================================================

#pragma once

#include "Config.h"
#include "DataStreamer.h"

#include <spine/SmartMetPlugin.h>
#include <spine/Reactor.h>
#include <spine/HTTP.h>
#include <engines/geonames/Engine.h>

#include <boost/shared_ptr.hpp>
#include <boost/utility.hpp>
#include <boost/thread.hpp>

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
  Plugin(SmartMet::Spine::Reactor* theReactor, const char* theConfig);
  virtual ~Plugin();

  const std::string& getPluginName() const;
  int getRequiredAPIVersion() const;
  bool queryIsFast(const SmartMet::Spine::HTTP::Request& theRequest) const;

 protected:
  void init();
  void shutdown();
  void requestHandler(SmartMet::Spine::Reactor& theReactor,
                      const SmartMet::Spine::HTTP::Request& theRequest,
                      SmartMet::Spine::HTTP::Response& theResponse);

 private:
  Plugin();
  void query(const SmartMet::Spine::HTTP::Request& req, SmartMet::Spine::HTTP::Response& response);

  const std::string itsModuleName;
  Config itsConfig;

  SmartMet::Spine::Reactor* itsReactor;
  SmartMet::Engine::Querydata::Engine* itsQEngine;
  SmartMet::Engine::Geonames::Engine* itsGeoEngine;
};

}  // namespace Download
}  // namespace Plugin
}  // namespace SmartMet

// ======================================================================
/*!
 * \brief SmartMet download service plugin — top-level router
 *
 *        Routes requests to DownloadHandler (/download) or
 *        CoveragesHandler (/coverages).  Shared streamer creation
 *        utilities are in StreamerFactory.cpp.
 */
// ======================================================================

#include "Plugin.h"
#include <boost/bind/bind.hpp>
#include <macgyver/Exception.h>
#include <spine/SmartMet.h>
#include <iostream>
#include <netcdf.h>

namespace ph = boost::placeholders;

namespace SmartMet
{
namespace Plugin
{
namespace Download
{
// ----------------------------------------------------------------------
/*!
 * \brief Main content handler for /download
 *
 *        Delegates to DownloadHandler which handles the legacy
 *        SmartMet query string interface.
 */
// ----------------------------------------------------------------------

void Plugin::requestHandler(Spine::Reactor &theReactor,
                            const Spine::HTTP::Request &theRequest,
                            Spine::HTTP::Response &theResponse)
{
  itsDownloadHandler.requestHandler(theReactor, theRequest, theResponse);
}

// ----------------------------------------------------------------------
/*!
 * \brief Content handler for /coverages
 *
 *        Delegates to CoveragesHandler which handles the OGC API
 *        Coverages interface.
 */
// ----------------------------------------------------------------------

void Plugin::coveragesRequestHandler(Spine::Reactor &theReactor,
                                     const Spine::HTTP::Request &theRequest,
                                     Spine::HTTP::Response &theResponse)
{
  itsCoveragesHandler.requestHandler(theReactor, theRequest, theResponse);
}

// ----------------------------------------------------------------------
/*!
 * \brief Plugin constructor
 */
// ----------------------------------------------------------------------

Plugin::Plugin(Spine::Reactor *theReactor, const char *theConfig)
    : SmartMetPlugin(), itsModuleName("Download"), itsConfig(theConfig), itsReactor(theReactor)
{
  try
  {
    if (theReactor->getRequiredAPIVersion() != SMARTMET_API_VERSION)
    {
      std::cerr << "*** Download Plugin and Server SmartMet API version mismatch ***" << std::endl;
      return;
    }
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Initializator — loads engines and registers both handlers
 */
// ----------------------------------------------------------------------
void Plugin::init()
{
  try
  {
    /* Dont't let the NetCDF library crash the server */
    ncopts = NC_VERBOSE;

    /* QEngine */

    itsQEngine = itsReactor->getEngine<Engine::Querydata::Engine>("Querydata", nullptr);

    /* GridEngine */

    itsGridEngine = itsReactor->getEngine<Engine::Grid::Engine>("grid", nullptr);

    /* GeoEngine */

    itsGeoEngine = itsReactor->getEngine<Engine::Geonames::Engine>("Geonames", nullptr);

    itsConfig.init(itsQEngine.get(), itsGridEngine.get());

    /* Initialize handlers */

    itsDownloadHandler.init(itsConfig, itsQEngine.get(), itsGridEngine.get(), itsGeoEngine.get());
    itsCoveragesHandler.init(itsConfig, itsQEngine.get(), itsGridEngine.get(), itsGeoEngine.get());

    /* Register content handlers for both API paths */

    if (!itsReactor->addContentHandler(
            this,
            "/download",
            boost::bind(&Plugin::callRequestHandler, this, ph::_1, ph::_2, ph::_3)))
      throw Fmi::Exception(BCP, "Failed to register download content handler");

    if (!itsReactor->addContentHandler(
            this,
            "/coverages",
            boost::bind(&Plugin::coveragesRequestHandler, this, ph::_1, ph::_2, ph::_3)))
      throw Fmi::Exception(BCP, "Failed to register coverages content handler");
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Shutdown the plugin
 */
// ----------------------------------------------------------------------

void Plugin::shutdown()
{
  std::cout << "  -- Shutdown requested (dls)\n";
}

// ----------------------------------------------------------------------
/*!
 * \brief Destructor
 */
// ----------------------------------------------------------------------

Plugin::~Plugin() {}

// ----------------------------------------------------------------------
/*!
 * \brief Return the plugin name
 */
// ----------------------------------------------------------------------

const std::string &Plugin::getPluginName() const
{
  return itsModuleName;
}

// ----------------------------------------------------------------------
/*!
 * \brief Return the required version
 */
// ----------------------------------------------------------------------

int Plugin::getRequiredAPIVersion() const
{
  return SMARTMET_API_VERSION;
}

// ----------------------------------------------------------------------
/*!
 * \brief Performance query implementation.
 */
// ----------------------------------------------------------------------

bool Plugin::queryIsFast(const Spine::HTTP::Request & /* theRequest */) const
{
  return false;
}

}  // namespace Download
}  // namespace Plugin
}  // namespace SmartMet

/*
 * Server knows us through the 'SmartMetPlugin' virtual interface, which
 * the 'Plugin' class implements.
 */

extern "C" SmartMetPlugin *create(SmartMet::Spine::Reactor *them, const char *config)
{
  return new SmartMet::Plugin::Download::Plugin(them, config);
}

extern "C" void destroy(SmartMetPlugin *us)
{
  // This will call 'Plugin::~Plugin()' since the destructor is virtual
  delete us;
}

// ======================================================================

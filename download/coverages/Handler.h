// ======================================================================
/*!
 * \brief OGC API Coverages request handler
 *
 *        Handles /coverages requests following the OGC API - Coverages
 *        standard (OGC 19-087).  Provides REST endpoints for collection
 *        metadata, schema, conformance and coverage data retrieval with
 *        subsetting, field selection, scaling and CRS reprojection.
 */
// ======================================================================

#pragma once

#include "Config.h"
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
class CoveragesHandler
{
 public:
  CoveragesHandler() = default;
  CoveragesHandler(const CoveragesHandler &other) = delete;
  CoveragesHandler &operator=(const CoveragesHandler &other) = delete;

  void init(Config &config,
            Engine::Querydata::Engine *qEngine,
            Engine::Grid::Engine *gridEngine,
            Engine::Geonames::Engine *geoEngine);

  void requestHandler(Spine::Reactor &theReactor,
                      const Spine::HTTP::Request &theRequest,
                      Spine::HTTP::Response &theResponse);

 private:
  // Metadata endpoint handlers
  void handleLandingPage(const Spine::HTTP::Request &theRequest,
                         Spine::HTTP::Response &theResponse);
  void handleConformance(const Spine::HTTP::Request &theRequest,
                         Spine::HTTP::Response &theResponse);
  void handleCollections(const Spine::HTTP::Request &theRequest,
                         Spine::HTTP::Response &theResponse);
  void handleCollection(const Spine::HTTP::Request &theRequest,
                        Spine::HTTP::Response &theResponse,
                        const std::string &collectionId);
  void handleSchema(const Spine::HTTP::Request &theRequest,
                    Spine::HTTP::Response &theResponse,
                    const std::string &collectionId);
  void handleCoverage(const Spine::HTTP::Request &theRequest,
                      Spine::HTTP::Response &theResponse,
                      const std::string &collectionId);

  Config *itsConfig = nullptr;
  Engine::Querydata::Engine *itsQEngine = nullptr;
  Engine::Grid::Engine *itsGridEngine = nullptr;
  Engine::Geonames::Engine *itsGeoEngine = nullptr;
};

}  // namespace Download
}  // namespace Plugin
}  // namespace SmartMet

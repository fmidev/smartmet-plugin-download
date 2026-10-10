// ======================================================================
/*!
 * \brief Generated OpenAPI definition of the OGC API - Coverages interface
 */
// ======================================================================

#pragma once

#include <spine/OpenApi.h>
#include <string>
#include <vector>

namespace SmartMet
{
namespace Plugin
{
namespace Download
{
class Config;

namespace Coverages
{
// Values of the f option and the media types they select
const std::vector<std::pair<std::string, std::string>>& outputFormats();

// Conformance classes of /coverages/conformance
std::vector<std::string> conformanceClasses();

// The definition of /coverages. Paths are relative to the coverages root URL.
Spine::OpenAPI::Document apiDefinition(const Config& config,
                                       const Spine::OpenAPI::Settings& settings);

}  // namespace Coverages
}  // namespace Download
}  // namespace Plugin
}  // namespace SmartMet

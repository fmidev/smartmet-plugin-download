// ======================================================================
/*!
 * \brief Shared streamer creation utilities for download and coverages handlers
 */
// ======================================================================

#pragma once

#include "Config.h"
#include "DataStreamer.h"
#include "Query.h"
#include "Tools.h"
#include <engines/geonames/Engine.h>
#include <engines/grid/Engine.h>
#include <engines/querydata/Engine.h>
#include <macgyver/DateTime.h>
#include <spine/HTTP.h>
#include <string>

namespace SmartMet
{
namespace Plugin
{
namespace Download
{
// ----------------------------------------------------------------------
/*!
 * \brief Check that configuration exists for the requested parameters
 *		and get scale and offset for grib output.
 *
 * 		Unknown (and special) parameters are ignored.
 *
 * 		Returns true if all parameters are known. The known
 * 		parameters are set to knownParams.
 */
// ----------------------------------------------------------------------

bool getParamConfig(const ParamChangeTable &pTable,
                    const Query &query,
                    DataSource dataSource,
                    OutputFormat outputFormat,
                    TimeSeries::OptionParsers::ParameterList &knownParams,
                    Scaling &scaling);

// ----------------------------------------------------------------------
/*!
 * \brief Get download file name.
 */
// ----------------------------------------------------------------------

std::string getDownloadFileName(const std::string &producer,
                                const Fmi::DateTime &originTime,
                                const Fmi::DateTime &startTime,
                                const Fmi::DateTime &endTime,
                                const std::string &projection,
                                OutputFormat outputFormat);

// ----------------------------------------------------------------------
/*!
 * \brief Create and initialize a data streamer.
 *
 *        This is the shared factory used by both the download and
 *        coverages handlers.  The caller is responsible for parsing
 *        the HTTP request into ReqParams, Query and start/end times;
 *        this function handles format selection, parameter validation,
 *        engine wiring, data availability checking and filename
 *        generation.
 */
// ----------------------------------------------------------------------

std::shared_ptr<DataStreamer> createStreamer(const Spine::HTTP::Request &req,
                                            Config &config,
                                            const Engine::Querydata::Engine &qEngine,
                                            const Engine::Grid::Engine *gridEngine,
                                            const Engine::Geonames::Engine *geoEngine,
                                            ReqParams &reqParams,
                                            const Producer &producer,
                                            Query &query,
                                            const Fmi::DateTime &startTime,
                                            const Fmi::DateTime &endTime,
                                            std::string &fileName);

}  // namespace Download
}  // namespace Plugin
}  // namespace SmartMet

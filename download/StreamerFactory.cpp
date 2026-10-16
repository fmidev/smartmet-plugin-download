// ======================================================================
/*!
 * \brief Shared streamer creation utilities
 */
// ======================================================================

#include "StreamerFactory.h"
#include "GeoTiffStreamer.h"
#include "GribStreamer.h"
#include "NetCdfStreamer.h"
#include "QueryDataStreamer.h"
#include <boost/algorithm/string/replace.hpp>
#include <boost/numeric/conversion/cast.hpp>
#include <macgyver/Exception.h>
#include <macgyver/StringConversion.h>
#include <macgyver/TimeParser.h>

using namespace std;

namespace SmartMet
{
namespace Plugin
{
namespace Download
{
// ----------------------------------------------------------------------
/*!
 * \brief Get parameter scale factor and offset
 */
// ----------------------------------------------------------------------

namespace
{
// Test whether a parameter table entry matches the radon parameter. The index of the
// first matching entry without a radon producer is stored to j as a fallback.
bool matchRadonParam(const ParamChangeTable &ptable,
                     size_t i,
                     const std::string &paramName,
                     const std::string &producerName,
                     OutputFormat outputFormat,
                     size_t &j)
{
  if (paramName != ptable[i].itsRadonName)
    return false;

  if ((outputFormat == NetCdf) || (outputFormat == GeoTiff))
    return true;

  if (!(((outputFormat == Grib1) && ptable[i].itsGrib1Param) ||
        ((outputFormat == Grib2) && ptable[i].itsGrib2Param)))
    return false;

  auto const &confProducer = ptable[i].itsRadonProducer;

  if (producerName == confProducer)
    return true;
  if ((j == ptable.size()) && confProducer.empty())
    j = i;
  return false;
}

bool getScaleFactorAndOffset(signed long id,
                                    const std::string &producerName,
                                    const std::string &paramName,
                                    OutputFormat outputFormat,
                                    float *scale,
                                    float *offset,
                                    const ParamChangeTable &ptable)
{
  try
  {
    bool radonParam = (!paramName.empty());
    size_t i = 0;
    size_t j = ptable.size();

    for (; i < ptable.size(); ++i)
    {
      if (radonParam)
      {
        if (matchRadonParam(ptable, i, paramName, producerName, outputFormat, j))
          break;

        continue;
      }

      if (id != ptable[i].itsWantedParam.GetIdent())
        continue;

      *scale = ptable[i].itsConversionScale;
      *offset = ptable[i].itsConversionBase;

      return true;
    }

    // No unit conversion for radon parameters

    *scale = 1;
    *offset = 0;

    if (i >= ptable.size())
      i = j;

    return (i < ptable.size());
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// Parse the producer and parameter names of a grid parameter and check its geometry
// is the same as that of the previous parameters
void parseGridParameter(const Query &query,
                        const Spine::Parameter &param,
                        vector<string> &paramParts,
                        string &paramName,
                        string &producerName,
                        int &geometry)
{
  // All parameters must have the same geometry

  query.parseRadonParameterName(param.name(), paramParts);
  paramName = paramParts[0];
  producerName = paramParts[1];

  auto geom = getGeometryId(paramName, paramParts);

  if (geometry >= 0)
  {
    if (geom != geometry)
      throw Fmi::Exception(BCP,
                           "All parameters must have the same geometryid " +
                               Fmi::to_string(geometry) + ": " + param.name());
  }
  else
    geometry = geom;
}
}  // namespace

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
                    Scaling &scaling)
{
  try
  {
    knownParams.clear();

    if (pTable.empty())
      return false;

    std::list<unsigned int> missingParams;
    float scale = 1.0;
    float offset = 0.0;
    unsigned int i = 0;

    vector<string> paramParts;
    string producerName;
    string paramName;
    bool gridContent = (dataSource == GridContent);
    int geometry = -1;

    auto const &params = query.pOptions.parameters();

    for (const Spine::Parameter& param : params)
    {
      // We allow special params too if they have a number (WindUMS and WindVMS)

      bool ok = (gridContent || (param.number() > 0));

      if (ok)
      {
        int id = param.number();
        auto lid = boost::numeric_cast<unsigned long>(id);

        if (gridContent)
          parseGridParameter(query, param, paramParts, paramName, producerName, geometry);

        ok = getScaleFactorAndOffset(
            lid, producerName, paramName, outputFormat, &scale, &offset, pTable);
      }

      if (!ok)
        missingParams.push_back(i);
      else
        scaling.emplace_back(scale, offset);

      i++;
    }

    auto itm = missingParams.begin();
    i = 0;

    for (const Spine::Parameter& param : params)
    {
      if ((itm != missingParams.end()) && (i == *itm))
        itm++;
      else
        knownParams.push_back(param);

      i++;
    }

    return (missingParams.empty());
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Get download file name.
 *
 */
// ----------------------------------------------------------------------

std::string getDownloadFileName(const std::string &producer,
                                const Fmi::DateTime &originTime,
                                const Fmi::DateTime &startTime,
                                const Fmi::DateTime &endTime,
                                const std::string &projection,
                                OutputFormat outputFormat)
{
  try
  {
    string sTime;
    string eTime;
    string oTime;

    if (startTime.is_not_a_date_time())
      sTime = "start";
    else
      sTime = Fmi::to_iso_string(startTime);

    if (endTime.is_not_a_date_time())
      eTime = "end";
    else
      eTime = Fmi::to_iso_string(endTime);

    if (originTime.is_not_a_date_time())
      oTime = sTime;
    else
      oTime = Fmi::to_iso_string(originTime);

    const char* extn = nullptr;

    if (outputFormat == Grib1)
      extn = ".grb";
    else if (outputFormat == Grib2)
      extn = ".grb2";
    else if (outputFormat == NetCdf)
      extn = ".nc";
    else if (outputFormat == GeoTiff)
      extn = ".tif";
    else
      extn = ".sqd";

    return producer + "_" + oTime + "_" + sTime + "_" + eTime +
           (!projection.empty() ? ("_" + projection) : "") + extn;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

namespace
{
// Create the format specific streamer and get scaling information for the requested
// parameters. Unknown (and special) parameters are ignored.
std::shared_ptr<DataStreamer> makeFormatStreamer(
    const Spine::HTTP::Request &req,
    Config &config,
    const ReqParams &reqParams,
    const Producer &producer,
    const Query &query,
    TimeSeries::OptionParsers::ParameterList &knownParams,
    Scaling &scaling)
{
  std::shared_ptr<DataStreamer> ds;

  if ((reqParams.outputFormat == Grib1) || (reqParams.outputFormat == Grib2))
  {
    ds = std::shared_ptr<DataStreamer>(new GribStreamer(req, config, query, producer, reqParams));
    getParamConfig(config.getParamChangeTable(),
                   query,
                   reqParams.dataSource,
                   reqParams.outputFormat,
                   knownParams,
                   scaling);
  }
  else if ((reqParams.outputFormat == NetCdf) || (reqParams.outputFormat == GeoTiff))
  {
    if (reqParams.outputFormat == NetCdf)
      ds = std::shared_ptr<DataStreamer>(
          new NetCdfStreamer(req, config, query, producer, reqParams));
    else
      ds = std::shared_ptr<DataStreamer>(
          new GeoTiffStreamer(req, config, query, producer, reqParams));

    getParamConfig(config.getParamChangeTable(false),
                   query,
                   reqParams.dataSource,
                   reqParams.outputFormat,
                   knownParams,
                   scaling);
  }
  else
  {
    ds = std::shared_ptr<DataStreamer>(new QDStreamer(req, config, query, producer, reqParams));

    for (const Spine::Parameter& param : query.pOptions.parameters())
    {
      knownParams.push_back(param);
    }
  }

  return ds;
}

// Get the Q object for the producer and origintime. The origintime is updated to the
// origintime of the selected data.
Engine::Querydata::Q getProducerQ(DataStreamer &ds,
                                  const Engine::Querydata::Engine &qEngine,
                                  const ReqParams &reqParams,
                                  Fmi::DateTime &originTime)
{
  Engine::Querydata::Q q;

  if (reqParams.dataSource == QueryData)
  {
    ds.setMultiFile(qEngine.getProducerConfig(reqParams.producer).ismultifile);

    if (!reqParams.originTime.empty())
    {
      if (reqParams.originTime == "latest" || reqParams.originTime == "newest")
        originTime = Fmi::DateTime(Fmi::DateTime::POS_INFINITY);
      else if (reqParams.originTime == "oldest")
        originTime = Fmi::DateTime(Fmi::DateTime::NEG_INFINITY);
      else
        originTime = Fmi::TimeParser::parse(reqParams.originTime);

      q = qEngine.get(reqParams.producer, originTime);

      originTime = q->originTime();
    }
    else
    {
      q = qEngine.get(reqParams.producer);
    }
  }
  else
  {
    if (!reqParams.originTime.empty())
      originTime = Fmi::TimeParser::parse(reqParams.originTime);

    ds.setMultiFile(false);  // TODO: always ?
  }

  return q;
}

}  // namespace

// ----------------------------------------------------------------------
/*!
 * \brief Create and initialize a data streamer.
 */
// ----------------------------------------------------------------------

std::shared_ptr<DataStreamer> createStreamer(const Spine::HTTP::Request &req,
                                            Config &config,
                                            const Engine::Querydata::Engine &qEngine,
                                            const Engine::Grid::Engine *gridEngine,
                                            const Engine::Geonames::Engine *geoEngine,
                                             const ReqParams& reqParams,
                                            const Producer &producer,
                                             const Query& query,
                                            const Fmi::DateTime &startTime,
                                            const Fmi::DateTime &endTime,
                                            std::string &fileName)
{
  try
  {
    // Create format specific streamer and get scaling information for the requested parameters.
    // Unknown (and special) parameters are ignored.

    TimeSeries::OptionParsers::ParameterList knownParams;
    Scaling scaling;
    std::shared_ptr<DataStreamer> ds =
        makeFormatStreamer(req, config, reqParams, producer, query, knownParams, scaling);

    if (knownParams.empty())
      throw Fmi::Exception(
          BCP,
          "createStreamer: No known parameters available for producer '" + reqParams.producer + "'");

    if ((reqParams.outputFormat != QD) && (scaling.size() != knownParams.size()))
      throw Fmi::Exception(BCP, "createStreamer: internal: Parameter/scaling data mismatch");

    ds->setParams(knownParams, scaling);

    // Set engines

    ds->setEngines(&qEngine, gridEngine, geoEngine);

    // Get Q object for the producer/origintime

    Fmi::DateTime originTime;
    Engine::Querydata::Q q = getProducerQ(*ds, qEngine, reqParams, originTime);

    if (reqParams.dataSource == QueryData)
    {
      // Generate list of validtimes for the data to be loaded.
      // For grid data validtimes are generated after checking data availability

      Fmi::DateTime sTime = startTime;
      Fmi::DateTime eTime = endTime;
      ds->generateValidTimeList(q, originTime, sTime, eTime);

      // Set request levels.
      // For grid data levels are set after checking data availability

      ds->setLevels();
    }

    // In order to set response status check if (any) data is available for the requested
    // levels, parameters and time range
    //
    // Set parameter and level iterators etc. to their start positions and load first available grid

    Fmi::DateTime oTime = originTime;
    Fmi::DateTime sTime = startTime;
    Fmi::DateTime eTime = endTime;

    if (!ds->hasRequestedData(producer, oTime, sTime, eTime))
    {
      if (reqParams.dataSource != GridContent)
        throw Fmi::Exception(
            BCP, "createStreamer: No data available for producer '" + reqParams.producer + "'");
      throw Fmi::Exception(BCP, "createStreamer: No data available");
    }

    // Download file name

    string projection = boost::algorithm::replace_all_copy(reqParams.projection, " ", "_");
    boost::algorithm::replace_all(projection, ",", ":");

    fileName = getDownloadFileName(
        reqParams.producer, originTime, startTime, endTime, projection, reqParams.outputFormat);

    return ds;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

}  // namespace Download
}  // namespace Plugin
}  // namespace SmartMet

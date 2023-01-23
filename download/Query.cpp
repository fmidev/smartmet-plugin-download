// ======================================================================
/*!
 * \brief Utility functions for parsing the request
 */
// ======================================================================

#include "Query.h"
#include "Config.h"
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/foreach.hpp>
#include <macgyver/Exception.h>
#include <macgyver/StringConversion.h>
#include <newbase/NFmiPoint.h>
#include <spine/Convenience.h>
#include <timeseries/OptionParsers.h>

using namespace std;

namespace SmartMet
{
namespace Plugin
{
namespace Download
{
static const char* defaultTimeZone = "utc";

// ----------------------------------------------------------------------
/*!
 * \brief The constructor parses the query string
 */
// ----------------------------------------------------------------------

Query::Query(const Spine::HTTP::Request& req)
{
  try
  {
    parseParameters(req);
    parseTimeOptions(req);
    parseLevels(req);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Parse param option
 */
// ----------------------------------------------------------------------

void Query::parseParameters(const Spine::HTTP::Request& theReq)
{
  try
  {
    string opt = Spine::optional_string(theReq.getParameter("source"), "querydata");

    if (opt != "gridcontent")
    {
      // Using newbase names

      pOptions = TimeSeries::OptionParsers::parseParameters(theReq);
      return;
    }

    // Using radon names
    //
    // Generate unique param id's, grib/netcdf param mappings are searched by radon name

    opt = Spine::required_string(theReq.getParameter("param"), "param option is required");

    vector<string> params;
    boost::algorithm::split(params, opt, boost::algorithm::is_any_of(","));

    for (const string& param : params)
      pOptions.add(Spine::Parameter(param, Spine::Parameter::Type::Data,
                                    FmiParameterName(kFmiPressure + pOptions.size())));

    if (pOptions.size() == 0)
      throw Fmi::Exception::Trace(BCP, "No parameter names given");
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Parse time options
 */
// ----------------------------------------------------------------------

void Query::parseTimeOptions(const Spine::HTTP::Request& theReq)
{
  try
  {
    tOptions = TimeSeries::parseTimes(theReq);
    timeZone = Spine::optional_string(theReq.getParameter("tz"), defaultTimeZone);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Parse level options
 *
 * Empty result implies all levels are wanted
 */
// ----------------------------------------------------------------------

void Query::parseLevels(const Spine::HTTP::Request& theReq)
{
  try
  {
    // Get the option string

    string source = Spine::optional_string(theReq.getParameter("source"), "");
    string opt = Spine::optional_string(theReq.getParameter("level"), "");

    if (!opt.empty())
    {
      if (source == "gridcontent")
        throw Fmi::Exception(BCP, "Cannot specify level option with grid content data");

      levels.insert(Fmi::stoi(opt));
    }

    // Allow also "levels"

    opt = Spine::optional_string(theReq.getParameter("levels"), "");

    if (!opt.empty())
    {
      if (source == "gridcontent")
        throw Fmi::Exception(BCP, "Cannot specify levels option with grid content data");

      vector<string> parts;
      boost::algorithm::split(parts, opt, boost::algorithm::is_any_of(","));
      BOOST_FOREACH (const string& tmp, parts)
        levels.insert(Fmi::stoi(tmp));
    }
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

}  // namespace Download
}  // namespace Plugin
}  // namespace SmartMet

// ======================================================================

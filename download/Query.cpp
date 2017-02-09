// ======================================================================
/*!
 * \brief Utility functions for parsing the request
 */
// ======================================================================

#include "Query.h"
#include "Config.h"

#include <spine/Exception.h>
#include <spine/Convenience.h>
#include <spine/OptionParsers.h>

#include <newbase/NFmiPoint.h>
#include <macgyver/String.h>

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/foreach.hpp>

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

Query::Query(const SmartMet::Spine::HTTP::Request& req,
             const Config& /* config */,
             SmartMet::Engine::Querydata::Engine* /* qEngine */)
    : pOptions(SmartMet::Spine::OptionParsers::parseParameters(req)),
      tOptions(SmartMet::Spine::OptionParsers::parseTimes(req))
{
  try
  {
    parseTimeZone(req);
    parseLevels(req);
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Parse timezone options
 */
// ----------------------------------------------------------------------

void Query::parseTimeZone(const SmartMet::Spine::HTTP::Request& theReq)
{
  try
  {
    // Get the option string
    timeZone = SmartMet::Spine::optional_string(theReq.getParameter("tz"), defaultTimeZone);
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief parse Level query
 *
 * Empty result implies all levels are wanted
 */
// ----------------------------------------------------------------------

void Query::parseLevels(const SmartMet::Spine::HTTP::Request& theReq)
{
  try
  {
    // Get the option string

    string opt = SmartMet::Spine::optional_string(theReq.getParameter("level"), "");
    if (!opt.empty())
    {
      levels.insert(Fmi::stoi(opt));
    }

    // Allow also "levels"
    opt = SmartMet::Spine::optional_string(theReq.getParameter("levels"), "");
    if (!opt.empty())
    {
      vector<string> parts;
      boost::algorithm::split(parts, opt, boost::algorithm::is_any_of(","));
      BOOST_FOREACH (const string& tmp, parts)
        levels.insert(Fmi::stoi(tmp));
    }
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

}  // namespace Download
}  // namespace Plugin
}  // namespace SmartMet

// ======================================================================

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

#include <boost/date_time/posix_time/posix_time.hpp>
using boost::posix_time::ptime;

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

Query::Query(const Spine::HTTP::Request& req, Engine::Grid::Engine *gridEngine)
{
  try
  {
    parseTimeOptions(req);
    parseParameters(req, gridEngine);
    parseLevels(req);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Parse int value
 */
// ----------------------------------------------------------------------

int Query::parseIntValue(const string &paramName, const string &fieldName, const string &fieldValue,
                         int maxValue)
{
  if (strspn(fieldValue.c_str(), "1234567890") != fieldValue.length())
    throw Fmi::Exception::Trace(BCP, paramName + ": Invalid " + fieldName + " value " + fieldValue);

  auto v = atoi(fieldValue.c_str());

  if ((maxValue > 0) && (v > maxValue))
    throw Fmi::Exception::Trace(
        BCP, paramName + ": Maximum " + fieldName + " value is " + Fmi::to_string(maxValue));

  return v;
}

// ----------------------------------------------------------------------
/*!
 * \brief Parse int value range
 */
// ----------------------------------------------------------------------

pair<int, int> Query::parseIntRange(const string &paramName, const string &fieldName,
                                    const string &fieldValue, size_t delimPos, int maxValue)
{
  int lo = 0, hi = 0;

  if ((delimPos > 0) && (delimPos < (fieldValue.length() - 1)))
  {
    lo = parseIntValue(paramName, fieldName, boost::trim_copy(fieldValue.substr(0, delimPos)),
                       maxValue);
    hi = parseIntValue(paramName, fieldName, boost::trim_copy(fieldValue.substr(delimPos + 1)),
                       maxValue);
  }

  if (lo >= hi)
    throw Fmi::Exception::Trace(
        BCP, paramName + ": Invalid " + fieldName + " range " + fieldValue);

  return make_pair(lo, hi);
}

// ----------------------------------------------------------------------
/*!
 * \brief Parse int values or value ranges, e.g. 1;5-8;11
 */
// ----------------------------------------------------------------------

list<pair<int, int>> Query::parseIntValues(const string &paramName, const string &fieldName,
                                           const string &valueStr, int maxValue)
{
  list<pair<int, int>> intValues;
  set<string> parts;

  boost::algorithm::split(parts, valueStr, boost::algorithm::is_any_of(";"));

  for (auto const &part : parts)
  {
    string s = boost::trim_copy(part);
    auto pos = s.find("-");

    if ((pos == string::npos) || (s == "-1"))
    {
      int value = ((pos == string::npos) ? parseIntValue(paramName, fieldName, s, maxValue) : -1);
      intValues.push_back(make_pair(value, value));
    }
    else
    {
      pair<int, int> rangeValue = parseIntRange(paramName, fieldName, s, pos, maxValue);
      intValues.push_back(rangeValue);
    }
  }

  return intValues;
}

// ----------------------------------------------------------------------
/*!
 * \brief Expand parameter name from level and forecastnumber single values
 */
// ----------------------------------------------------------------------

void Query::expandParameterFromSingleValues(const string &param,
                                            TimeSeries::OptionParsers::ParameterOptions &pOptions,
                                            list<pair<int, int>> &levelRanges,
                                            list<pair<int, int>> &forecastNumberRanges)
{
  // Expand parameter names from listed/single level/forecastnumber values, e.g. 1,11
  //
  // If level/forecastnumber ranges are given, both listed/single values (range start and end
  // are set to the same value) and range start/end values are set to levelRanges and
  // forecastNumberRanges. The expanded parameter names are added to pOptions

  vector<string> paramParts;
  string paramName;
  bool hasRange = false;

  parseRadonParameterName(param, paramParts, true);

  auto leveltype = getParamLevelId(param, paramParts);
  int maxLevel = (leveltype == GridFmiLevelTypeHybrid) ? 199 : 0;

  levelRanges = parseIntValues(param, "level", paramParts[4], maxLevel);

  forecastNumberRanges = parseIntValues(param, "forecast number", paramParts[6], 99);

  if (forecastNumberRanges.empty())
    forecastNumberRanges.push_back(make_pair(-1, -1));

  for (auto const &level : levelRanges)
  {
    if (level.first != level.second)
    {
      hasRange = true;
      continue;
    }

    for (auto const &forecastNumber : forecastNumberRanges)
    {
      if (forecastNumber.first != forecastNumber.second)
      {
        hasRange = true;
        continue;
      }

      paramName.clear();
      paramParts[4] = Fmi::to_string(level.first);

      if (forecastNumber.first >= 0)
        paramParts[6] = Fmi::to_string(forecastNumber.first);
      else
        paramParts.pop_back();

      for (auto const &part : paramParts)
        paramName += (((&part != &paramParts[0]) ? ":" : "") + part);

      pOptions.add(Spine::Parameter(paramName, Spine::Parameter::Type::Data,
                                    FmiParameterName(kFmiPressure + pOptions.size())));
    }
  }

  if (!hasRange)
  {
    levelRanges.clear();
    forecastNumberRanges.clear();
  }
  else
  {
    // Check duplicates/overlapping

    for (auto it = levelRanges.cbegin(); (it != levelRanges.cend()); it++)
      for (auto it2 = next(it); (it2 != levelRanges.cend()); it2++)
        if (
            ((it2->first >= it->first) && (it2->first <= it->second)) ||
            ((it2->second >= it->first) && (it2->second <= it->second))
           )
          throw Fmi::Exception::Trace(BCP, param + ": Duplicate level or overlapping range");

    for (auto it = forecastNumberRanges.cbegin(); (it != forecastNumberRanges.cend()); it++)
      for (auto it2 = next(it); (it2 != forecastNumberRanges.cend()); it2++)
        if (
            ((it2->first >= it->first) && (it2->first <= it->second)) ||
            ((it2->second >= it->first) && (it2->second <= it->second))
           )
          throw Fmi::Exception::Trace(
              BCP, param + ": Duplicate forecast number or overlapping range");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Expand parameter name from level and forecastnumber range values
 */
// ----------------------------------------------------------------------

void Query::expandParameterFromRangeValues(Engine::Grid::Engine *gridEngine,
                                           ptime originTime,
                                           const string &paramName,
                                           const list<pair<int, int>> &levelRanges,
                                           const list<pair<int, int>> &forecastNumberRanges,
                                           TimeSeries::OptionParsers::ParameterOptions &pOptions)
{
  try
  {
    // Expand parameter names from level/forecastnumber ranges (e.g. 5-8) by checking if
    // they have content available. The expanded parameter names are added to pOptions

    vector<string> paramParts;
    parseRadonParameterName(paramName, paramParts, true);

    string param = paramParts[0];
    string producer = paramParts[1];
    T::GeometryId geometryId = getGeometryId(paramName, paramParts);
    T::ParamLevelId levelTypeId = getParamLevelId(paramName, paramParts);
    T::ForecastType forecastType = getForecastType(paramName, paramParts);
    T::ContentInfoList contentInfoList;
    set<T::ParamLevel> levels;

    ptime &sTime = tOptions.startTime, &eTime = tOptions.endTime;
    string originTimeStr(originTime.is_not_a_date_time() ? "" : to_iso_string(originTime));
    string startTimeStr(sTime.is_not_a_date_time() ? "" : to_iso_string(sTime));
    string endTimeStr(eTime.is_not_a_date_time() ? "" : to_iso_string(eTime));

    auto pos = originTimeStr.find(",");
    if (pos != string::npos) originTimeStr = originTimeStr.substr(0, pos);

    if (startTimeStr.empty())
      startTimeStr = "19000101T000000";
    else
    {
      pos = startTimeStr.find(",");
      if (pos != string::npos) startTimeStr = startTimeStr.substr(0, pos);
    }

    if (endTimeStr.empty())
      endTimeStr = "99991231T235959";
    else
    {
      pos = endTimeStr.find(",");
      if (pos != string::npos) endTimeStr = endTimeStr.substr(0, pos);
    }

    if (startTimeStr > endTimeStr)
      startTimeStr = endTimeStr;

    auto cS = gridEngine->getContentServer_sptr();

    for (auto const &levelRange : levelRanges)
    {
      for (auto const &forecastNumberRange : forecastNumberRanges)
      {
        // Process only level range vs single values or vice versa

        if (
            (levelRange.first == levelRange.second) &&
            (forecastNumberRange.first == forecastNumberRange.second)
           )
          continue;

        for (int fN = forecastNumberRange.first; (fN <= forecastNumberRange.second); fN++)
        {
          // TODO: store content data to be used later when loading parameter mappings/details

          contentInfoList.clear();
          levels.clear();

          cS->getContentListByParameterAndProducerName(0,
                                                       producer,
                                                       T::ParamKeyTypeValue::FMI_NAME,
                                                       param,
                                                       levelTypeId,
                                                       levelRange.first,
                                                       levelRange.second,
                                                       forecastType,
                                                       fN,
                                                       geometryId,
                                                       startTimeStr,
                                                       endTimeStr,
                                                       0,
                                                       contentInfoList);

          for (size_t idx = 0; (idx < contentInfoList.getLength()); idx++)
          {
            // Ignore not ready or disabled content or content about to be deleted or
            // with nonmatching origin time

            auto contentInfo = contentInfoList.getContentInfoByIndex(idx);

            if ((contentInfo->mDeletionTime + 5) < time(NULL))
              continue;

            T::GenerationInfo generationInfo;
            cS->getGenerationInfoById(0, contentInfo->mGenerationId, generationInfo);

            if (
                (generationInfo.mStatus != T::GenerationInfo::Status::Ready) ||
                ((! originTimeStr.empty()) && (originTimeStr != generationInfo.mAnalysisTime))
               )
              continue;

            if (!levels.insert(contentInfo->mParameterLevel).second)
              continue;

            paramParts[4] = Fmi::to_string(contentInfo->mParameterLevel);
            paramParts[6] = Fmi::to_string(fN);

            string expandedParamName;

            for (auto const &part : paramParts)
              expandedParamName += (((&part != &paramParts[0]) ? ":" : "") + part);

            pOptions.add(Spine::Parameter(expandedParamName, Spine::Parameter::Type::Data,
                                          FmiParameterName(kFmiPressure + pOptions.size())));
          }
        }
      }
    }
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

void Query::parseParameters(const Spine::HTTP::Request& theReq, Engine::Grid::Engine *gridEngine)
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
    // Generating unique param newbase id's, grib/netcdf param mappings are searched by radon name
    //
    // Expand parameter levels and forecast numbers (e.q. 1;5-8;11) by loading content
    // records for given level/forecastnumber ranges and examining available data.
    // For single/listed level/forecastnumber values expanded parameter names are just added
    // to pOptions, their content is loaded later when fetching the data

    opt = Spine::optional_string(theReq.getParameter("origintime"), "");
    ptime originTime(opt.empty() ? ptime() : Fmi::TimeParser::parse(opt));

    opt = Spine::required_string(theReq.getParameter("param"), "param option is required");
    vector<string> params;
    boost::algorithm::split(params, opt, boost::algorithm::is_any_of(","));

    list<pair<int, int>> levelRanges, forecastNumberRanges;

    for (const string& param : params)
    {
      levelRanges.clear();
      forecastNumberRanges.clear();

      expandParameterFromSingleValues(param, pOptions, levelRanges, forecastNumberRanges);

      if ((!levelRanges.empty()) || (!forecastNumberRanges.empty()))
        expandParameterFromRangeValues(gridEngine, originTime, param, levelRanges, forecastNumberRanges,
                                       pOptions);
    }

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

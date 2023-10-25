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

Query::Query(const Spine::HTTP::Request& req,
             const Engine::Grid::Engine *gridEngine,
             string &originTime)
{
  try
  {
    parseTimeOptions(req);
    parseParameters(req, gridEngine, originTime);
    parseLevels(req);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Parse radon parameter name parts
 *
 */
// ----------------------------------------------------------------------

void Query::parseRadonParameterName(
    const string &param, vector<string> &paramParts, bool expanding) const
{
  if (! expanding)
  {
    auto itp = radonParameters.find(param);

    if (itp != radonParameters.end())
    {
      paramParts = itp->second;
      return;
    }
  }

  vector<string> parts;
  const char *partNames[] = {
                             "parameter",
                             "producer name",
                             "geometryId",
                             "levelTypeId",
                             "level",
                             "forecastType",
                             "forecastNumber"
                            };

  paramParts.clear();

  boost::algorithm::split(parts, param, boost::algorithm::is_any_of(":"));
  if ((parts.size() != 6) && (parts.size() != 7))
    throw Fmi::Exception::Trace(BCP, "Invalid radon parameter name '" + param + "'");

  // Returned vector is later trusted to have entry ([6]) for forecastNumber too, even through
  // it might be missing in parameter name

  if (parts.size() == 6)
    parts.push_back("");

  size_t n = 0;
  for (auto const &part : parts)
  {
    string s = boost::trim_copy(part);
    const char *cp = s.c_str();
    auto length = s.length();

    // Forecastnumber -1 does not work (to query all ensemble members) when fetching
    // content records, and missing (-1) value generally means "any value" for data query;
    // don't allow missing forecastnumber for ensemble data
    //
    // Allow negative value for height level

    if (
        (n == 6) && (s.empty() || (s == "-1")) &&
        (!isEnsembleForecast(getForecastType(param, paramParts)))
       )
    {
      // Forecast number can be missing or have value -1

      s = "-1";
    }
    else if (s.empty())
      throw Fmi::Exception::Trace(
          BCP, string("Missing '") + partNames[n] + "' in radon parameter name '" + param + "'");
    else if ((n > 1) && (!expanding))
    {
      if (
          (n == 4) && (*cp == '-') &&
          (getParamLevelId(param, paramParts) == GridFmiLevelTypeHeight)
         )
      {
        cp++;
        length--;
      }

      if (strspn(cp, "1234567890") != length)
        throw Fmi::Exception::Trace(
            BCP, string("Invalid '") + partNames[n] + "' in radon parameter name '" + param + "'");
    }

    paramParts.push_back(s);
    n++;
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Parse int value
 */
// ----------------------------------------------------------------------

int Query::parseIntValue(const string &paramName, const string &fieldName, const string &fieldValue,
                         bool negativeValueValid, int maxValue)
{
  const char *cp = fieldValue.c_str();
  auto length = fieldValue.length();

  if (negativeValueValid && (*cp == '-'))
  {
    cp++;
    length--;
  }

  if (strspn(cp, "1234567890") != length)
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
                                    const string &fieldValue, size_t delimPos,
                                    bool negativeValueValid, int maxValue)
{
  int lo = 0, hi = 0;

  if ((delimPos > 0) && (delimPos < (fieldValue.length() - 1)))
  {
    lo = parseIntValue(paramName, fieldName, boost::trim_copy(fieldValue.substr(0, delimPos)),
                       negativeValueValid, maxValue);
    hi = parseIntValue(paramName, fieldName, boost::trim_copy(fieldValue.substr(delimPos + 1)),
                       negativeValueValid, maxValue);
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
                                           const string &valueStr, bool negativeValueValid,
                                           int maxValue)
{
  list<pair<int, int>> intValues;
  set<string> parts;

  boost::algorithm::split(parts, valueStr, boost::algorithm::is_any_of(";"));

  for (auto const &part : parts)
  {
    string s = boost::trim_copy(part);
    auto pos = s.find("-");

    if (pos == 0)
       pos = s.find("-", 1);

    if (pos == string::npos)
    {
      int value = parseIntValue(paramName, fieldName, s, negativeValueValid, maxValue);
      intValues.push_back(make_pair(value, value));
    }
    else
    {
      pair<int, int> rangeValue =
          parseIntRange(paramName, fieldName, s, pos, negativeValueValid, maxValue);
      intValues.push_back(rangeValue);
    }
  }

  return intValues;
}

// ----------------------------------------------------------------------
/*!
 * \brief Parse level and forecastnumber ranges from parameter name
 */
// ----------------------------------------------------------------------

void Query::parseParameterLevelAndForecastNumberRanges(
    const string &param,
    bool gribOutput,
    TimeSeries::OptionParsers::ParameterOptions &pOptions,
    list<pair<int, int>> &levelRanges,
    list<pair<int, int>> &forecastNumberRanges)
{
  // Both listed/single values (e.g. 1;11, range start and end are set to the same value) and
  // range start/end values (e.g 5-8) are set to levelRanges and forecastNumberRanges.
  //
  // Heigth level value can be negative. Forecast number can have negative value (-1) for
  // deterministic forecast

  vector<string> paramParts;
  string paramName, fcNumber;

  parseRadonParameterName(param, paramParts, true);

  auto leveltype = getParamLevelId(param, paramParts);

  if (!isSupportedGridLevelType(gribOutput, FmiLevelType(leveltype)))
    return;

  bool negativeLevelValid = (leveltype == GridFmiLevelTypeHeight);
  bool negativeForecastNumberValid =
      ((paramParts[6] == "-1") && (!isEnsembleForecast(getForecastType(param, paramParts))));
  int maxLevel = (leveltype == GridFmiLevelTypeHybrid) ? 199 : 0;

  levelRanges = parseIntValues(param, "level", paramParts[4], negativeLevelValid, maxLevel);

  forecastNumberRanges =
      parseIntValues(param, "forecast number", paramParts[6], negativeForecastNumberValid, 99);

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

// ----------------------------------------------------------------------
/*!
 * \brief Load generation data for given origintime or for parameters
 *        latest common origitintime
 */
// ----------------------------------------------------------------------

bool Query::loadOriginTimeGenerations(Engine::Grid::ContentServer_sptr cS,
                                      const vector<string> &params,
                                      string &originTime)
{
  try
  {
    vector<string> paramParts;
    string commonOriginTime;

    for (const string &param : params)
    {
      parseRadonParameterName(param, paramParts, true);

      const string &producer = paramParts[1];
      auto pg = producerGenerations.find(producer);

      if (pg != producerGenerations.end())
        continue;

      pg = producerGenerations.insert(make_pair(producer, OriginTimeGenerations())).first;

      T::GenerationInfoList generationInfoList;

      generationInfoList.setComparisonMethod(T::GenerationInfo::ComparisonMethod::analysisTime);
      cS->getGenerationInfoListByProducerName(0, producer, generationInfoList);

      size_t idx = generationInfoList.getLength();
      if (idx == 0)
        continue;

      if (! originTime.empty())
      {
        auto generationInfo = generationInfoList.getGenerationInfoByAnalysisTime(originTime);

        if (generationInfo && isValidGeneration(generationInfo))
        {
          generationInfos.insert(make_pair(generationInfo->mGenerationId, *generationInfo));
          pg->second.insert(make_pair(originTime, generationInfo->mGenerationId));

          if (commonOriginTime.empty())
            commonOriginTime = originTime;
        }

        continue;
      }

      // Generations are fetched to ascending analysistime order

      for (; ((idx > 0) && (pg->second.size() < 2)); idx--)
      {
        auto generationInfo = generationInfoList.getGenerationInfoByIndex(idx - 1);

        if (isValidGeneration(generationInfo))
        {
          generationInfos.insert(make_pair(generationInfo->mGenerationId, *generationInfo));
          pg->second.insert(
              make_pair(generationInfo->mAnalysisTime, generationInfo->mGenerationId));
        }
      }

      if (pg->second.empty())
        continue;

      if (commonOriginTime.empty())
      {
        commonOriginTime = pg->second.rbegin()->first;
        continue;
      }

      // Get common origintime

      auto og = pg->second.rbegin();

      for (; (og != pg->second.rend()); og++)
      {
        auto pg2 = producerGenerations.begin();

        for (; (pg2 != producerGenerations.end()); pg2++)
        {
          if ((pg2 == pg) || (pg2->second.empty()))
            continue;

          if (pg2->second.find(og->first) == pg2->second.end())
            break;
        }

        if (pg2 == producerGenerations.end())
        {
          commonOriginTime = og->first;
          break;
        }
      }

      if (og == pg->second.rend())
        throw Fmi::Exception(BCP, "Data has no common origintime");
    }

    originTime = commonOriginTime;

    return (!originTime.empty());
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Get generation id for given producer and origin time
 */
// ----------------------------------------------------------------------

bool Query::getOriginTimeGeneration(Engine::Grid::ContentServer_sptr cS,
                                    const string &producer,
                                    const string &originTime,
                                    uint &generationId)
{
  try
  {
    auto pg = producerGenerations.find(producer);

    if (pg == producerGenerations.end())
      throw Fmi::Exception(
          BCP, "getOriginTimeGeneration: internal: producer not found");

    auto og = pg->second.find(originTime);

    if (og != pg->second.end())
    {
      generationId = og->second;

      auto generationInfo = generationInfos.find(generationId);

      if (generationInfo == generationInfos.end())
        throw Fmi::Exception(
            BCP, "getOriginTimeGeneration: internal: generationId not found");

      // Ignore too old content

      return (isValidGeneration(&(generationInfo->second)));
    }

    return false;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Expand parameter name from level and forecastnumber range values
 */
// ----------------------------------------------------------------------

void Query::expandParameterFromRangeValues(const Engine::Grid::Engine *gridEngine,
                                           ptime originTime,
                                           const string &paramName,
                                           bool gribOutput,
                                           const list<pair<int, int>> &levelRanges,
                                           const list<pair<int, int>> &forecastNumberRanges,
                                           TimeSeries::OptionParsers::ParameterOptions &pOptions)
{
  try
  {
    // Expand parameter names from level/forecastnumber ranges (e.g. 2-2 or 5-8) by checking if
    // they have content available. The expanded parameter names are added to pOptions

    if (originTime.is_not_a_date_time())
      throw Fmi::Exception(
          BCP, "expandParameterFromRangeValues: internal: originTime is not set");

    vector<string> paramParts;
    parseRadonParameterName(paramName, paramParts, true);

    T::ParamLevelId levelTypeId = getParamLevelId(paramName, paramParts);

    if (!isSupportedGridLevelType(gribOutput, FmiLevelType(levelTypeId)))
      return;

    const string &param = paramParts[0];
    const string &producer = paramParts[1];
    T::GeometryId geometryId = getGeometryId(paramName, paramParts);
    T::ForecastType forecastType = getForecastType(paramName, paramParts);
    T::ContentInfoList contentInfoList;
    map<T::ParamLevel, ParameterContents::iterator> levels;

    ptime sTime, eTime;
    if (!tOptions.startTimeData)
      sTime = tOptions.startTime;
    if (!tOptions.endTimeData)
      eTime = tOptions.endTime;

    string originTimeStr(to_iso_string(originTime));
    string startTimeStr(sTime.is_not_a_date_time() ? "" : to_iso_string(sTime));
    string endTimeStr(eTime.is_not_a_date_time() ? "" : to_iso_string(eTime));
    string fcNumber;

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

    // Get generation id for the requested or latest common origintime

    auto cS = gridEngine->getContentServer_sptr();
    uint generationId;

    if (!getOriginTimeGeneration(cS, producer, originTimeStr, generationId))
      return;

    for (auto const &levelRange : levelRanges)
    {
      for (auto const &forecastNumberRange : forecastNumberRanges)
      {
        for (int fN = forecastNumberRange.first; (fN <= forecastNumberRange.second); fN++)
        {
          contentInfoList.clear();
          levels.clear();

          cS->getContentListByParameterAndGenerationId(0,
                                                       generationId,
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

          auto contentLength = contentInfoList.getLength();

          for (size_t idx = 0; (idx < contentLength); idx++)
          {
            auto contentInfo = contentInfoList.getContentInfoByIndex(idx);
            auto levelContents = levels.find(contentInfo->mParameterLevel);
            auto cI = new T::ContentInfo(*contentInfo);

            if (levelContents != levels.end())
            {
              levelContents->second->second.addContentInfo(cI);
              continue;
            }

            paramParts[4] = Fmi::to_string(contentInfo->mParameterLevel);

            fcNumber = Fmi::to_string(fN);
            if (fN >= 0)
              paramParts[6] = fcNumber;
            else
              paramParts.pop_back();

            string expandedParamName;

            for (auto const &part : paramParts)
              expandedParamName += (((&part != &paramParts[0]) ? ":" : "") + part);

            pOptions.add(Spine::Parameter(expandedParamName, Spine::Parameter::Type::Data,
                                          FmiParameterName(kFmiPressure + pOptions.size())));

            if (fN < 0)
              paramParts.push_back(fcNumber);

            radonParameters.insert(make_pair(expandedParamName, paramParts));

            auto paramContents = parameterContents.insert(
                make_pair(expandedParamName, T::ContentInfoList())).first;
            paramContents->second.addContentInfo(cI);

            levels.insert(make_pair(contentInfo->mParameterLevel, paramContents));
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

void Query::parseParameters(const Spine::HTTP::Request& theReq,
                            const Engine::Grid::Engine *gridEngine,
                            string &originTimeStr)
{
  try
  {
    string opt = Spine::optional_string(theReq.getParameter("source"), "querydata");

    if ((opt != "grid") && (opt != "gridcontent"))
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
    //
    // First load generation info for the parameters to load content records and get latest
    // common origintime if origintime is not given

    opt = Spine::required_string(theReq.getParameter("format"), "format option is required");
    Fmi::ascii_toupper(opt);
    bool gribOutput = (opt != "NETCDF");

    opt = Spine::required_string(theReq.getParameter("param"), "param option is required");
    vector<string> params;
    boost::algorithm::split(params, opt, boost::algorithm::is_any_of(","));

    if (!originTimeStr.empty())
    {
      // YYYYMMDDHHMM[SS] to YYYYMMDDSSTHHMMSS

      ptime originTime = Fmi::TimeParser::parse(originTimeStr);
      originTimeStr = to_iso_string(originTime);

      auto pos = originTimeStr.find(",");
      if (pos != string::npos)
        originTimeStr = originTimeStr.substr(0, pos);
    }

    auto cS = gridEngine->getContentServer_sptr();

    if (!loadOriginTimeGenerations(cS, params, originTimeStr))
      throw Fmi::Exception::Trace(BCP, "No data available");

    ptime originTime(Fmi::TimeParser::parse(originTimeStr));

    list<pair<int, int>> levelRanges, forecastNumberRanges;

    for (const string& param : params)
    {
      levelRanges.clear();
      forecastNumberRanges.clear();

      parseParameterLevelAndForecastNumberRanges(
          param, gribOutput, pOptions, levelRanges, forecastNumberRanges);

      expandParameterFromRangeValues(gridEngine, originTime, param, gribOutput, levelRanges,
                                     forecastNumberRanges, pOptions);
    }

    if (pOptions.size() == 0)
      throw Fmi::Exception::Trace(BCP, "No data available");
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
    auto now = Spine::optional_string(theReq.getParameter("now"), "");
    auto startTime = Spine::optional_string(theReq.getParameter("starttime"), "");
    auto endTime = Spine::optional_string(theReq.getParameter("endtime"), "");

    unsigned long timeStep = 0;
    auto opt = theReq.getParameter("timestep");

    if (opt && (*opt != "data"))
      timeStep = Spine::optional_unsigned_long(opt, 0);

    tOptions = TimeSeries::parseTimes(theReq);
    tOptions.startTimeData = (startTime.empty() && now.empty());
    tOptions.timeStep = timeStep;
    tOptions.endTimeData = (endTime.empty() && (timeStep == 0));

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
      if ((source == "grid") || (source == "gridcontent"))
        throw Fmi::Exception(BCP, "Cannot specify level option with grid content data");

      levels.insert(Fmi::stoi(opt));
    }

    // Allow also "levels"

    opt = Spine::optional_string(theReq.getParameter("levels"), "");

    if (!opt.empty())
    {
      if ((source == "grid") || (source == "gridcontent"))
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

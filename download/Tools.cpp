#include "Tools.h"

using namespace std;

namespace SmartMet
{
namespace Plugin
{
namespace Download
{
static time_t GridGenerationDeletionTimeOffset = 5;

// ----------------------------------------------------------------------
/*!
 * \brief Utility routines for testing level type
 */
// ----------------------------------------------------------------------

bool isGroundLevel(FmiLevelType levelType)
{
  return (levelType == GridFmiLevelTypeGround);
}

bool isSurfaceLevel(FmiLevelType levelType)
{
  return ((levelType == kFmiGroundSurface) || (levelType == kFmiAnyLevelType));
}

bool isPressureLevel(FmiLevelType levelType, bool gridContent)
{
  if (gridContent)
    return (levelType == GridFmiLevelTypePressure);

  return (levelType == kFmiPressureLevel);
}

bool isHybridLevel(FmiLevelType levelType, bool gridContent)
{
  if (gridContent)
    return (levelType == GridFmiLevelTypeHybrid);

  return (levelType == kFmiHybridLevel);
}

bool isHeightOrDepthLevel(FmiLevelType levelType)
{
  return ((levelType == kFmiHeight) || (levelType == kFmiDepth));
}

bool isHeightLevel(FmiLevelType levelType, int levelValue, bool gridContent)
{
  if (gridContent)
    return (levelType == GridFmiLevelTypeHeight);

  return ((levelType == kFmiHeight) && (levelValue >= 0));
}

bool isEntireAtmosphereLevel(FmiLevelType levelType)
{
  return (levelType == GridFmiLevelTypeEntireAtmosphere);
}

bool isDepthLevel(FmiLevelType levelType, int levelValue, bool gridContent)
{
  if (gridContent)
    return (levelType == GridFmiLevelTypeDepth);

  return (((levelType == kFmiHeight) && (levelValue < 0)) || (levelType == kFmiDepth));
}

bool isNominalTopLevel(FmiLevelType levelType, bool gridContent)
{
  if (gridContent)
    return (levelType == GridFmiLevelTypeNominalTop);

  return false;
}

bool isMeanSeaLevel(FmiLevelType levelType, bool gridContent)
{
  if (gridContent)
    return (levelType == GridFmiLevelTypeMeanSea);

  return false;
}

bool isSupportedGridLevelType(bool gribOutput, FmiLevelType levelType)
{
  if (gribOutput)
    return (
            isGroundLevel(levelType) ||
            isEntireAtmosphereLevel(levelType) ||
            isPressureLevel(levelType, true) ||
            isHybridLevel(levelType, true) ||
            isHeightLevel(levelType, 0, true) ||
            isDepthLevel(levelType, 0, true) ||
            isNominalTopLevel(levelType, true) ||
            isMeanSeaLevel(levelType, true)
           );

  return (
          isPressureLevel(levelType, true) || isHybridLevel(levelType, true) ||
          isHeightLevel(levelType, 0, true) || isDepthLevel(levelType, 0, true)
         );
}

// ----------------------------------------------------------------------
/*!
 * \brief Utility routine for getting projection parameter's value from srs
 */
// ----------------------------------------------------------------------

double getProjParam(const OGRSpatialReference &srs,
                    const char *param,
                    bool ignoreErr,
                    double defaultValue)
{
  try
  {
    OGRErr err;

    double v = srs.GetNormProjParm(param, defaultValue, &err);

    if (err != OGRERR_NONE)
    {
      if (ignoreErr)
        return defaultValue;
      else
        throw Fmi::Exception(BCP, string("Getting projection parameter '") + param + "' failed");
    }

    return v;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Utility routine for getting querydata's level type
 */
// ----------------------------------------------------------------------

FmiLevelType getLevelTypeFromData(Engine::Querydata::Q q,
                                  const string &producer,
                                  FmiLevelType &nativeLevelType,
                                  bool &positiveLevels)
{
  try
  {
    q->firstLevel();

    // BUG: Onko bugi kun on auto mukana??
    auto levelType = nativeLevelType = q->levelType();

    if ((!isSurfaceLevel(levelType)) && (!isHybridLevel(levelType)) &&
        (!isPressureLevel(levelType)) && (!isHeightOrDepthLevel(levelType)))
    {
      throw Fmi::Exception(BCP,
                             "Internal: Unrecognized level type '" +
                                 boost::lexical_cast<string>(levelType) + "' for producer '" +
                                 producer + "'");
    }

    positiveLevels = true;

    if (isHeightOrDepthLevel(levelType))
    {
      // Height level data with negative levels is returned as kFmiDepth; check the second level
      // (first might be 0).
      //
      if (!q->nextLevel())
        q->firstLevel();

      if (q->levelValue() < 0)
      {
        levelType = kFmiDepth;
        positiveLevels = false;
      }
    }

    return levelType;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Utility routine for testing querydata's level order
 */
// ----------------------------------------------------------------------

bool areLevelValuesInIncreasingOrder(Engine::Querydata::Q q)
{
  try
  {
    q->firstLevel();

    if (isSurfaceLevel(q->levelType()))
      return true;

    double firstLevel = q->levelValue();

    if (!q->nextLevel())
      return true;

    double secondLevel = q->levelValue();

    // Note: Height level data can have negative levels.

    return (fabs(secondLevel) > fabs(firstLevel));
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Check if generation is valid
 *
 */
// ----------------------------------------------------------------------

bool isValidGeneration(const T::GenerationInfo *generationInfo)
{
  return (
          (
           (generationInfo->mDeletionTime == 0) ||
           (generationInfo->mDeletionTime >= (time(NULL) + GridGenerationDeletionTimeOffset))
          ) &&
          (generationInfo->mStatus == T::GenerationInfo::Status::Ready)
         );
}

// ----------------------------------------------------------------------
/*!
 * \brief Check for ensemble forecast
 *
 */
// ----------------------------------------------------------------------

bool isEnsembleForecast(T::ForecastType forecastType)
{
  return ((forecastType == 3) || (forecastType == 4));
}

// ----------------------------------------------------------------------
/*!
 * \brief Return radon parameter geometry id
 *
 */
// ----------------------------------------------------------------------

T::GeometryId getGeometryId(const string &param, const vector<string> &paramParts,
                            boost::optional<T::GeometryId> defaultValue)
{
  if ((paramParts.size() < 3) || paramParts[2].empty())
  {
    if (defaultValue)
     return *defaultValue;

    throw Fmi::Exception::Trace(
        BCP, "Geometry id missing in radon parameter name '" + param + "'");
  }

  return atoi(paramParts[2].c_str());
}

// ----------------------------------------------------------------------
/*!
 * \brief Return radon parameter level type
 *
 */
// ----------------------------------------------------------------------

T::ParamLevelId getParamLevelId(const string &param, const vector<string> &paramParts,
                                boost::optional<T::ParamLevelId> defaultValue)
{
  if ((paramParts.size() < 4) || paramParts[3].empty())
  {
    if (defaultValue)
     return *defaultValue;

    throw Fmi::Exception::Trace(
        BCP, "Level type missing in radon parameter name '" + param + "'");
  }

  return atoi(paramParts[3].c_str());
}

// ----------------------------------------------------------------------
/*!
 * \brief Return radon parameter level number
 *
 */
// ----------------------------------------------------------------------

T::ParamLevel getParamLevel(const string &param, const vector<string> &paramParts,
                            boost::optional<T::ParamLevel> defaultValue)
{
  if ((paramParts.size() < 5) || paramParts[4].empty())
  {
    if (defaultValue)
     return *defaultValue;

    throw Fmi::Exception::Trace(
        BCP, "Level number missing in radon parameter name '" + param + "'");
  }

  return atoi(paramParts[4].c_str());
}

// ----------------------------------------------------------------------
/*!
 * \brief Return radon parameter forecast type
 *
 */
// ----------------------------------------------------------------------

T::ForecastType getForecastType(const string &param, const vector<string> &paramParts,
                                boost::optional<T::ForecastType> defaultValue)
{
  if ((paramParts.size() < 6) || paramParts[5].empty())
  {
    if (defaultValue)
     return *defaultValue;

    throw Fmi::Exception::Trace(
        BCP, "Forecast type missing in radon parameter name '" + param + "'");
  }

  return atoi(paramParts[5].c_str());
}

// ----------------------------------------------------------------------
/*!
 * \brief Return radon parameter forecast number
 *
 */
// ----------------------------------------------------------------------

T::ForecastType getForecastNumber(const string &param, const vector<string> &paramParts,
                                  boost::optional<T::ForecastNumber> defaultValue)
{
  if ((paramParts.size() < 7) || paramParts[6].empty())
  {
    if (defaultValue)
     return *defaultValue;

    throw Fmi::Exception::Trace(
        BCP, "Forecast number missing in radon parameter name '" + param + "'");
  }

  return atoi(paramParts[6].c_str());
}

}  // namespace Download
}  // namespace Plugin
}  // namespace SmartMet

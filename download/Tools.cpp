#include "Tools.h"

static const long maxChunkLengthInBytes = 2048 * 2048;  // Max length of data chunk to return
static const long maxMsgChunks = 30;  // Max # of data chunks collected and returned as one chunk

using namespace std;

using namespace boost::gregorian;
using namespace boost::posix_time;

namespace SmartMet
{
namespace Plugin
{
namespace Download
{
// ----------------------------------------------------------------------
/*!
 * \brief Utility routines for testing querydata's level type
 */
// ----------------------------------------------------------------------

bool isSurfaceLevel(FmiLevelType levelType)
{
  return ((levelType == kFmiGroundSurface) || (levelType == kFmiAnyLevelType));
}

bool isPressureLevel(FmiLevelType levelType)
{
  return (levelType == kFmiPressureLevel);
}

bool isHybridLevel(FmiLevelType levelType)
{
  return (levelType == kFmiHybridLevel);
}

bool isHeightOrDepthLevel(FmiLevelType levelType)
{
  return ((levelType == kFmiHeight) || (levelType == kFmiDepth));
}

bool isHeightLevel(FmiLevelType levelType, int levelValue)
{
  return ((levelType == kFmiHeight) && (levelValue >= 0));
}

bool isDepthLevel(FmiLevelType levelType, int levelValue)
{
  return (((levelType == kFmiHeight) && (levelValue < 0)) || (levelType == kFmiDepth));
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
        throw Spine::Exception(BCP, string("Getting projection parameter '") + param + "' failed");
    }

    return v;
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
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
      throw Spine::Exception(BCP,
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
    throw Spine::Exception::Trace(BCP, "Operation failed!");
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
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

}  // namespace Download
}  // namespace Plugin
}  // namespace SmartMet

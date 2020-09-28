#pragma once

#include <boost/algorithm/string/trim.hpp>
#include <boost/optional.hpp>
#include <engines/querydata/Q.h>
#include <ogr_spatialref.h>
#include <newbase/NFmiLevelType.h>
#include <newbase/NFmiPoint.h>
#include <macgyver/Exception.h>
#include <list>
#include <string>
#include <utility>
#include <vector>

using Scaling = std::list<std::pair<float, float>>;

struct BBoxCorners
{
  NFmiPoint bottomLeft;
  NFmiPoint topRight;
};

#define BOTTOMLEFT 0
#define TOPRIGHT 1

namespace SmartMet
{
namespace Plugin
{
namespace Download
{
bool isSurfaceLevel(FmiLevelType levelType);
bool isPressureLevel(FmiLevelType levelType);
bool isHybridLevel(FmiLevelType levelType);
bool isHeightOrDepthLevel(FmiLevelType levelType);
bool isHeightLevel(FmiLevelType levelType, int levelValue);
bool isDepthLevel(FmiLevelType levelType, int levelValue);

FmiLevelType getLevelTypeFromData(Engine::Querydata::Q q,
                                  const std::string &producer,
                                  FmiLevelType &nativeLevelType,
                                  bool &positiveLevels);

bool areLevelValuesInIncreasingOrder(Engine::Querydata::Q q);

double getProjParam(const OGRSpatialReference &srs,
                    const char *param,
                    bool ignoreErr = false,
                    double defaultValue = 0.0);

// ----------------------------------------------------------------------
/*!
 * \brief Return pairs of values from comma separated string
 */
// ----------------------------------------------------------------------

template <typename T>
boost::optional<std::vector<std::pair<T, T>>> nPairsOfValues(std::string &pvs,
                                                             const char *param,
                                                             std::size_t nPairs)
{
  try
  {
    boost::optional<std::vector<std::pair<T, T>>> pvalue;
    boost::trim(pvs);

    if (pvs.empty())
      return pvalue;

    try
    {
      std::vector<std::string> flds;
      boost::split(flds, pvs, boost::is_any_of(","));
      size_t nValues = 2 * nPairs;

      if (flds.size() != nValues)
        throw Fmi::Exception(
            BCP, std::string("Invalid value for parameter '") + param + "': '" + pvs + "'");

      std::size_t n;

      for (n = 0; (n < nValues); n++)
      {
        boost::trim(flds[n]);

        if (flds[n].empty())
          throw Fmi::Exception(
              BCP, std::string("Invalid value for parameter '") + param + "': '" + pvs + "'");
      }

      std::vector<std::pair<T, T>> pvv;
      std::size_t np;

      for (np = 0, n = 0; (n < nValues); np++, n += 2)
        pvv.push_back(std::make_pair<T, T>(boost::lexical_cast<T>(flds[n]),
                                           boost::lexical_cast<T>(flds[n + 1])));

      pvalue = pvv;

      return pvalue;
    }
    catch (...)
    {
    }

    throw Fmi::Exception(
        BCP, std::string("Invalid value for parameter '") + param + "': '" + pvs + "'");
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

}  // namespace Download
}  // namespace Plugin
}  // namespace SmartMet

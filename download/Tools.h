#pragma once

#include <boost/algorithm/string/trim.hpp>
#include <optional>
#include <engines/querydata/Q.h>
#include <grid-files/grid/Typedefs.h>
#include <ogr_spatialref.h>
#include <newbase/NFmiLevelType.h>
#include <newbase/NFmiPoint.h>
#include <macgyver/Exception.h>
#include <list>
#include <string>
#include <utility>
#include <vector>
#include <grid-content/contentServer/definition/GenerationInfo.h>

using Scaling = std::list<std::pair<float, float>>;

struct BBoxCorners
{
  BBoxCorners(){};
  BBoxCorners(const NFmiPoint &bl, const NFmiPoint &tr) : bottomLeft(bl), topRight(tr){};

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
/*
  1;GROUND;Ground or water surface;
  2;PRESSURE;Pressure level;
  3;HYBRID;Hybrid level;
  4;ALTITUDE;Altitude;
  5;TOP;Top of atmosphere;
  6;HEIGHT;Height above ground in meters;
  7;MEANSEA;Mean sea level;
  8;ENTATM;Entire atmosphere;
  9;GROUND_DEPTH;Layer between two depths below land surface;
 10;DEPTH;Depth below some surface;
 11;PRESSURE_DELTA;Level at specified pressure difference from ground to level;
 12;MAXTHETAE;Level where maximum equivalent potential temperature is found;
 13;HEIGHT_LAYER;Layer between two metric heights above ground;
 14;DEPTH_LAYER;Layer between two depths below land surface;
 15;ISOTHERMAL;Isothermal level, temperature in 1/100 K;
 16;MAXWIND;Maximum wind level;
*/

static const T::ParamLevelId GridFmiLevelTypeNone = 0;
static const T::ParamLevelId GridFmiLevelTypeGround = 1;
static const T::ParamLevelId GridFmiLevelTypePressure = 2;
static const T::ParamLevelId GridFmiLevelTypeHybrid = 3;
static const T::ParamLevelId GridFmiLevelTypeNominalTop = 5;
static const T::ParamLevelId GridFmiLevelTypeHeight = 6;
static const T::ParamLevelId GridFmiLevelTypeMeanSea = 7;
static const T::ParamLevelId GridFmiLevelTypeEntireAtmosphere = 8;
static const T::ParamLevelId GridFmiLevelTypeDepth = 10;
static const T::ParamLevelId GridFmiLevelTypeMostUnstableParcel = 21;

bool isGroundLevel(FmiLevelType levelType);
bool isSurfaceLevel(FmiLevelType levelType);
bool isPressureLevel(FmiLevelType levelType, bool gridContent = false);
bool isHybridLevel(FmiLevelType levelType, bool gridContent = false);
bool isHeightOrDepthLevel(FmiLevelType levelType);
bool isHeightLevel(FmiLevelType levelType, int levelValue, bool gridContent = false);
bool isEntireAtmosphereLevel(FmiLevelType levelType);
bool isDepthLevel(FmiLevelType levelType, int levelValue, bool gridContent = false);
bool isNominalTopLevel(FmiLevelType levelType, bool gridContent = false);
bool isSupportedGridLevelType(bool gribOutput, FmiLevelType levelType);
bool isMeanSeaLevel(FmiLevelType levelType, bool gridContent = false);
bool isMostUnstableParcelLevel(FmiLevelType levelType, bool gridContent = false);

FmiLevelType getLevelTypeFromData(Engine::Querydata::Q q,
                                  const std::string &producer,
                                  FmiLevelType &nativeLevelType,
                                  bool &positiveLevels);

bool areLevelValuesInIncreasingOrder(Engine::Querydata::Q q);

double getProjParam(const OGRSpatialReference &srs,
                    const char *param,
                    bool ignoreErr = false,
                    double defaultValue = 0.0);

void parseRadonParameterName(const std::string &param, std::vector<std::string> &paramParts,
                             bool expanding = false);

std::string getProducerName(
    const std::string &param,
    const std::vector<std::string> &paramParts,
    std::optional<std::string> defaultValue = std::optional<std::string>());
T::GeometryId getGeometryId(
    const std::string &param,
    const std::vector<std::string> &paramParts,
    std::optional<T::GeometryId> defaultValue = std::optional<T::GeometryId>());
T::ParamLevelId getParamLevelId(
    const std::string &param,
    const std::vector<std::string> &paramParts,
    std::optional<T::ParamLevelId> defaultValue = std::optional<T::ParamLevelId>());
T::ParamLevel getParamLevel(
    const std::string &param,
    const std::vector<std::string> &paramParts,
    std::optional<T::ParamLevel> defaultValue = std::optional<T::ParamLevel>());
T::ForecastType getForecastType(
    const std::string &param,
    const std::vector<std::string> &paramParts,
    std::optional<T::ForecastType> defaultValue = std::optional<T::ForecastType>());
T::ForecastNumber getForecastNumber(
    const std::string &param,
    const std::vector<std::string> &paramParts,
    std::optional<T::ForecastNumber> defaultValue = std::optional<T::ForecastNumber>());

bool isValidGeneration(const T::GenerationInfo *generationInfo);
bool isEnsembleForecast(T::ForecastType forecastType);

// ----------------------------------------------------------------------
/*!
 * \brief Return pairs of values from comma separated string
 */
// ----------------------------------------------------------------------

template <typename T>
std::optional<std::vector<std::pair<T, T>>> nPairsOfValues(std::string &pvs,
                                                             const char *param,
                                                             std::size_t nPairs)
{
  try
  {
    std::optional<std::vector<std::pair<T, T>>> pvalue;
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

      for (n = 0; (n < nValues); n += 2)
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

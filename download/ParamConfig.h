// ======================================================================
/*!
 * \brief Parameter configuration
 */
// ======================================================================

#pragma once

#include <newbase/NFmiLevel.h>
#include <newbase/NFmiParam.h>

#include <boost/filesystem.hpp>
#include <boost/optional.hpp>

#include <string>
#include <vector>

namespace SmartMet
{
namespace Plugin
{
namespace Download
{
// Parameter configuration

struct ParamChangeItem
{
 public:
  ParamChangeItem();
  ParamChangeItem(const ParamChangeItem &theOther);
  ~ParamChangeItem();
  ParamChangeItem &operator=(const ParamChangeItem &theOther);

  unsigned long itsOriginalParamId;
  NFmiParam itsWantedParam;
  float itsConversionBase;  // f(x) = (scale * x) + base
  float itsConversionScale;
  NFmiLevel *itsLevel;
  std::string itsLevelType;              // Temporary storage for level type ..
  boost::optional<float> itsLevelValue;  // .. and level value; used when creating NFmiLevel object
                                         // (itsLevel)
  std::string itsStepType;  // For average, cumulative etc. data; "accum", "max", "min", ...
  unsigned int itsPeriodLengthMinutes;  // Period length in minutes for average, cumulative etc.
                                        // data
  std::string itsUnit;                  // Unit for netcdf parameters
  std::string itsStdName;               // Standfard name for netcdf parameters
  std::string itsLongName;              // Long name for netcdf parameters
  std::string itsCentre;                // Originating centre for grib parameters
  unsigned int itsTemplateNumber;       // 'productDefinitionTemplateNumber' for grib parameters
};

typedef std::vector<ParamChangeItem> ParamChangeTable;
ParamChangeTable readParamConfig(const boost::filesystem::path &configFilePath, bool grib = true);

}  // namespace Download
}  // namespace Plugin
}  // namespace SmartMet

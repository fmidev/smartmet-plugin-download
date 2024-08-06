// ======================================================================
/*!
 * \brief Parameter configuration
 */
// ======================================================================

#pragma once

#include <newbase/NFmiLevel.h>
#include <newbase/NFmiParam.h>

#include <filesystem>
#include <optional>

#include <string>
#include <vector>

namespace SmartMet
{
namespace Plugin
{
namespace Download
{
// Parameter configuration

typedef struct
{
  std::optional<long> itsParamNumber;
  // grib1
  std::optional<long> itsTable2Version;
  std::optional<long> itsIndicatorOfTimeRange;
  // grib2
  std::optional<long> itsDiscipline;
  std::optional<long> itsCategory;
  std::optional<long> itsTemplateNumber;
  std::optional<long> itsTypeOfStatisticalProcessing;
} GribParamIdentification;

typedef std::optional<GribParamIdentification> GribParamId;

struct ParamChangeItem
{
 public:
  ParamChangeItem();
  ParamChangeItem(const ParamChangeItem &theOther);
  ~ParamChangeItem();
  ParamChangeItem &operator=(const ParamChangeItem &theOther);

  unsigned long itsOriginalParamId;
  NFmiParam itsWantedParam;
  float itsConversionBase;              // f(x) = (scale * x) + base
  float itsConversionScale;
  NFmiLevel *itsLevel;
  std::string itsLevelType;             // Temporary storage for level type ..
  std::optional<float> itsLevelValue; // .. and value; used when creating NFmiLevel object
  std::string itsStepType;              // Aggregate type, "accum", "max", "min", ...
  unsigned int itsPeriodLengthMinutes;  // Aggregate period length in minutes
  std::string itsUnit;                  // Unit for netcdf parameters
  std::string itsStdName;               // Standfard name for netcdf parameters
  std::string itsLongName;              // Long name for netcdf parameters
  std::string itsCentre;                // Originating centre for grib parameters
  std::optional<long> itsTemplateNumber;  // 'productDefinitionTemplateNumber' for grib parameters

  std::optional<bool> itsGridRelative;// Set for grid relative U and V

  // Radon parameter data
  //
  std::string itsRadonProducer;  // SMARTMET etc
  std::string itsRadonName;      // T-K etc
  GribParamId itsGrib1Param;     // Grib1 discipline etc
  GribParamId itsGrib2Param;     // Grib2 discipline etc
};

typedef std::vector<ParamChangeItem> ParamChangeTable;
ParamChangeTable readParamConfig(const std::filesystem::path &configFilePath, bool grib = true);

}  // namespace Download
}  // namespace Plugin
}  // namespace SmartMet

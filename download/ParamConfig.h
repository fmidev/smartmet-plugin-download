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

typedef struct
{
  boost::optional<long> itsParamNumber;
  // grib1
  boost::optional<long> itsTableVersion;
  boost::optional<long> itsIndicatorOfTimeRange;
  // grib2
  boost::optional<long> itsDiscipline;
  boost::optional<long> itsCategory;
  boost::optional<long> itsTemplateNumber;
  boost::optional<long> itsTypeOfStatisticalProcessing;
} GribParamIdentification;

typedef boost::optional<GribParamIdentification> GribParamId;

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
  boost::optional<float> itsLevelValue; // .. and value; used when creating NFmiLevel object
  std::string itsStepType;              // Aggregate type, "accum", "max", "min", ...
  unsigned int itsPeriodLengthMinutes;  // Aggregate period length in minutes
  std::string itsUnit;                  // Unit for netcdf parameters
  std::string itsStdName;               // Standfard name for netcdf parameters
  std::string itsLongName;              // Long name for netcdf parameters
  std::string itsCentre;                // Originating centre for grib parameters
  boost::optional<long> itsTemplateNumber;  // 'productDefinitionTemplateNumber' for grib parameters

  boost::optional<bool> itsGridRelative;// Set for grid relative U and V

  // Radon parameter data
  //
  std::string itsRadonProducer;  // SMARTMET etc
  std::string itsRadonName;      // T-K etc
  GribParamId itsGrib1Param;     // Grib1 discipline etc
  GribParamId itsGrib2Param;     // Grib2 discipline etc
};

typedef std::vector<ParamChangeItem> ParamChangeTable;
ParamChangeTable readParamConfig(const boost::filesystem::path &configFilePath, bool grib = true);

}  // namespace Download
}  // namespace Plugin
}  // namespace SmartMet

// ======================================================================
/*!
 * \brief Parameter configuration loading
 */
// ======================================================================

#include "ParamConfig.h"

#include <boost/filesystem.hpp>
#include <boost/foreach.hpp>
#include <macgyver/StringConversion.h>
#include <macgyver/Exception.h>

#include <fstream>

#include <json/json.h>
#include <json/reader.h>

namespace SmartMet
{
namespace Plugin
{
namespace Download
{
// ======================================================================
/*!
 * \brief Parameter configuration item
 */
// ======================================================================

ParamChangeItem::ParamChangeItem()
    : itsOriginalParamId(0),
      itsWantedParam(
          0, "", kFloatMissing, kFloatMissing, kFloatMissing, kFloatMissing, "%.1f", kLinearly),
      itsConversionBase(0),
      itsConversionScale(1.0),
      itsLevel(nullptr),
      itsPeriodLengthMinutes(0),
      itsTemplateNumber(0),
      itsGridRelative(boost::optional<bool>())
{
}

ParamChangeItem::~ParamChangeItem()
{
  if (itsLevel)
    delete itsLevel;
}

ParamChangeItem::ParamChangeItem(const ParamChangeItem& theOther)
    : itsOriginalParamId(theOther.itsOriginalParamId),
      itsWantedParam(theOther.itsWantedParam),
      itsConversionBase(theOther.itsConversionBase),
      itsConversionScale(theOther.itsConversionScale),
      itsLevel(theOther.itsLevel ? new NFmiLevel(*theOther.itsLevel) : nullptr),
      itsStepType(theOther.itsStepType),
      itsPeriodLengthMinutes(theOther.itsPeriodLengthMinutes),
      itsUnit(theOther.itsUnit),
      itsStdName(theOther.itsStdName),
      itsLongName(theOther.itsLongName),
      itsCentre(theOther.itsCentre),
      itsTemplateNumber(theOther.itsTemplateNumber),
      itsGridRelative(theOther.itsGridRelative)
{
}

ParamChangeItem& ParamChangeItem::operator=(const ParamChangeItem& theOther)
{
  try
  {
    if (this != &theOther)
    {
      itsOriginalParamId = theOther.itsOriginalParamId;
      itsWantedParam = theOther.itsWantedParam;
      itsConversionBase = theOther.itsConversionBase;
      itsConversionScale = theOther.itsConversionScale;
      itsLevel = theOther.itsLevel ? new NFmiLevel(*theOther.itsLevel) : nullptr;
      itsStepType = theOther.itsStepType;
      itsPeriodLengthMinutes = theOther.itsPeriodLengthMinutes;
      itsUnit = theOther.itsUnit;
      itsStdName = theOther.itsStdName;
      itsLongName = theOther.itsLongName;
      itsCentre = theOther.itsCentre;
      itsTemplateNumber = theOther.itsTemplateNumber;
      itsGridRelative = theOther.itsGridRelative;
    }

    return *this;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ======================================================================
/*!
 * \brief Functions to convert a json value to a number or a string.
 */
// ======================================================================
unsigned long asUInt64(const std::string& name, const Json::Value& json, uint arrayIndex)
{
  try
  {
    if (json.isUInt64())
      return json.asUInt64();

    throw Fmi::Exception(BCP,
                           "'" + name + "': uint64 value expected at array index " +
                               Fmi::to_string(arrayIndex) + ", got value " + json.asString() +
                               " instead");
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

unsigned int asUInt(const std::string& name, const Json::Value& json, uint arrayIndex)
{
  try
  {
    if (json.isUInt())
      return json.asUInt();

    throw Fmi::Exception(
        BCP, "'" + name + "': uint value expected at array index " + Fmi::to_string(arrayIndex));
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

float asFloat(const std::string& name, const Json::Value& json, uint arrayIndex)
{
  try
  {
    if (json.isDouble())
      return json.asFloat();

    throw Fmi::Exception(
        BCP, "'" + name + "': float value expected at array index " + Fmi::to_string(arrayIndex));
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

std::string asString(const std::string& name, const Json::Value& json, uint arrayIndex)
{
  try
  {
    // json.isConvertibleTo(Json::stringValue) for a number returns true but json.asString() fails
    //
    // if (json.isConvertibleTo(Json::stringValue))

    if (json.isString())
      return json.asString();

    throw Fmi::Exception(
        BCP, "'" + name + "': string value expected at array index " + Fmi::to_string(arrayIndex));
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ======================================================================
/*!
 * \brief Load grib format specific configuration fields.
 */
// ======================================================================

bool readGribParamConfigField(const std::string& name,
                              const Json::Value& json,
                              ParamChangeItem& p,
                              unsigned int arrayIndex)
{
  try
  {
    if (name == "gribid")
    {
      p.itsOriginalParamId = asUInt64(name, json, arrayIndex);
    }
    else if (name == "leveltype")
    {
      p.itsLevelType = asString(name, json, arrayIndex);
    }
    else if (name == "levelvalue")
    {
      p.itsLevelValue = asFloat(name, json, arrayIndex);
    }
    else if (name == "center")
    {
      p.itsCentre = asString(name, json, arrayIndex);
    }
    else if (name == "templatenumber")
    {
      p.itsTemplateNumber = asUInt(name, json, arrayIndex);
    }
    else
    {
      // Unknown setting
      //
      return false;
    }

    return true;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ======================================================================
/*!
 * \brief Load netcdf format specific configuration fields.
 */
// ======================================================================

bool readNetCdfParamConfigField(const std::string& name,
                                const Json::Value& json,
                                ParamChangeItem& p,
                                unsigned int arrayIndex)
{
  try
  {
    if (name == "standardname")
      p.itsStdName = asString(name, json, arrayIndex);
    else if (name == "longname")
      p.itsLongName = asString(name, json, arrayIndex);
    else if (name == "unit")
      p.itsUnit = asString(name, json, arrayIndex);
    else if (name == "gridrelative")
      // Nonzero (true) when U and V are relative to the grid
      p.itsGridRelative = (asUInt(name, json, arrayIndex) > 0);
    else
      // Unknown setting
      //
      return false;

    return true;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ======================================================================
/*!
 * \brief Load parameter configuration.
 */
// ======================================================================

ParamChangeTable readParamConfig(const boost::filesystem::path& configFilePath, bool grib)
{
  try
  {
    // Read and parse the JSON formatted configuration

    Json::Reader reader;
    Json::Value theJson;
    std::vector<ParamChangeItem> paramChangeTable;

    std::ifstream in(configFilePath.c_str());
    if (!in)
      throw Fmi::Exception(BCP, "Failed to open '" + configFilePath.string() + "' for reading");

    std::string content;
    content.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());

    if (!reader.parse(content, theJson))
      throw Fmi::Exception(BCP,
                             "Failed to parse '" + configFilePath.string() +
                                 "': " + reader.getFormattedErrorMessages());

    if (!theJson.isArray())
      throw Fmi::Exception(BCP, "Parameter configuration must contain an array of JSON objects");

    // Iterate through all the objects

    auto& fmtConfigFunc(grib ? readGribParamConfigField : readNetCdfParamConfigField);

    for (unsigned int i = 0; i < theJson.size(); i++)
    {
      const Json::Value& paramJson = theJson[i];

      if (!paramJson.isObject())
        throw Fmi::Exception(BCP, "JSON object expected at array index " + Fmi::to_string(i));

      ParamChangeItem p;
      std::string paramName;
      uint paramId = 0;

      const auto members = paramJson.getMemberNames();
      BOOST_FOREACH (const auto& name, members)
      {
        const Json::Value& json = paramJson[name];
        if (json.isArray() || json.isObject())
          throw Fmi::Exception(BCP,
                                 name + ": value is neither a string nor a number at array index " +
                                     Fmi::to_string(i));

        // Ignore null values

        if (json.isNull())
          continue;
        //
        // Handle common settings
        //
        if (name == "newbaseid")
          paramId = asUInt(name, json, i);
        else if (name == "name")
          paramName = asString(name, json, i);
        else if (name == "offset")
          p.itsConversionBase = asFloat(name, json, i);
        else if (name == "divisor")
          p.itsConversionScale = asFloat(name, json, i);
        else if (name == "aggregatetype")
          p.itsStepType = asString(name, json, i);
        else if (name == "aggregatelength")
          p.itsPeriodLengthMinutes = asUInt(name, json, i);
        //
        // Handle format specific settings
        //
        else if (!fmtConfigFunc(name, json, p, i))
          throw Fmi::Exception(BCP,
                                 std::string(grib ? "Grib" : "Netcdf") +
                                     " parameter configuration does not have a setting named '" +
                                     name + "'!");
      }

      // Set parameter id and name

      p.itsWantedParam.SetIdent(paramId);
      p.itsWantedParam.SetName(paramName);

      // Create level object if level data was given

      if (p.itsLevelValue || (!p.itsLevelType.empty()))
        p.itsLevel = new NFmiLevel(0, p.itsLevelType, p.itsLevelValue ? *(p.itsLevelValue) : 0);

      paramChangeTable.push_back(p);
    }

    return paramChangeTable;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

}  // namespace Download
}  // namespace Plugin
}  // namespace SmartMet

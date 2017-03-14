// ======================================================================
/*!
 * \brief Parameter configuration loading
 */
// ======================================================================

#include "ParamConfig.h"

#include <boost/filesystem.hpp>
#include <boost/foreach.hpp>
#include <spine/Exception.h>
#include <macgyver/StringConversion.h>

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
      itsLevel(NULL),
      itsPeriodLengthMinutes(0),
      itsTemplateNumber(0)
{
}

ParamChangeItem::~ParamChangeItem()
{
  try
  {
    if (itsLevel)
      delete itsLevel;
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

ParamChangeItem::ParamChangeItem(const ParamChangeItem& theOther)
    : itsOriginalParamId(theOther.itsOriginalParamId),
      itsWantedParam(theOther.itsWantedParam),
      itsConversionBase(theOther.itsConversionBase),
      itsConversionScale(theOther.itsConversionScale),
      itsLevel(theOther.itsLevel ? new NFmiLevel(*theOther.itsLevel) : NULL),
      itsStepType(theOther.itsStepType),
      itsPeriodLengthMinutes(theOther.itsPeriodLengthMinutes),
      itsUnit(theOther.itsUnit),
      itsStdName(theOther.itsStdName),
      itsLongName(theOther.itsLongName),
      itsCentre(theOther.itsCentre),
      itsTemplateNumber(theOther.itsTemplateNumber)
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
      itsLevel = theOther.itsLevel ? new NFmiLevel(*theOther.itsLevel) : NULL;
      itsStepType = theOther.itsStepType;
      itsPeriodLengthMinutes = theOther.itsPeriodLengthMinutes;
      itsUnit = theOther.itsUnit;
      itsStdName = theOther.itsStdName;
      itsLongName = theOther.itsLongName;
      itsCentre = theOther.itsCentre;
      itsTemplateNumber = theOther.itsTemplateNumber;
    }

    return *this;
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
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

    throw SmartMet::Spine::Exception(BCP,
                                     "'" + name + "': uint64 value expected at array index " +
                                         Fmi::to_string(arrayIndex) + ", got value " +
                                         json.asString() + " instead");
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

unsigned int asUInt(const std::string& name, const Json::Value& json, uint arrayIndex)
{
  try
  {
    if (json.isUInt())
      return json.asUInt();

    throw SmartMet::Spine::Exception(
        BCP, "'" + name + "': uint value expected at array index " + Fmi::to_string(arrayIndex));
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

float asFloat(const std::string& name, const Json::Value& json, uint arrayIndex)
{
  try
  {
    if (json.isDouble())
      return json.asFloat();

    throw SmartMet::Spine::Exception(
        BCP, "'" + name + "': float value expected at array index " + Fmi::to_string(arrayIndex));
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
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

    throw SmartMet::Spine::Exception(
        BCP, "'" + name + "': string value expected at array index " + Fmi::to_string(arrayIndex));
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
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
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
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
    else
      // Unknown setting
      //
      return false;

    return true;
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
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
      throw SmartMet::Spine::Exception(
          BCP, "Failed to open '" + configFilePath.string() + "' for reading");

    std::string content;
    content.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());

    if (!reader.parse(content, theJson))
      throw SmartMet::Spine::Exception(BCP,
                                       "Failed to parse '" + configFilePath.string() + "': " +
                                           reader.getFormattedErrorMessages());

    if (!theJson.isArray())
      throw SmartMet::Spine::Exception(
          BCP, "Parameter configuration must contain an array of JSON objects");

    // Iterate through all the objects

    auto& fmtConfigFunc(grib ? readGribParamConfigField : readNetCdfParamConfigField);

    for (unsigned int i = 0; i < theJson.size(); i++)
    {
      const Json::Value& paramJson = theJson[i];

      if (!paramJson.isObject())
        throw SmartMet::Spine::Exception(
            BCP, "JSON object expected at array index " + Fmi::to_string(i));

      ParamChangeItem p;
      std::string paramName;
      uint paramId = 0;

      const auto members = paramJson.getMemberNames();
      BOOST_FOREACH (const auto& name, members)
      {
        const Json::Value& json = paramJson[name];
        if (json.isArray() || json.isObject())
          throw SmartMet::Spine::Exception(
              BCP,
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
          throw SmartMet::Spine::Exception(
              BCP,
              std::string(grib ? "Grib" : "Netcdf") +
                  " parameter configuration does not have a setting named '" + name + "'!");
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
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

}  // namespace Download
}  // namespace Plugin
}  // namespace SmartMet

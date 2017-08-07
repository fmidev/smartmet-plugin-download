// ======================================================================
/*!
 * \brief Implementation of Config
 */
// ======================================================================

#include "Config.h"
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <spine/Exception.h>
#include <stdexcept>

using namespace std;

static const char* defaultTempDirectory = "/tmp";

namespace SmartMet
{
namespace Plugin
{
namespace Download
{
// ----------------------------------------------------------------------
/*!
 * \brief Parse a named producer setting
 */
// ----------------------------------------------------------------------

void Config::parseConfigProducer(const string& name, Producer& currentSettings)
{
  try
  {
    Producer prod;
    string optName = "producers." + name;

    if (itsConfig.exists(optName))
    {
      libconfig::Setting& settings = itsConfig.lookup(optName);

      if (!settings.isGroup())
        throw Spine::Exception(
            BCP,
            "Producer settings for dls must be stored in groups delimited by {}: line " +
                boost::lexical_cast<std::string>(settings.getSourceLine()));

      for (int i = 0; i < settings.getLength(); ++i)
      {
        string paramName = settings[i].getName();

        try
        {
          if ((paramName == "disabledReqParameters") || (paramName == "disabledDataParameters"))
          {
            libconfig::Setting& setting = settings[i];

            if (!setting.isArray())
              throw Spine::Exception(BCP,
                                     optName + "." + paramName +
                                         " must be an array in dls configuration file line " +
                                         boost::lexical_cast<string>(setting.getSourceLine()));

            if (paramName == "disabledReqParameters")
            {
              currentSettings.disabledReqParams.clear();

              for (int j = 0; j < setting.getLength(); ++j)
              {
                string param = setting[j];
                boost::trim(param);

                currentSettings.disabledReqParams.insert(param);
              }
            }
            else
            {
              currentSettings.disabledDataParams.clear();

              for (int j = 0; i < setting.getLength(); ++j)
              {
                int param = setting[j];
                currentSettings.disabledDataParams.insert(param);
              }
            }
          }
          else if (
              (paramName ==
               "grib") /* || (paramName == "grib1") || (paramName == "grib2") || (paramName == "netcdf") */)
          {
            optName += ("." + paramName);
            libconfig::Setting& formatSettings = itsConfig.lookup(optName);

            if (!formatSettings.isGroup())
              throw Spine::Exception(
                  BCP,
                  optName + " must be an array in dls configuration file line " +
                      boost::lexical_cast<string>(formatSettings.getSourceLine()));

            for (int j = 0; j < formatSettings.getLength(); ++j)
              currentSettings.namedSettings.insert(
                  NamedSettings::value_type(formatSettings[j].getName(), formatSettings[j]));
          }
          else if (paramName == "verticalInterpolation")
          {
            currentSettings.verticalInterpolation = settings[i];
          }
          else if (paramName == "datum")
          {
            if (!Plugin::Download::Datum::parseDatumShift(settings[i], currentSettings.datumShift))
              throw Spine::Exception(BCP,
                                     "Invalid datum in dls configuration file line " +
                                         boost::lexical_cast<string>(settings.getSourceLine()));
          }
          else
          {
            throw Spine::Exception(BCP,
                                   string("Unrecognized parameter '") + paramName +
                                       "' in dls configuration on line " +
                                       boost::lexical_cast<string>(settings[i].getSourceLine()));
          }
        }
        catch (libconfig::ParseException& e)
        {
          throw Spine::Exception(BCP,
                                 string("DLS configuration error ' ") + e.getError() +
                                     "' with variable '" + paramName + "' on line " +
                                     boost::lexical_cast<string>(e.getLine()));
        }
        catch (libconfig::ConfigException&)
        {
          throw Spine::Exception(BCP,
                                 string("DLS configuration error with variable '") + paramName +
                                     "' on line " +
                                     boost::lexical_cast<string>(settings[i].getSourceLine()));
        }
        catch (exception& e)
        {
          throw Spine::Exception(BCP,
                                 e.what() + string(" (line number ") +
                                     boost::lexical_cast<string>(settings[i].getSourceLine()) +
                                     ")");
        }
      }
    }

    prod.disabledReqParams.insert(currentSettings.disabledReqParams.begin(),
                                  currentSettings.disabledReqParams.end());
    prod.disabledDataParams.insert(currentSettings.disabledDataParams.begin(),
                                   currentSettings.disabledDataParams.end());
    prod.namedSettings.insert(currentSettings.namedSettings.begin(),
                              currentSettings.namedSettings.end());
    prod.verticalInterpolation = currentSettings.verticalInterpolation;
    prod.datumShift = currentSettings.datumShift;

    itsProducers.insert(Producers::value_type(name, prod));
  }
  catch (...)
  {
    throw Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Parse and push environment settings
 */
// ----------------------------------------------------------------------

void Config::setEnvSettings()
{
  try
  {
    const string env("environment");

    if (itsConfig.exists(env))
    {
      libconfig::Setting& settings = itsConfig.lookup(env);
      int i = 0;

      try
      {
        if (!settings.isGroup())
          throw Spine::Exception(BCP,
                                 env + " must be an array in dls configuration file om line " +
                                     boost::lexical_cast<string>(settings.getSourceLine()));

        for (; i < settings.getLength(); ++i)
        {
          string var(settings[i].getName()), val = settings[i];

          boost::trim(val);
          setenv(var.c_str(), val.c_str(), 1);
        }
      }
      catch (libconfig::ParseException& e)
      {
        throw Spine::Exception(BCP,
                               string("DLS configuration error ' ") + e.getError() +
                                   "' with variable '" + env + "' on line " +
                                   boost::lexical_cast<string>(e.getLine()));
      }
      catch (libconfig::ConfigException&)
      {
        throw Spine::Exception(BCP,
                               string("DLS configuration error with variable '") + env +
                                   "' on line " +
                                   boost::lexical_cast<string>(settings[i].getSourceLine()));
      }
      catch (exception& e)
      {
        throw Spine::Exception(BCP,
                               e.what() + string(" (line number ") +
                                   boost::lexical_cast<string>(settings[i].getSourceLine()) + ")");
      }
    }
  }
  catch (...)
  {
    throw Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Parse producers listed in producers.enabled
 */
// ----------------------------------------------------------------------

void Config::parseConfigProducers(const Engine::Querydata::Engine& querydata)
{
  try
  {
    // Available producers; if not specified, all producers available in querydata

    if (!itsConfig.exists("producers"))
      itsConfig.getRoot().add("producers", libconfig::Setting::TypeGroup);
    else
    {
      libconfig::Setting& producers = itsConfig.lookup("producers");

      if (!producers.isGroup())
        throw Spine::Exception(BCP,
                               "producers must be a group in dls configuration file line " +
                                   boost::lexical_cast<string>(producers.getSourceLine()));
    }

    if (!itsConfig.exists("producers.enabled"))
    {
      // Get all querydata's producers
      //
      const Engine::Querydata::ProducerList& prodList = querydata.producers();
      auto prBeg = prodList.begin(), prEnd = prodList.end();
      auto& enabled = itsConfig.lookup("producers").add("enabled", libconfig::Setting::TypeArray);

      int i = 0;
      for (auto it_Prod = prBeg; (it_Prod != prEnd); it_Prod++, i++)
      {
        enabled.add(libconfig::Setting::TypeString);
        enabled[i] = it_Prod->c_str();
      }
    }

    libconfig::Setting& enabled = itsConfig.lookup("producers.enabled");

    if (!enabled.isArray())
      throw Spine::Exception(BCP,
                             "producers.enabled must be an array in dls configuration file line " +
                                 boost::lexical_cast<string>(enabled.getSourceLine()));

    // Default producer; if not set, using the first producer

    std::string defaultProducer;
    itsConfig.lookupValue("defaultproducer", defaultProducer);
    boost::trim(defaultProducer);

    // Disabled request and data parameters, named (grib) settings (key = value), vertical
    // interpolation state and
    // datum used until overridden by producer specific settings.

    Producer currentSettings;
    currentSettings.verticalInterpolation = false;
    currentSettings.datumShift = Plugin::Download::Datum::None;

    itsConfig.lookupValue("verticalinterpolation", currentSettings.verticalInterpolation);

    for (int i = 0; i < enabled.getLength(); ++i)
    {
      const char* name = enabled[i];
      parseConfigProducer(name, currentSettings);

      if ((i == 0) && defaultProducer.empty())
        defaultProducer = name;
    }

    if (itsProducers.empty())
      throw Spine::Exception(BCP, "No producers defined/enabled: datablock!");

    // Check the default producer exists

    itsDefaultProducer = itsProducers.find(defaultProducer);

    if (itsDefaultProducer == itsProducers.end())
      throw Spine::Exception(
          BCP, "Default producer '" + defaultProducer + "' not enabled in dls producers!");

    // Set given variables to environment

    setEnvSettings();
  }
  catch (...)
  {
    throw Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Constructor
 */
// ----------------------------------------------------------------------

Config::Config(const string& configfile)
    : itsGribPTable(),
      itsNetCdfPTable(),
      itsTempDirectory(defaultTempDirectory),
      itsGrib2TablesVersionMin(0),
      itsGrib2TablesVersionMax(0)
{
  try
  {
    if (configfile.empty())
      throw Spine::Exception(BCP, "DLS configuration file name is empty!");

    itsConfig.readFile(configfile.c_str());

    itsConfig.lookupValue("gribconfig", itsGribConfig);
    if (!itsGribConfig.empty())
    {
      if (itsGribConfig[0] != '/')
      {
        boost::filesystem::path p(configfile);
        itsGribConfig = p.parent_path().string() + "/" + itsGribConfig;
      }
      itsGribPTable = readParamConfig(itsGribConfig);
    }

    itsConfig.lookupValue("netcdfconfig", itsNetCdfConfig);
    if (!itsNetCdfConfig.empty())
    {
      if (itsNetCdfConfig[0] != '/')
      {
        boost::filesystem::path p(configfile);
        itsNetCdfConfig = p.parent_path().string() + "/" + itsNetCdfConfig;
      }
      itsNetCdfPTable = readParamConfig(itsNetCdfConfig, false);
    }

    itsConfig.lookupValue("tempdirectory", itsTempDirectory);

    bool hasMin = itsConfig.exists("grib2.tablesversion.min"),
         hasMax = itsConfig.exists("grib2.tablesversion.max");

    if (hasMin != hasMax)
      throw Spine::Exception(BCP,
                             "Neither or both grib2.tablesversion.min and "
                             "grib2.tablesversion.max must be given in DLS "
                             "configuration");

    if (hasMin)
    {
      itsConfig.lookupValue("grib2.tablesversion.min", itsGrib2TablesVersionMin);
      itsConfig.lookupValue("grib2.tablesversion.max", itsGrib2TablesVersionMax);

      if (itsGrib2TablesVersionMin > itsGrib2TablesVersionMax)
        throw Spine::Exception(
            BCP,
            "Invalid DLS configuration: grib2.tablesversion.min must be less than or equal to "
            "grib2.tablesversion.max");
    }

    // GRIB packing settings

    itsPackingWarningMessage = "Selected packing type is not enabled in this server.";
    itsPackingErrorMessage = "Selected packing type is disabled in this server.";

    if (itsConfig.exists("packing"))
    {
      // Override error messages

      itsConfig.lookupValue("packing.warning", itsPackingWarningMessage);
      itsConfig.lookupValue("packing.error", itsPackingErrorMessage);

      // Explicitly allowed packing types

      if (itsConfig.exists("packing.enabled"))
      {
        libconfig::Setting& enabled = itsConfig.lookup("packing.enabled");
        if (!enabled.isArray())
          throw Spine::Exception(BCP, "packing.enabled must be an array");

        if (enabled.getLength() == 0)
          throw Spine::Exception(BCP, "packing.enabled must not be an empty array");

        for (auto i = 0; i < enabled.getLength(); ++i)
          itsEnabledPackingTypes.insert(enabled[i]);
      }

      // Explicitly disabled packing types

      if (itsConfig.exists("packing.disabled"))
      {
        libconfig::Setting& disabled = itsConfig.lookup("packing.disabled");
        if (!disabled.isArray())
          throw Spine::Exception(BCP, "packing.disabled must be an array");

        for (auto i = 0; i < disabled.getLength(); ++i)
          itsDisabledPackingTypes.insert(disabled[i]);
      }
    }
  }
  catch (...)
  {
    throw Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Initialize the configuration (this waits for engines, so it must
 * be run in the plugin init-method).
 */
// ----------------------------------------------------------------------

void Config::init(Engine::Querydata::Engine* querydata)
{
  try
  {
    parseConfigProducers(*querydata);
  }
  catch (...)
  {
    throw Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Get the producer settings for the given name
 */
// ----------------------------------------------------------------------

const Producer& Config::getProducer(const string& name) const
{
  try
  {
    Producers::const_iterator p = itsProducers.find(name);
    if (p != itsProducers.end())
      return p->second;

    // Unnamed producer "mathces" at this point; the producer will be searched against
    // querydata's configuration

    if (defaultProducerName() == "")
      return itsProducers.begin()->second;

    throw Spine::Exception(BCP, "Unknown producer: " + name);
  }
  catch (...)
  {
    throw Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

const Producer& Config::getProducer(string& name, const Engine::Querydata::Engine& querydata)
{
  try
  {
    bool noDefaultProducer = false;

    Producers::iterator p = (name.empty() ? itsProducers.end() : itsProducers.find(name));
    if ((p == itsProducers.end()) && (noDefaultProducer = (defaultProducerName() == "")))
      // Unnamed producer "mathces" at this point; the producer will be searched against
      // querydata's configuration
      //
      p = itsProducers.begin();

    if (p != itsProducers.end())
    {
      if (!(p->second.qEngineProducerConfig))
      {
        if (name.empty())
        {
          // Using the first producer
          //
          const Engine::Querydata::ProducerList& prodlist = querydata.producers();

          if (!prodlist.empty())
            name = *prodlist.begin();
        }

        p->second.qEngineProducerConfig = querydata.getProducerConfig(name);
      }

      return p->second;
    }

    throw Spine::Exception(BCP, "Unknown producer: " + name);
  }
  catch (...)
  {
    throw Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Returns non-empty error message if the given packing type is not allowed
 */
// ----------------------------------------------------------------------

std::string Config::packingErrorMessage(const std::string& thePackingType) const
{
  // Disabling overrides enabling
  if (itsDisabledPackingTypes.find(thePackingType) != itsDisabledPackingTypes.end())
    return itsPackingErrorMessage;

  // Not having allowed types enables all but explicitly disabled types
  if (itsEnabledPackingTypes.empty())
    return {};

  // Must be one of the explicity enabled then
  if (itsEnabledPackingTypes.find(thePackingType) == itsEnabledPackingTypes.end())
    return itsPackingWarningMessage;

  return {};
}

}  // namespace Download
}  // namespace Plugin
}  // namespace SmartMet

// ======================================================================

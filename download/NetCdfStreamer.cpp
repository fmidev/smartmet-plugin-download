// ======================================================================
/*!
 * \brief SmartMet download service plugin; netcdf streaming
 */
// ======================================================================

#include "NetCdfStreamer.h"
#include <macgyver/DateTime.h>
#include <boost/format.hpp>
#include <gis/ProjInfo.h>
#include <macgyver/Exception.h>
#include <macgyver/Join.h>
#include <macgyver/StringConversion.h>
#include <newbase/NFmiMetTime.h>
#include <newbase/NFmiQueryData.h>
#include <spine/Thread.h>

namespace
{
// NcFile::Open does not seem to be thread safe
SmartMet::Spine::MutexType myFileOpenMutex;
}  // namespace

#define CHECK(x, message) try { x; } catch (...) { throw Fmi::Exception(BCP, message); }


using namespace std;
using namespace netCDF;

namespace SmartMet
{
namespace Plugin
{
namespace Download
{

NetCdfStreamer::NetCdfStreamer(const Spine::HTTP::Request &req,
                               const Config &config,
                               const Query &query,
                               const Producer &producer,
                               const ReqParams &reqParams)
    : DataStreamer(req, config, query, producer, reqParams),
      itsFilename(config.getTempDirectory() + "/dls_" + boost::lexical_cast<string>((int)getpid()) +
                  "_" + boost::lexical_cast<string>(boost::this_thread::get_id())),
      itsLoadedFlag(false)
{
}

NetCdfStreamer::~NetCdfStreamer()
{
  if (itsStream.is_open())
    itsStream.close();

  unlink(itsFilename.c_str());
}

void NetCdfStreamer::requireNcFile()
try
{
  // Require a started NetCDF file

  if (itsFile)
    return;

  itsFile.reset(new netCDF::NcFile(
      itsFilename,
      netCDF::NcFile::replace,
      netCDF::NcFile::classic64));
}
catch (...)
{
  throw Fmi::Exception(BCP, "Operation failed!");
}

// ----------------------------------------------------------------------
/*!
 * \brief Get next chunk of data. Called from SmartMet server code
 *
 */
// ----------------------------------------------------------------------

std::string NetCdfStreamer::getChunk()
{
  try
  {
    try
    {
      string chunk;

      if (!itsDoneFlag)
      {
        if (!itsLoadedFlag)
        {
          // The data is first loaded into a netcdf file (memory mapped filesystem assumed).
          //
          // Note: the data is loaded from 'itsGridValues'; 'chunk' serves only as 'end of data'
          // indicator.
          //
          do
          {
            extractData(chunk);

            if (chunk.empty())
              itsLoadedFlag = true;
            else
              storeParamValues();
          } while (!itsLoadedFlag);

          // Then outputting the file/data in chunks
          //
          // Unset NcFile pointer has caused crashes when calling close(); (possible) reason is
          // that when query initialization (hasRequestedData(), checking atleast some requested
          // data is available) has succeeded and then 1'st call to extractData has for any reason
          // returned an empty result (denoting end of data), NcFile object has not been created.
          //
          // Possible empty query result for 1'st grid is now checked by query initialization and
          // "no data" exception is thrown if nothing was returned.

          if (!itsFile)
            throw Fmi::Exception(BCP, "Netcdf file object is unset");

          itsFile->close();

          itsStream.open(itsFilename, ifstream::in | ifstream::binary);

          if (!itsStream)
            throw Fmi::Exception(BCP, "Unable to open file stream");
        }

        if (!itsStream.eof())
        {
          std::unique_ptr<char[]> mesg(new char[itsChunkLength]);

          itsStream.read(mesg.get(), itsChunkLength);
          streamsize mesg_len = itsStream.gcount();

          if (mesg_len > 0)
            chunk = string(mesg.get(), mesg_len);
        }

        if (chunk.empty())
          itsDoneFlag = true;
      }

      if (itsDoneFlag)
        setStatus(ContentStreamer::StreamerStatus::EXIT_OK);

      return chunk;
    }
    catch (...)
    {
      Fmi::Exception exception(BCP, "Request processing exception!", nullptr);
      exception.addParameter("URI", itsRequest.getURI());

      std::cerr << exception.getStackTrace();
    }

    setStatus(ContentStreamer::StreamerStatus::EXIT_ERROR);

    itsDoneFlag = true;
    return "";
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// void dimDeleter(const netCDF::NcDim& /* dim */) {}

// ----------------------------------------------------------------------
/*!
 * \brief Add dimension.
 *
 *    Note: Dimensions are owned by the netcdf file object
 */
// ----------------------------------------------------------------------

netCDF::NcDim NetCdfStreamer::addDimension(const string &dimName, long dimSize)
{
  try
  {
    return itsFile->addDim(dimName, dimSize);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// void varDeleter(NcVar * /* var */) {}

// ----------------------------------------------------------------------
/*!
 * \brief Add variable
 *
 *    Note: Variables are owned by the netcdf file object
 */
// ----------------------------------------------------------------------

netCDF::NcVar NetCdfStreamer::addVariable(const string &varName,
                                          netCDF::NcType dataType,
                                          const netCDF::NcDim& dim1,
                                          const netCDF::NcDim& dim2,
                                          const netCDF::NcDim& dim3,
                                          const netCDF::NcDim& dim4,
                                          const netCDF::NcDim& dim5)
{
  try
  {
    std::vector<netCDF::NcDim> dims;
    if (not dim1.isNull()) dims.push_back(dim1);
    if (not dim2.isNull()) dims.push_back(dim2);
    if (not dim3.isNull()) dims.push_back(dim3);
    if (not dim4.isNull()) dims.push_back(dim4);
    if (not dim5.isNull()) dims.push_back(dim5);

    auto var = itsFile->addVar(varName, dataType, dims);
    return var;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Add coordinate variable
 *
 */
// ----------------------------------------------------------------------

netCDF::NcVar NetCdfStreamer::addCoordVariable(const string &dimName,
                                               long dimSize,
                                               netCDF::NcType dataType,
                                               string stdName,
                                               string unit,
                                               string axisType,
                                               netCDF::NcDim &dim)
{
  try
  {
    dim = addDimension(dimName, dimSize);

    auto var = addVariable(dimName, dataType, dim);
    var.putAtt("standard_name", stdName);
    var.putAtt("units", unit);

    if (!axisType.empty())
      var.putAtt("axis", axisType);

    return var;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

#if 0
// ----------------------------------------------------------------------
/*!
 * \brief Add attribute
 *
 */
// ----------------------------------------------------------------------
template <typename T1, typename T2>
void NetCdfStreamer::addAttribute(T1 resource, const std::string& attrName,const T2& attrValue)
{
  try
  {
    if constexpr (std::is_same<T2, char *>::value)
    {
      resource.putAtt(attrName, std::string(attrValue));
    }
    else if constexpr (std::is_same<T2, std::string>::value)
    {
      resource.putAtt(attrName, attrValue);
    }
    else
    {
      resource.putAtt(attrName, getNcType<T2>(), attrValue);
    }
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

template <typename T1, typename T2>
void NetCdfStreamer::addAttribute(T1 resource, const std::string& attrName, int nValues, const T2 *attrValues)
{
  try
  {
    if constexpr (std::is_same<T2, char *>::value)
    {
      std::vector<std::string> strValues;
      strValues.reserve(nValues);
      for (int i = 0; i < nValues; i++)
      {
        strValues.push_back(attrValues[i]);
      }
      resource.putAtt(attrName.c_str(), nValues, strValues.data());
    }
    else if constexpr (std::is_same<T2, std::string>::value)
    {
      resource.putAtt(attrName.c_str(), nValues, attrValues);
    }
    else
    {
      resource.putAtt(attrName.c_str(), getNcType<T2>(), nValues, attrValues);
    }
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}
#endif

// ----------------------------------------------------------------------
/*!
 * \brief Get time offset as number of timesteps of given length
 *
 */
// ----------------------------------------------------------------------

int getTimeOffset(const Fmi::DateTime &t1, const Fmi::DateTime t2, long timeStep)
{
  try
  {
    if (timeStep < DataStreamer::minutesInDay)
    {
      auto td = t1 - t2;

      if ((timeStep < 60) || (timeStep % 60))
        return (td.hours() * 60) + td.minutes();

      return ((td.hours() * 60) + td.minutes()) / 60;
    }
    else if (timeStep == DataStreamer::minutesInDay)
    {
      auto dd = t1.date() - t2.date();
      return dd;
    }
    else if (timeStep == DataStreamer::minutesInMonth)
    {
      Fmi::Date d1(t1.date());
      Fmi::Date d2(t2.date());
      return (12 * (d1.year() - d2.year())) + (d1.month() - d2.month());
    }
    else if (timeStep == DataStreamer::minutesInYear)
    {
      return t1.date().year() - t2.date().year();
    }

    throw Fmi::Exception(BCP, "Invalid time step length " + boost::lexical_cast<string>(timeStep));
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Add time dimension
 *
 */
// ----------------------------------------------------------------------

void NetCdfStreamer::addTimeDimension()
{
  try
  {
    // Determine time unit.
    //
    // Supported units are minutes, hours, days, months and years ('common_years').
    //
    // Note: NetCDF Climate and Forecast (CF) Metadata Conventions (Version 1.6, 5 December, 2011):
    //
    // 		 "We recommend that the unit year be used with caution. The Udunits package defines
    // a
    // year to be exactly
    //		  365.242198781 days (the interval between 2 successive passages of the sun through
    // vernal equinox).
    //		  It is not a calendar year. Udunits includes the following definitions for years: a
    // common_year is 365 days,
    // 		  a leap_year is 366 days, a Julian_year is 365.25 days, and a Gregorian_year is
    // 365.2425
    // days.
    //		  For similar reasons the unit month, which is defined in udunits.dat to be exactly
    // year/12, should also be used with caution."

    string timeUnit;
    long timeStep = ((itsReqParams.timeStep > 0) ? itsReqParams.timeStep : itsDataTimeStep);

    if ((timeStep == 60) || ((timeStep > 0) && (timeStep < minutesInDay) && ((timeStep % 60) == 0)))
      timeUnit = "hours";
    else if (timeStep == minutesInDay)
      timeUnit = "days";
    else if (timeStep == minutesInMonth)
      timeUnit = "months";
    else if (timeStep == minutesInYear)
      timeUnit = "common_years";
    else if ((timeStep > 0) && (timeStep < minutesInDay))
    {
      timeUnit = "minutes";
      timeStep = 1;
    }
    else
      throw Fmi::Exception(BCP,
                           "Invalid data timestep " + boost::lexical_cast<string>(timeStep) +
                               " for producer '" + itsReqParams.producer + "'");

    TimeSeries::TimeSeriesGenerator::LocalTimeList::const_iterator timeIter = itsDataTimes.begin();
    Fmi::DateTime startTime = itsDataTimes.front().utc_time();
    size_t timeSize = 0;
    int times[itsDataTimes.size()];

    for (; (timeIter != itsDataTimes.end()); timeIter++, timeSize++)
    {
      long period = getTimeOffset(timeIter->utc_time(), startTime, timeStep);

      if ((timeSize > 0) && (times[timeSize - 1] >= period))
        throw Fmi::Exception(BCP,
                             "Invalid time offset " + boost::lexical_cast<string>(period) + "/" +
                                 boost::lexical_cast<string>(times[timeSize - 1]) + " (validtime " +
                                 Fmi::to_iso_string(timeIter->utc_time()) + " timestep " +
                                 boost::lexical_cast<string>(timeStep) + ") for producer '" +
                                 itsReqParams.producer + "'");

      times[timeSize] = period;
    }

    const Fmi::Date d = startTime.date();
    auto gm = d.month();
    Fmi::TimeDuration td(startTime.time_of_day());

    ostringstream os;

    os << d.year() << "-" << boost::format("%02d-%02d") % gm % d.day()
       << boost::format(" %02d:%02d:%02d") % td.hours() % td.minutes() % td.seconds();

    string timeUnitDef = timeUnit + " since " + os.str();

    itsTimeDim = addDimension("time", timeSize);

    itsTimeVar = addVariable("time", ncInt, itsTimeDim);
    itsTimeVar.putAtt("long_name", "time");
    itsTimeVar.putAtt("calendar", "gregorian");
    itsTimeVar.putAtt("units", timeUnitDef);

    try
    {
      itsTimeVar.putVar(times);
    }
    catch(const std::exception& e)
    {
      throw Fmi::Exception(BCP, "Failed to store validtimes");
    }
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Get time period name
 *
 */
// ----------------------------------------------------------------------

string getPeriodName(long periodLengthInMinutes)
{
  try
  {
    if (periodLengthInMinutes < 60)
      return boost::lexical_cast<string>(periodLengthInMinutes) + "min";
    else if (periodLengthInMinutes == 60)
      return "h";
    else if ((periodLengthInMinutes < DataStreamer::minutesInDay) &&
             ((DataStreamer::minutesInDay % periodLengthInMinutes) == 0))
      return boost::lexical_cast<string>(periodLengthInMinutes / 60) + "h";
    else if (periodLengthInMinutes == DataStreamer::minutesInDay)
      return "d";
    else if (periodLengthInMinutes == DataStreamer::minutesInMonth)
      return "mon";
    else if (periodLengthInMinutes == DataStreamer::minutesInYear)
      return "y";

    return boost::lexical_cast<string>(periodLengthInMinutes);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Get ensemble dimension name
 *
 */
// ----------------------------------------------------------------------

string NetCdfStreamer::getEnsembleDimensionName(
    T::ForecastType forecastType, T::ForecastNumber forecastNumber) const
{
  try
  {
    if (!isEnsembleForecast(forecastType))
      return "";

    return "ensemble_" + Fmi::to_string(forecastType) + "_" + Fmi::to_string(forecastNumber);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Get ensemble dimension by ensemble type and number
 *
 */
// ----------------------------------------------------------------------

NcDim NetCdfStreamer::getEnsembleDimension(
    T::ForecastType forecastType, T::ForecastNumber forecastNumber,
    string &ensembleDimensionName) const
{
  try
  {
    ensembleDimensionName = getEnsembleDimensionName(forecastType, forecastNumber);
    if (ensembleDimensionName.empty())
      return NcDim();

    return itsFile->getDim(ensembleDimensionName);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

NcDim NetCdfStreamer::getEnsembleDimension(
    T::ForecastType forecastType, T::ForecastNumber forecastNumber) const
{
  try
  {
    string ensembleDimensionName;
    return getEnsembleDimension(forecastType, forecastNumber, ensembleDimensionName);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Add ensemble dimensions
 *
 */
// ----------------------------------------------------------------------

void NetCdfStreamer::addEnsembleDimensions()
{
  try
  {
    vector<string> paramParts;
    string ensembleDimName;

    for (auto it = itsDataParams.begin(); (it != itsDataParams.end()); it++)
    {
      // Create ensemble dimension when applicable
      //
      // Take forecast type/number from radon parameter names, e.g. T-K:MEPS:1093:2:92500:3:3

      itsQuery.parseRadonParameterName(it->name(), paramParts);
      auto forecastType = getForecastType(it->name(), paramParts);
      auto forecastNumber = getForecastNumber(it->name(), paramParts);

      // Ensemble dimension might already be created or is not created at all for given parameter

      if (
          getEnsembleDimension(forecastType, forecastNumber, ensembleDimName).isNull() ||
          ensembleDimName.empty()
         )
        continue;

      auto ensembleVar =
          addCoordVariable(ensembleDimName, 1, ncShort, "ensemble", "", "Ensemble", itsEnsembleDim);
//        addCoordVariable(ensembleDimName, 1, ncShort, "realization", "", "E", itsEnsembleDim);

      ensembleVar.putAtt("long_name", "Ensemble member");

      short forecastNumberDimValue = forecastNumber;
      ensembleVar.putVar(&forecastNumberDimValue);
    }
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Add ensemble dimension
 *
 */
// ----------------------------------------------------------------------

void NetCdfStreamer::addEnsembleDimension()
{
  try
  {
    if (itsReqParams.dataSource == GridContent)
    {
      addEnsembleDimensions();
      return;
    }

    // Create dimension if ensemble is applicable

    if (itsGridMetaData.forecastType < 0)
      return;

    auto ensembleVar =
        addCoordVariable("ensemble", 1, ncShort, "ensemble", "", "Ensemble", itsEnsembleDim);
//      addCoordVariable("ensemble", 1, ncShort, "realization", "", "E", itsEnsembleDim);

    ensembleVar.putAtt("long_name", "Ensemble member");

    short forecastNumberDimValue = itsGridMetaData.forecastNumber;
    ensembleVar.putVar(&forecastNumberDimValue);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Add time period length specific time dimension
 *		by copying the 'time' dimension
 */
// ----------------------------------------------------------------------

NcDim NetCdfStreamer::addTimeDimension(long periodLengthInMinutes,
                                       NcVar &tVar)
{
  try
  {
    string name("time_" + getPeriodName(periodLengthInMinutes));

    auto tDim = addDimension(name, itsTimeDim.getSize());
    tVar = addVariable(name, ncInt, tDim);

    int times[itsTimeDim.getSize()];
    itsTimeVar.getVar(times);

    tVar.putVar(times);
      throw Fmi::Exception(BCP, "Failed to store validtimes");

    tVar.putAtt("long_name", "time");
    tVar.putAtt("calendar", "gregorian");

    NcVarAtt uAtt = itsTimeVar.getAtt("units");
    const std::size_t uAttSize = uAtt.getAttLength();
    std::vector<char> uAttBuf(uAttSize);
    uAtt.getValues(uAttBuf.data());

    string unit(uAttBuf.data(), uAttBuf.size());
    tVar.putAtt("units", unit);

    return tDim;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Get level type name, direction of positive values and unit
 *
 */
// ----------------------------------------------------------------------

void NetCdfStreamer::getLevelTypeAttributes(FmiLevelType levelType,
                                            string &name,
                                            string &positive,
                                            string &unit) const
{
  try
  {
    bool gridContent = (itsReqParams.dataSource == GridContent);

    unit.clear();

    if (isPressureLevel(levelType, gridContent))
    {
      name = "pressure";
      positive = "down";
      unit = "hPa";
    }
    else if (isHybridLevel(levelType, gridContent))
    {
      name = "hybrid";
      positive = "up";
    }
    else if (isHeightLevel(levelType, 0, gridContent))
    {
      name = "height";
      positive = "up";
      unit = "m";
    }
    else if (isDepthLevel(levelType, 0, gridContent))
    {
      name = "depth";
      unit = "m";

      if ((! gridContent) && (levelType != itsNativeLevelType))
        // kFmiHeight with negative levels
        //
        positive = "up";
      else
        positive = (itsPositiveLevels ? "down" : "up");
    }
    else
      throw Fmi::Exception(
          BCP, "Unrecognized level type " + boost::lexical_cast<string>(levelType));
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Get level dimension by radon parameter name
 *
 */
// ----------------------------------------------------------------------

namespace
{
string paramNameWithoutLevel(const vector<string> &paramParts)
{
  try
  {
    return paramParts[0] + ":" + paramParts[1] + ":" + paramParts[2] + ":" +
           paramParts[3] + ":" + paramParts[5] + ":" + paramParts[6];
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}
string paramNameWithoutLevel(const Query &query, const string &paramName)
{
  try
  {
    vector<string> paramParts;
    query.parseRadonParameterName(paramName, paramParts);

    return paramNameWithoutLevel(paramParts);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}
}

NcDim NetCdfStreamer::getLevelDimension(
    const string &paramName, string &levelDimName) const
{
  try
  {
    vector<string> paramParts;
    itsQuery.parseRadonParameterName(paramName, paramParts);

    levelDimName.clear();

    auto levelDim = itsLevelDimensions.find(paramNameWithoutLevel(itsQuery, paramName));

    if (levelDim == itsLevelDimensions.end())
      return NcDim();

    levelDimName = levelDim->second.c_str();

    return itsFile->getDim(levelDimName);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Get level dimension and index by radon parameter name and level value
 *
 */
// ----------------------------------------------------------------------

NcDim NetCdfStreamer::getLevelDimAndIndex(
    const string &paramName, int paramLevel, int &levelIndex) const
{
  try
  {
    string levelDimName;

    levelIndex = -1;

    auto levelDim = getLevelDimension(paramName, levelDimName);

    if (levelDim.isNull())
      return levelDim;

    auto it = itsDimensionLevels.find(levelDimName);

    if (it == itsDimensionLevels.end())
      throw Fmi::Exception::Trace(BCP, "Internal error: level dimension not found");

    auto itl = it->second.cbegin();

    for (; (itl != it->second.cend()); itl++)
    {
      levelIndex++;

      if (*itl == paramLevel)
        break;
    }

    if (itl == it->second.cend())
      throw Fmi::Exception::Trace(BCP, "Internal error: level not found");

    return levelDim;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Add level dimensions
 *
 */
// ----------------------------------------------------------------------

void NetCdfStreamer::addLevelDimensions()
{
  try
  {
    // Add level dimension for each unique leveltype/levels combination. Level dimension is not used
    // for ground and entire atmosphere data.
    //
    // Take level types/numbers from radon parameter names, e.g. T-K:MEPS:1093:2:92500:4:0
    //
    // First collect all parameters and levels using leveltype as the top level map key, storing
    // it's parameters and their levels into a map

    typedef map<string, set<int>> ParamLevels;
    typedef pair<string, string> DirectionAndUnit;
    typedef map<string, DirectionAndUnit> DimensionAttributes;
    typedef map<FmiLevelType, ParamLevels> LevelTypeLevels;
    LevelTypeLevels levelTypeLevels;
    DimensionAttributes dimensionAttributes;
    vector<string> paramParts;

    for (auto it = itsDataParams.begin(); (it != itsDataParams.end()); it++)
    {
      itsQuery.parseRadonParameterName(it->name(), paramParts);
      FmiLevelType levelType = (FmiLevelType) getParamLevelId(it->name(), paramParts);

      if (!(
            isPressureLevel(levelType, true) || isHybridLevel(levelType, true) ||
            isHeightLevel(levelType, 0, true) || isDepthLevel(levelType, 0, true)
         ))
        continue;

      auto itlt = levelTypeLevels.find(levelType);
      if (itlt == levelTypeLevels.end())
        itlt = levelTypeLevels.insert(make_pair(levelType, ParamLevels())).first;

      string paramName = paramNameWithoutLevel(paramParts);

      auto itp = itlt->second.find(paramName);
      if (itp == itlt->second.end())
        itp = itlt->second.insert(make_pair(paramName, set<int>())).first;

      itp->second.insert(getParamLevel(it->name(), paramParts));
    }

    // Clear duplicate level sets to use the same dimension variable for the parameters

    string levelTypeName, levelDirectionPositive, varName, unit;
    size_t nDims = 0;

    for (auto itlt = levelTypeLevels.begin(); (itlt != levelTypeLevels.end()); itlt++)
    {
      getLevelTypeAttributes(itlt->first, levelTypeName, levelDirectionPositive, unit);

      for (auto itp = itlt->second.begin(); (itp != itlt->second.end()); itp++)
      {
        if (itp->second.empty())
          continue;

        varName = levelTypeName + "_" + Fmi::to_string(++nDims);

        itsDimensionLevels.insert(make_pair(varName, itp->second));
        dimensionAttributes.insert(make_pair(varName, make_pair(levelDirectionPositive, unit)));
        itsLevelDimensions.insert(make_pair(itp->first, varName));

        for (auto itp2 = next(itp); (itp2 != itlt->second.end()); itp2++)
        {
          if (itp2->second == itp->second)
          {
            itsLevelDimensions.insert(make_pair(itp2->first, varName));
            itp2->second.clear();
          }
        }
      }
    }

    // Add dimensions. Grid pressure levels are Pa, output level is hPa

    for (auto itd = itsDimensionLevels.begin(); (itd != itsDimensionLevels.end()); itd++)
    {
      auto itda = dimensionAttributes.find(itd->first);

      auto levelVar = addCoordVariable(
          itd->first, itd->second.size(), ncFloat, "level", itda->second.second, "Z", itsLevelDim);

      levelVar.putAtt("long_name", itd->first + " levels");
      levelVar.putAtt("positive", itda->second.first);

      float levels[itd->second.size()];
      size_t nLevels = 0;
      bool hPa = (itda->second.second == "hPa");

      for (auto level : itd->second)
        levels[nLevels++] = (hPa ? (level / 100) : level);

      levelVar.putVar(levels);
    }
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Add level dimension
 *
 */
// ----------------------------------------------------------------------

void NetCdfStreamer::addLevelDimension()
{
  try
  {
    if (itsReqParams.dataSource == GridContent)
    {
      addLevelDimensions();
      return;
    }

    if (isSurfaceLevel(itsLevelType))
      return;

    string name, positive, unit;
    getLevelTypeAttributes(itsLevelType, name, positive, unit);

    auto levelVar =
        addCoordVariable(name, itsDataLevels.size(), ncFloat, "level", unit, "Z", itsLevelDim);

    levelVar.putAtt("long_name", (levelVar.getName()) + " level");
    levelVar.putAtt("positive", positive);

    float levels[itsDataLevels.size()];
    int i = 0;

    for (auto level = itsDataLevels.begin(); (level != itsDataLevels.end()); level++, i++)
    {
      levels[i] = (float)*level;
    }

    levelVar.putVar(levels);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Set spheroid and wkt attributes
 *
 */
// ----------------------------------------------------------------------

void NetCdfStreamer::setSpheroidAndWKT(const NcVar &crsVar,
                                       OGRSpatialReference *geometrySRS,
                                       const string &areaWKT)
{
  try
  {
    string srsWKT = (geometrySRS ? getWKT(geometrySRS) : ""), ellipsoid;
    const string &WKT = (geometrySRS ? srsWKT : areaWKT);
    double radiusOrSemiMajor, invFlattening;

    extractSpheroidFromGeom(geometrySRS, areaWKT, ellipsoid, radiusOrSemiMajor, invFlattening);

    if (invFlattening > 0)
    {
      crsVar.putAtt("semi_major", NcType::nc_DOUBLE, radiusOrSemiMajor);
      crsVar.putAtt("inverse_flattening", NcType::nc_DOUBLE, invFlattening);
    }
    else
      crsVar.putAtt("earth_radius", NcType::nc_DOUBLE, radiusOrSemiMajor);

    crsVar.putAtt("crs_wkt", WKT);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Set latlon projection metadata
 *
 */
// ----------------------------------------------------------------------

void NetCdfStreamer::setLatLonGeometry(const NcVar &crsVar)
{
  try
  {
    crsVar.putAtt("grid_mapping_name", "latitude_longitude");
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Set rotated latlon projection metadata
 *
 */
// ----------------------------------------------------------------------

void NetCdfStreamer::setRotatedLatlonGeometry(const NcVar &crsVar)
{
  try
  {
    // Note: grid north pole longitude (0 +) 180 works for longitude 0 atleast

    crsVar.putAtt("grid_mapping_name", "rotated_latitude_longitude");
    crsVar.putAtt("grid_north_pole_latitude", NcType::nc_DOUBLE, 0 - itsGridMetaData.southernPoleLat);
    crsVar.putAtt("grid_north_pole_longitude", NcType::nc_DOUBLE, itsGridMetaData.southernPoleLon + 180);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Set stereographic projection metadata
 *
 */
// ----------------------------------------------------------------------

void NetCdfStreamer::setStereographicGeometry(const NcVar &crsVar,
                                              const NFmiArea *area)
{
  try
  {
    OGRSpatialReference *geometrySRS = itsResources.getGeometrySRS();
    double lon_0, lat_0, lat_ts;

    if ((!geometrySRS) && (!area))
      throw Fmi::Exception(BCP, "Internal error, either SRS or NFmiArea is required");

    if (!geometrySRS)
    {
      auto projInfo = area->SpatialReference().projInfo();

      auto opt_lon_0 = projInfo.getDouble("lon_0");
      auto opt_lat_0 = projInfo.getDouble("lat_0");
      auto opt_lat_ts = projInfo.getDouble("lat_ts");
      lon_0 = (opt_lon_0 ? *opt_lon_0 : 0);
      lat_0 = (opt_lat_0 ? *opt_lat_0 : 90);
      lat_ts = (opt_lat_ts ? *opt_lat_ts : 90);
    }
    else
    {
      lon_0 = getProjParam(*geometrySRS, SRS_PP_CENTRAL_MERIDIAN);
      lat_ts = getProjParam(*geometrySRS, SRS_PP_LATITUDE_OF_ORIGIN);
      lat_0 = (lat_ts > 0) ? 90 : -90;
    }

    crsVar.putAtt("grid_mapping_name", "polar_stereographic");
    crsVar.putAtt("straight_vertical_longitude_from_pole", NcType::nc_DOUBLE, lon_0);
    crsVar.putAtt("latitude_of_projection_origin", NcType::nc_DOUBLE, lat_0);
    crsVar.putAtt("standard_parallel", NcType::nc_DOUBLE, lat_ts);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Set mercator projection metadata
 *
 */
// ----------------------------------------------------------------------

void NetCdfStreamer::setMercatorGeometry(const NcVar &crsVar)
{
  try
  {
    OGRSpatialReference *geometrySRS = itsResources.getGeometrySRS();

    if (!geometrySRS)
      throw Fmi::Exception(BCP, "SRS is not set");

    double lon_0 = getProjParam(*geometrySRS, SRS_PP_CENTRAL_MERIDIAN);

    crsVar.putAtt("grid_mapping_name", "mercator");
    crsVar.putAtt("longitude_of_projection_origin", NcType::nc_DOUBLE, lon_0);

    if (geometrySRS->FindProjParm(SRS_PP_STANDARD_PARALLEL_1) >= 0)
    {
      double lat_ts = getProjParam(*geometrySRS, SRS_PP_STANDARD_PARALLEL_1);
      crsVar.putAtt("standard_parallel", NcType::nc_DOUBLE, lat_ts);
    }
    else
    {
      double scale_factor = getProjParam(*geometrySRS, SRS_PP_SCALE_FACTOR);
      crsVar.putAtt("scale_factor_at_projection_origin", NcType::nc_DOUBLE, scale_factor);
    }
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Set YKJ (transverse mercator) projection metadata
 *
 */
// ----------------------------------------------------------------------

void NetCdfStreamer::setYKJGeometry(const NcVar &crsVar)
{
  try
  {
    const double lon_0 = 27;               // SRS_PP_CENTRAL_MERIDIAN
    const double lat_0 = 0;                // SRS_PP_LATITUDE_OF_ORIGIN
    const double false_easting = 3500000;  // SRS_PP_FALSE_EASTING

    crsVar.putAtt("grid_mapping_name", "transverse_mercator");
    crsVar.putAtt("longitude_of_central_meridian", NcType::nc_DOUBLE, lon_0);
    crsVar.putAtt("latitude_of_projection_origin", NcType::nc_DOUBLE, lat_0);
    crsVar.putAtt("false_easting", NcType::nc_DOUBLE, false_easting);

    setSpheroidAndWKT(crsVar, Fmi::SpatialReference(2393).get());
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Set lcc projection metadata
 *
 */
// ----------------------------------------------------------------------

void NetCdfStreamer::setLambertConformalGeometry(const NcVar &crsVar,
                                                 const NFmiArea *area)
{
  try
  {
    OGRSpatialReference *geometrySRS = itsResources.getGeometrySRS();
    OGRSpatialReference areaSRS;

    if ((!geometrySRS) && (!area))
      throw Fmi::Exception(BCP, "Internal error, either SRS or NFmiArea is required");

    if (!geometrySRS)
    {
      OGRErr err;

      if ((err = areaSRS.importFromWkt(area->WKT().c_str())) != OGRERR_NONE)
        throw Fmi::Exception(BCP,
                               "srs.importFromWKT(" + area->WKT() + ") error " +
                                   boost::lexical_cast<string>(err));
       geometrySRS = &areaSRS;
    }

    auto projection = geometrySRS->GetAttrValue("PROJECTION");
    if (!projection)
      throw Fmi::Exception(BCP, "Geometry PROJECTION not set");

    double lon_0 = getProjParam(*geometrySRS, SRS_PP_CENTRAL_MERIDIAN);
    double lat_0 = getProjParam(*geometrySRS, SRS_PP_LATITUDE_OF_ORIGIN);
    double latin1 = getProjParam(*geometrySRS, SRS_PP_STANDARD_PARALLEL_1);

    crsVar.putAtt("grid_mapping_name", "lambert_conformal_conic");
    crsVar.putAtt("longitude_of_central_meridian", NcType::nc_DOUBLE, lon_0);
    crsVar.putAtt("latitude_of_projection_origin", NcType::nc_DOUBLE, lat_0);

    if (EQUAL(projection, SRS_PT_LAMBERT_CONFORMAL_CONIC_2SP))
    {
      // http://cfconventions.org/Data/cf-conventions/cf-conventions-1.8/cf-conventions.html
      // #table-grid-mapping-attributes
      //
      // .. with the additional convention that the standard parallel nearest the pole
      // (N or S) is provided first

      double latin2 = getProjParam(*geometrySRS, SRS_PP_STANDARD_PARALLEL_2);
      double sp1 = latin1, sp2 = latin2;

      if (((latin1 >= 0) && (latin2 >= 0) && (latin1 < latin2)) ||
          ((latin1 <= 0) && (latin2 <= 0) && (latin1 > latin2)))
      {
        sp1 = latin2;
        sp2 = latin1;
      }

      double sp[] = {sp1, sp2};

      crsVar.putAtt("standard_parallel", NcType::nc_DOUBLE, 2, sp);
    }
    else
      crsVar.putAtt("standard_parallel", NcType::nc_DOUBLE, latin1);

    crsVar.putAtt("grid_mapping_name", "lambert_conformal_conic");
    crsVar.putAtt("longitude_of_central_meridian", NcType::nc_DOUBLE, lon_0);
    crsVar.putAtt("latitude_of_projection_origin", NcType::nc_DOUBLE, lat_0);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Set metadata
 *
 */
// ----------------------------------------------------------------------

void NetCdfStreamer::setGeometry(Engine::Querydata::Q q, const NFmiArea *area, const NFmiGrid *grid)
{
  try
  {
    // Conventions

    itsFile->putAtt("Conventions", "CF-1.6");
    itsFile->putAtt("title", "<title>");
    itsFile->putAtt("institution", "fmi.fi");
    itsFile->putAtt("source", "<producer>");

    // Time dimension

    addTimeDimension();

    // Level dimension

    addLevelDimension();

    // Set projection

    auto crsVar = addVariable("crs", ncShort);

    int classId = (itsReqParams.areaClassId != A_Native)
        ? (int)itsReqParams.areaClassId
        : area->ClassId();

    switch (classId)
    {
      case kNFmiLatLonArea:
        setLatLonGeometry(crsVar);
        break;
      case kNFmiStereographicArea:
        setStereographicGeometry(crsVar, area);
        break;
      case kNFmiYKJArea:
        setYKJGeometry(crsVar);
        break;
      case kNFmiLambertConformalConicArea:
        setLambertConformalGeometry(crsVar, area);
        break;
      default:
        throw Fmi::Exception(BCP, "Unsupported projection in input data");
    }

    // Store y/x and/or lat/lon dimensions and coordinate variables, cropping the grid if manual
    // cropping is set

    bool projected = (classId != kNFmiLatLonArea);

    size_t x0 = (itsCropping.cropped ? itsCropping.bottomLeftX : 0),
           y0 = (itsCropping.cropped ? itsCropping.bottomLeftY : 0);
    size_t xN = (itsCropping.cropped ? (x0 + itsCropping.gridSizeX) : itsReqGridSizeX),
           yN = (itsCropping.cropped ? (y0 + itsCropping.gridSizeY) : itsReqGridSizeY);
    size_t xStep = (itsReqParams.gridStepXY ? (*(itsReqParams.gridStepXY))[0].first : 1),
           yStep = (itsReqParams.gridStepXY ? (*(itsReqParams.gridStepXY))[0].second : 1), x, y, n;
    size_t nLat = (projected ? (itsNY * itsNX) : itsNY),
           nLon = (projected ? (itsNY * itsNX) : itsNX);
    std::unique_ptr<double[]> latPtr(new double[nLat]), lonPtr(new double[nLon]);
    double *lat = latPtr.get(), *lon = lonPtr.get();

    NcVar latVar, lonVar;

    if (!grid)
      grid = &q->grid();

    if (projected)
    {
      // Store y, x and 2d (y,x) lat/lon coordinates.
      //
      // Note: NetCDF Climate and Forecast (CF) Metadata Conventions (Version 1.6, 5 December,
      // 2011):
      //
      // "T(k,j,i) is associated with the coordinate values lon(j,i), lat(j,i), and
      // lev(k). The vertical coordinate is represented by the coordinate variable lev(lev) and
      // the latitude and longitude coordinates are represented by the auxiliary coordinate
      // variables lat(yc,xc) and lon(yc,xc) which are identified by the coordinates attribute.
      //
      // Note that coordinate variables are also defined for the xc and yc dimensions. This
      // faciliates processing of this data by generic applications that don't recognize the
      // multidimensional latitude and longitude coordinates."

      auto yVar =
          addCoordVariable("y", itsNY, ncFloat, "projection_y_coordinate", "m", "Y", itsYDim);
      auto xVar =
          addCoordVariable("x", itsNX, ncFloat, "projection_x_coordinate", "m", "X", itsXDim);

      NFmiPoint p0 =
          ((itsReqParams.datumShift == Datum::DatumShift::None) ? grid->GridToWorldXY(x0, y0)
                                                                : itsTargetWorldXYs(x0, y0));
      NFmiPoint pN = ((itsReqParams.datumShift == Datum::DatumShift::None)
                          ? grid->GridToWorldXY(xN - 1, yN - 1)
                          : itsTargetWorldXYs(xN - 1, yN - 1));

      double worldY[itsNY], worldX[itsNX];
      double wY = p0.Y(), wX = p0.X();
      double stepY = yStep * ((itsNY > 1) ? ((pN.Y() - p0.Y()) / (yN - y0 - 1)) : 0.0);
      double stepX = xStep * ((itsNX > 1) ? ((pN.X() - p0.X()) / (xN - x0 - 1)) : 0.0);

      for (y = 0; (y < itsNY); wY += stepY, y++)
        worldY[y] = wY;
      for (x = 0; (x < itsNX); wX += stepX, x++)
        worldX[x] = wX;

      CHECK(yVar.putVar(worldY), "Failed to store y -coordinates");
      CHECK(xVar.putVar(worldX), "Failed to store x -coordinates");

      latVar = addVariable("lat", ncFloat, itsYDim, itsXDim);
      lonVar = addVariable("lon", ncFloat, itsYDim, itsXDim);

      for (y = y0, n = 0; (y < yN); y += yStep)
        for (x = x0; (x < xN); x += xStep, n++)
        {
          const NFmiPoint p =
              ((itsReqParams.datumShift == Datum::DatumShift::None) ? grid->GridToLatLon(x, y)
                                                                    : itsTargetLatLons(x, y));

          lat[n] = p.Y();
          lon[n] = p.X();
        }

      CHECK(latVar.putVar(lat), "Failed to store latitude(y,x) coordinates");
      CHECK(lonVar.putVar(lon), "Failed to store longitude(y,x) coordinates");
    }
    else
    {
      // latlon, grid defined as cartesian product of latitude and longitude axes
      //
      latVar = addCoordVariable("lat", itsNY, ncFloat, "latitude", "degrees_north", "Y", itsLatDim);
      lonVar = addCoordVariable("lon", itsNX, ncFloat, "longitude", "degrees_east", "X", itsLonDim);

      for (y = y0, n = 0; (y < yN); y += yStep, n++)
        lat[n] =
            ((itsReqParams.datumShift == Datum::DatumShift::None) ? grid->GridToLatLon(0, y).Y()
                                                                  : itsTargetLatLons.y(0, y));

      for (x = x0, n = 0; (x < xN); x += xStep, n++)
        lon[n] =
            ((itsReqParams.datumShift == Datum::DatumShift::None) ? grid->GridToLatLon(x, 0).X()
                                                                  : itsTargetLatLons.x(x, 0));

      CHECK(latVar.putVar(lat), "Failed to store latitude coordinates");
      CHECK(lonVar.putVar(lon), "Failed to store longitude coordinates");
    }

    latVar.putAtt("standard_name", "latitude");
    latVar.putAtt("long_name", "latitude");
    latVar.putAtt("units", "degrees_north");
    lonVar.putAtt("standard_name", "longitude");
    lonVar.putAtt("long_name", "longitude");
    lonVar.putAtt("units", "degrees_east");

    // For YKJ spheroid is already set from epsg:2393

    if (classId != kNFmiYKJArea)
      setSpheroidAndWKT(crsVar, itsResources.getGeometrySRS(), area->WKT());
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

void NetCdfStreamer::setGridGeometry(const QueryServer::Query &gridQuery)
{
  try
  {
    // Conventions

    itsFile->putAtt("Conventions", "CF-1.6");
    itsFile->putAtt("title", "<title>");
    itsFile->putAtt("institution", "fmi.fi");
    itsFile->putAtt("source", "<producer>");

    // Ensemble dimension

    addEnsembleDimension();

    // Time dimension

    addTimeDimension();

    // Level dimension

    addLevelDimension();

    // Set projection

    OGRSpatialReference *geometrySRS = itsResources.getGeometrySRS();
    auto crsVar = addVariable("crs", ncShort);

    switch (itsGridMetaData.projType)
    {
      case T::GridProjectionValue::LatLon:
        setLatLonGeometry(crsVar);
        break;
      case T::GridProjectionValue::RotatedLatLon:
        setRotatedLatlonGeometry(crsVar);
        break;
      case T::GridProjectionValue::PolarStereographic:
        setStereographicGeometry(crsVar);
        break;
      case T::GridProjectionValue::Mercator:
        setMercatorGeometry(crsVar);
        break;
      case T::GridProjectionValue::LambertConformal:
        setLambertConformalGeometry(crsVar);
        break;
      default:
        throw Fmi::Exception(BCP, "Unsupported projection in input data");
    }

    // Store y/x and/or lat/lon dimensions and coordinate variables

    bool projected = ((itsGridMetaData.projType != T::GridProjectionValue::LatLon) &&
                      (itsGridMetaData.projType != T::GridProjectionValue::RotatedLatLon));

    size_t x0 = 0, y0 = 0;
    size_t xN = itsReqGridSizeX, yN = itsReqGridSizeY;
    size_t xStep = (itsReqParams.gridStepXY ? (*(itsReqParams.gridStepXY))[0].first : 1),
           yStep = (itsReqParams.gridStepXY ? (*(itsReqParams.gridStepXY))[0].second : 1), x, y, n;
    size_t nLat = (projected ? (itsNY * itsNX) : itsNY),
           nLon = (projected ? (itsNY * itsNX) : itsNX);
    std::unique_ptr<double[]> latPtr(new double[nLat]), lonPtr(new double[nLon]);
    double *lat = latPtr.get(), *lon = lonPtr.get();

    NcVar latVar, lonVar;

    auto coords = gridQuery.mQueryParameterList.front().mCoordinates;

    if (coords.size() != (itsReqGridSizeX * itsReqGridSizeY))
      throw Fmi::Exception(BCP,
                           "Number of coordinates (" + Fmi::to_string(coords.size()) +
                               ") and grid size (" + Fmi::to_string(itsReqGridSizeX) + "/" +
                               Fmi::to_string(itsReqGridSizeY) + ") mismatch");

    if (projected)
    {
      // Store y, x and 2d (y,x) lat/lon coordinates.
      //
      // Note: NetCDF Climate and Forecast (CF) Metadata Conventions (Version 1.6, 5 December,
      // 2011):
      //
      //	 "T(k,j,i) is associated with the coordinate values lon(j,i), lat(j,i), and lev(k).
      //	  The vertical coordinate is represented by the coordinate variable lev(lev) and
      //	  the latitude and longitude coordinates are represented by the auxiliary coordinate
      //	  variables lat(yc,xc) and lon(yc,xc) which are identified by the coordinates
      //	  attribute.
      //
      //	  Note that coordinate variables are also defined for the xc and yc dimensions. This
      //	  faciliates processing of this data by generic applications that don't recognize
      //	  the multidimensional latitude and longitude coordinates."

      OGRSpatialReference llSRS;
      llSRS.CopyGeogCSFrom(geometrySRS);

      OGRCoordinateTransformation *ct =
          itsResources.getCoordinateTransformation(&llSRS, geometrySRS);

      double xc[] = {coords[0].x(), coords[coords.size() - 1].x()};
      double yc[] = {coords[0].y(), coords[coords.size() - 1].y()};
      int pabSuccess[2];

      int status = ct->Transform(2, xc, yc, nullptr, pabSuccess);

      if (!(status && pabSuccess[0] && pabSuccess[1]))
        throw Fmi::Exception(BCP, "Failed to transform llbbox to bbox: " + itsGridMetaData.crs);

      auto yVar =
          addCoordVariable("y", itsNY, ncFloat, "projection_y_coordinate", "m", "Y", itsYDim);
      auto xVar =
          addCoordVariable("x", itsNX, ncFloat, "projection_x_coordinate", "m", "X", itsXDim);

      NFmiPoint p0(xc[0], yc[0]);
      NFmiPoint pN(xc[1], yc[1]);

      double worldY[itsNY], worldX[itsNX];
      double wY = p0.Y(), wX = p0.X();
      double stepY = yStep * ((itsNY > 1) ? ((pN.Y() - p0.Y()) / (yN - y0 - 1)) : 0.0);
      double stepX = xStep * ((itsNX > 1) ? ((pN.X() - p0.X()) / (xN - x0 - 1)) : 0.0);

      for (y = 0; (y < itsNY); wY += stepY, y++)
        worldY[y] = wY;
      for (x = 0; (x < itsNX); wX += stepX, x++)
        worldX[x] = wX;

      CHECK(yVar.putVar(worldY), "Failed to store y -coordinates");
      CHECK(xVar.putVar(worldX), "Failed to store x -coordinates");

      latVar = addVariable("lat", ncFloat, itsYDim, itsXDim);
      lonVar = addVariable("lon", ncFloat, itsYDim, itsXDim);

      latVar.putAtt("standard_name", "latitude");
      latVar.putAtt("units", "degrees_north");
      lonVar.putAtt("standard_name", "longitude");
      lonVar.putAtt("units", "degrees_east");

      for (y = 0, n = 0; (y < yN); y += yStep)
        for (x = 0; (x < xN); x += xStep, n++)
        {
          auto c = (y * xN) + x;
          const NFmiPoint p(coords[c].x(), coords[c].y());

          lat[n] = p.Y();
          lon[n] = p.X();
        }

      CHECK(latVar.putVar(lat), "Failed to store latitude(y,x) coordinates");
      CHECK(lonVar.putVar(lon), "Failed to store longitude(y,x) coordinates");
    }
    else
    {
      // latlon or rotlatlon, grid defined as cartesian product of latitude and longitude axes

      auto latCoord = (itsGridMetaData.projType == T::GridProjectionValue::LatLon)
                          ? "latitude"
                          : "grid_latitude";
      auto latUnit = (itsGridMetaData.projType == T::GridProjectionValue::LatLon) ? "degrees_north"
                                                                                  : "degrees";
      auto lonCoord = (itsGridMetaData.projType == T::GridProjectionValue::LatLon)
                          ? "longitude"
                          : "grid_longitude";
      auto lonUnit =
          (itsGridMetaData.projType == T::GridProjectionValue::LatLon) ? "degrees_east" : "degrees";

      latVar = addCoordVariable("lat", itsNY, ncFloat, latCoord, latUnit, "Lat", itsLatDim);
      lonVar = addCoordVariable("lon", itsNX, ncFloat, lonCoord, lonUnit, "Lon", itsLonDim);

      if (itsGridMetaData.projType == T::GridProjectionValue::LatLon)
      {
        for (y = 0, n = 0; (y < yN); y += yStep, n++)
          lat[n] = coords[y * xN].y();

        for (x = 0, n = 0; (x < xN); x += xStep, n++)
          lon[n] = coords[x].x();
      }
      else
      {
        auto rotLat = itsGridMetaData.rotLatitudes.get();
        auto rotLon = itsGridMetaData.rotLongitudes.get();

        for (y = 0, n = 0; (y < yN); y += yStep, n++)
          lat[n] = rotLat[y * xN];

        for (x = 0, n = 0; (x < xN); x += xStep, n++)
          lon[n] = rotLon[x];
      }

      CHECK(latVar.putVar(lat), "Failed to store latitude coordinates");
      CHECK(lonVar.putVar(lon), "Failed to store longitude coordinates");
    }

    latVar.putAtt("long_name", "latitude");
    lonVar.putAtt("long_name", "longitude");

    setSpheroidAndWKT(crsVar, geometrySRS);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Get time period start time
 *
 */
// ----------------------------------------------------------------------

Fmi::DateTime getPeriodStartTime(const Fmi::DateTime &vt, long periodLengthInMinutes)
{
  try
  {
    Fmi::Date d = vt.date();
    Fmi::TimeDuration td = vt.time_of_day();
    long minutes = (td.hours() * 60) + td.minutes();

    if (((periodLengthInMinutes > 0) && (periodLengthInMinutes < 60) &&
         ((60 % periodLengthInMinutes) == 0)) ||
        (periodLengthInMinutes == 60) ||
        ((periodLengthInMinutes < DataStreamer::minutesInDay) &&
         ((DataStreamer::minutesInDay % periodLengthInMinutes) == 0)))
      if (minutes == 0)
        return Fmi::DateTime(d, Fmi::TimeDuration(0, -periodLengthInMinutes, 0));
      else if ((minutes % periodLengthInMinutes) != 0)
        return Fmi::DateTime(
            d, Fmi::TimeDuration(0, ((minutes / periodLengthInMinutes) * periodLengthInMinutes), 0));
      else
        return Fmi::DateTime(d, Fmi::TimeDuration(0, minutes - periodLengthInMinutes, 0));
    else if (periodLengthInMinutes == DataStreamer::minutesInDay)
      if (minutes == 0)
        return Fmi::DateTime(Fmi::DateTime(d, Fmi::TimeDuration(-1, 0, 0)).date());
      else
        return Fmi::DateTime(d);
    else if (periodLengthInMinutes == DataStreamer::minutesInMonth)
    {
      if ((d.day() == 1) && (minutes == 0))
        d = Fmi::DateTime(d, Fmi::TimeDuration(-1, 0, 0)).date();

      return Fmi::DateTime(Fmi::Date(d.year(), d.month(), 1));
    }
    else if (periodLengthInMinutes == DataStreamer::minutesInYear)
    {
      if ((d.month() == 1) && (d.day() == 1) && (minutes == 0))
        d = Fmi::DateTime(d, Fmi::TimeDuration(-1, 0, 0)).date();

      return Fmi::DateTime(Fmi::Date(d.year(), 1, 1));
    }

    throw Fmi::Exception(
        BCP, "Invalid time period length " + boost::lexical_cast<string>(periodLengthInMinutes));
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Add time bounds for aggregate data.
 *
 */
// ----------------------------------------------------------------------

NcDim NetCdfStreamer::addTimeBounds(long periodLengthInMinutes,
                                                       string &timeDimName)
{
  try
  {
    string pName(getPeriodName(periodLengthInMinutes));

    timeDimName = "time_" + pName;

    NcDim tDim = itsFile->getDim(timeDimName);

    if (tDim.isNull())
      return tDim;

    // Add aggregate period length specific time dimension and variable

    NcVar tVar;
    tDim = addTimeDimension(periodLengthInMinutes, tVar);

    // Add time bounds dimension

    if (itsTimeBoundsDim.isNull())
      itsTimeBoundsDim = addDimension("time_bounds", 2);

    // Determine and store time bounds

    TimeSeries::TimeSeriesGenerator::LocalTimeList::const_iterator timeIter = itsDataTimes.begin();
    Fmi::DateTime startTime = itsDataTimes.front().utc_time(), vt;
    int bounds[2 * itsTimeDim.getSize()];
    size_t i = 0;

    for (; (timeIter != itsDataTimes.end()); timeIter++)
    {
      // Period start time offset
      //
      vt = timeIter->utc_time();

      bounds[i] =
          getTimeOffset(getPeriodStartTime(vt, periodLengthInMinutes), startTime, itsDataTimeStep);
      i++;

      // Validtime's offset

      bounds[i] = getTimeOffset(vt, startTime, itsDataTimeStep);
      i++;
    }

    string name("time_bounds_" + pName);

    auto timeBoundsVar = addVariable(name, ncInt, tDim, itsTimeBoundsDim);

    CHECK(timeBoundsVar.putVar(bounds), "Failed to store time bounds");

    // Connect the bounds to the time variable

    tVar.putAtt("bounds", name);

    return tDim;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Add parameters
 *
 */
// ----------------------------------------------------------------------

bool NetCdfStreamer::hasParamVariable
    (const vector<string> &paramParts, const map<string, NcVar> &paramVariables)
{
  try
  {
    auto it = paramVariables.find(paramNameWithoutLevel(paramParts));

    if (it != paramVariables.end())
    {
      itsDataVars.push_back(it->second);
      return true;
    }

    return false;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

void NetCdfStreamer::addParamVariable(const NcVar& var,
                                      const vector<string> &paramParts,
                                      map<string, NcVar> &paramVariables)
{
  try
  {
    paramVariables.insert(make_pair(paramNameWithoutLevel(paramParts), var));
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

void NetCdfStreamer::addVariables(bool relative_uv)
{
  try
  {
    const ParamChangeTable &pTable = itsCfg.getParamChangeTable(false);
    NcDim &yOrLat = (!itsYDim.isNull() ? itsYDim : itsLatDim);
    NcDim &xOrLon = (!itsYDim.isNull() ? itsXDim : itsLonDim);
    NcDim tDim;
    map<string, NcVar> paramVariables;
    vector<string> paramParts;
    size_t nVars = 0;
    bool gridContent = (itsReqParams.dataSource == GridContent);

    for (auto it = itsDataParams.begin(); (it != itsDataParams.end()); it++)
    {
      // Note: when querying with radon parameter names (when gridContent is true) itsEnsembleDim
      // and/or itsLevelDim is set if any on the parameters has ensemble and/or level dimension
      // (created by addEnsembleDimensions() and addLevelDimensions()); ensemble and/or level
      // dimension may not exist for given parameter.
      //
      // With non-gridContent query all parameters have or have not ensemble and/or level dimension
      // as indicated by itsEnsembleDim and itsLevelDim; ensemble is not used with querydata source
      // (source = QueryData) but can exist when querying grid engine data using grid
      // mappings/details (source = GridMapping).

      auto ensembleDim = itsEnsembleDim;
      auto levelDim = itsLevelDim;

      NFmiParam theParam(it->number());
      string paramName, radonParam, stdName, longName, unit, timeDimName = "time";
      size_t i, j;

      signed long usedParId = theParam.GetIdent();

      if (gridContent)
      {
        // Check if netcdf variable already exists for the parameter

        paramName = it->name();
        itsQuery.parseRadonParameterName(paramName, paramParts);

        if (hasParamVariable(paramParts, paramVariables))
          continue;

        // Get param config index for the parameter

        radonParam = paramParts.front();

        for (i = j = 0; (i < pTable.size()); ++i)
          if (pTable[i].itsRadonName == radonParam)
            break;

        if (i >= pTable.size())
          throw Fmi::Exception(
              BCP, "Internal error: No netcdf configuration for parameter " + radonParam);

        // Get ensemble dimension

        if (!itsEnsembleDim.isNull())
        {
          auto forecastType = getForecastType(paramName, paramParts);
          auto forecastNumber = getForecastNumber(paramName, paramParts);

          ensembleDim = getEnsembleDimension(forecastType, forecastNumber);
        }

        // Get level dimension

        if (!itsLevelDim.isNull())
        {
          string levelDimName;
          levelDim = getLevelDimension(paramName, levelDimName);
        }

        usedParId = ++nVars;
      }
      else
      {
        for (i = j = 0; i < pTable.size(); ++i)
          if (usedParId == pTable[i].itsWantedParam.GetIdent())
          {
            if (relative_uv == (pTable[i].itsGridRelative ? *pTable[i].itsGridRelative : false))
              break;
            else if (j == 0)
              j = i + 1;
            else
              throw Fmi::Exception(BCP,
                                   "Missing gridrelative configuration for parameter " +
                                       boost::lexical_cast<string>(usedParId));
          }
      }

      NcDim dimensions[] = {
          !ensembleDim.isNull() ? ensembleDim : NcDim(),    // Ensemble
          itsTimeDim,                                       // Time dimension
          !levelDim.isNull() ? levelDim : yOrLat,           // Level or Y/lat
          !levelDim.isNull() ? yOrLat : xOrLon,             // Y/lat or X/lon dimension
          !levelDim.isNull() ? xOrLon : NcDim()             // X dimension or n/a
      };

      if ((i >= pTable.size()) && (j > 0))
        i = j - 1;

      if (i < pTable.size())
      {
        paramName = pTable[i].itsWantedParam.GetName();
        stdName = pTable[i].itsStdName;
        longName = pTable[i].itsLongName;
        unit = pTable[i].itsUnit;

        if ((!pTable[i].itsStepType.empty()) || (pTable[i].itsPeriodLengthMinutes > 0))
        {
          // Add aggregate period length specific time dimension and time bounds.
          // Use data period length if aggregate period length is not given.
          //
          tDim = addTimeBounds((pTable[i].itsPeriodLengthMinutes > 0)
                                   ? pTable[i].itsPeriodLengthMinutes
                                   : itsDataTimeStep,
                               timeDimName);
          dimensions[1] = tDim;
        }
      }
      else
        paramName = theParam.GetName();

      const NcDim *dim = dimensions;

      if (ensembleDim.isNull())
        dim++;

      const NcDim& dim1 = *(dim++);
      const NcDim& dim2 = *(dim++);
      const NcDim& dim3 = *(dim++);
      const NcDim& dim4 = *(dim++);
      const NcDim dim5 = (!ensembleDim.isNull() ? *dim : NcDim());

      auto dataVar = addVariable(paramName + "_" + Fmi::to_string(usedParId),
                                 ncFloat,
                                 dim1,
                                 dim2,
                                 dim3,
                                 dim4,
                                 dim5);

      float missingValue =
          (itsReqParams.dataSource == QueryData) ? kFloatMissing : gribMissingValue;

      dataVar.putAtt("units", unit);
      dataVar.putAtt("_FillValue", NcType::nc_FLOAT, missingValue);
      dataVar.putAtt("missing_value", NcType::nc_FLOAT, missingValue);
      dataVar.putAtt("grid_mapping", "crs");

      if (!stdName.empty())
        dataVar.putAtt("standard_name", stdName);

      if (!longName.empty())
        dataVar.putAtt("long_name", longName);

      if ((i < pTable.size()) && (!pTable[i].itsStepType.empty()))
        // Cell method for aggregate data
        //
        dataVar.putAtt("cell_methods", (timeDimName + ": " + pTable[i].itsStepType));

      if (!itsYDim.isNull())
        dataVar.putAtt("coordinates", "lat lon");

      itsDataVars.push_back(dataVar);

      if (gridContent)
        // Store the netcdf variable for the parameter
        //
        addParamVariable(dataVar, paramParts, paramVariables);
    }

    itsVarIterator = itsDataVars.begin();
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Store current parameter's/grid's values.
 *
 */
// ----------------------------------------------------------------------

void NetCdfStreamer::storeParamValues()
{
  try
  {
    // Load scaled values into a continuous buffer, cropping the grid/values if manual cropping is
    // set
    //
    // Note: Using heap because buffer size might exceed stack size

    bool cropxy = (itsCropping.cropped && itsCropping.cropMan);
    size_t x0 = (cropxy ? itsCropping.bottomLeftX : 0), y0 = (cropxy ? itsCropping.bottomLeftY : 0);
    size_t xN = (itsCropping.cropped ? (x0 + itsCropping.gridSizeX) : itsReqGridSizeX),
           yN = (itsCropping.cropped ? (y0 + itsCropping.gridSizeY) : itsReqGridSizeY);
    size_t xStep = (itsReqParams.gridStepXY ? (*(itsReqParams.gridStepXY))[0].first : 1),
           yStep = (itsReqParams.gridStepXY ? (*(itsReqParams.gridStepXY))[0].second : 1), x, y;

    std::shared_ptr<float[]> values(new float[itsNY * itsNX]);

    int i = 0;

    if (itsReqParams.dataSource == QueryData)
    {
      for (y = y0; (y < yN); y += yStep)
        for (x = x0; (x < xN); x += xStep, i++)
        {
          auto value = itsGridValues[x][y];

          if (value != kFloatMissing)
            values[i] = (value + itsScalingIterator->second) / itsScalingIterator->first;
          else
            values[i] = value;
        }
    }
    else
    {
      const auto vVec = &(getValueListItem(itsGridQuery)->mValueVector);
      bool gridContent = (itsReqParams.dataSource == GridContent);

      for (y = y0; (y < yN); y += yStep)
        for (x = x0; (x < xN); x += xStep, i++)
        {
          auto c = (y * xN) + x;
          auto value = (*vVec)[c];

          if (value != ParamValueMissing)
          {
            if (gridContent)
              values[i] = value;
            else
              values[i] = (value + itsScalingIterator->second) / itsScalingIterator->first;
          }
          else
            values[i] = gribMissingValue;
        }
    }

    // Store the values for current parameter/[level/]validtime.
    //
    // First skip variables for missing parameters if any.
    //
    // Note: with querydata timeIndex was incremented after getting the data
    //       and ensemble dimension is not used

    auto timeIndex = itsTimeIndex - ((itsReqParams.dataSource == QueryData) ? 1 : 0);

    if ((itsVarIterator == itsDataVars.begin()) && (itsParamIterator != itsDataParams.begin()))
      for (auto it_p = itsDataParams.begin(); (it_p != itsParamIterator); it_p++)
      {
        itsVarIterator++;

        if (itsVarIterator == itsDataVars.end())
          throw Fmi::Exception(BCP, "storeParamValues: internal: No more netcdf variables");
      }

    // Note: when querying with radon parameter names (when gridContent is true) itsEnsembleDim
    // and/or itsLevelDim is set if any on the parameters has ensemble and/or level dimension
    // (created by addEnsembleDimensions() and addLevelDimensions()); ensemble and/or level
    // dimension may not exist for given parameter.
    //
    // With non-gridContent query all parameters have or have not ensemble and/or level dimension
    // as indicated by itsEnsembleDim and itsLevelDim; ensemble is not used with querydata source
    // (source = QueryData) but can exist when querying grid engine data using grid
    // mappings/details (source = GridMapping).

    auto ensembleDim = itsEnsembleDim;
    auto levelDim = itsLevelDim;
    bool gridContent = (itsReqParams.dataSource == GridContent);
    int levelIndex = itsLevelIndex;

    if (gridContent)
    {
      vector<string> paramParts;
      itsQuery.parseRadonParameterName(itsParamIterator->name(), paramParts);

      if (!itsEnsembleDim.isNull())
      {
        // Get ensemble dimension

        auto forecastType = getForecastType(itsParamIterator->name(), paramParts);
        auto forecastNumber = getForecastNumber(itsParamIterator->name(), paramParts);

        ensembleDim = getEnsembleDimension(forecastType, forecastNumber);
      }

      if (!itsLevelDim.isNull())
      {
        // Get level dimension and index

        int level = getParamLevel(itsParamIterator->name(), paramParts);

        levelDim = getLevelDimAndIndex(itsParamIterator->name(), level, levelIndex);
      }
    }

    const bool hasLevelDim = !levelDim.isNull();
    std::size_t nX = (long)itsNX, nY = (long)itsNY;

    std::vector<std::size_t> offsets, edges;

    // Ensemble dimension is always first, if it exists
    if (!ensembleDim.isNull())
    {
      offsets.push_back(0);
      edges.push_back(1);  // Ensemble dimension, edge length 1
    }

    // Time dimension is always after it or the first if ensemble is not used
    offsets.push_back(timeIndex);
    edges.push_back(1);  // Time dimension, edge length 1

    if (hasLevelDim)
    {
      offsets.push_back(levelIndex);
      edges.push_back(1);  // Level dimension, edge length 1
    }

    offsets.push_back(y0);
    edges.push_back(nY);  // Y dimension, edge length nY

    offsets.push_back(x0);
    edges.push_back(nX);  // X dimension, edge length nX

    try
    {
      itsVarIterator->putVar(offsets, edges, values.get());
    }
    catch(const std::exception& e)
    {
      auto error = Fmi::Exception::Trace(BCP, "Storing parameter values failed!");
      const auto dims = itsVarIterator->getDims();
      error.addParameter("Name", itsParamIterator->name());
      error.addParameter("offsets", Fmi::join(offsets, [](std::size_t v) { return Fmi::to_string(v); }, ","));
      error.addParameter("edges", Fmi::join(edges, [](std::size_t v) { return Fmi::to_string(v); }, ","));
      error.addParameter("Variable dimensions", Fmi::join(dims, [](const NcDim& d) { return d.isNull() ? "-" : Fmi::to_string(d.getSize()); }, ","));
      throw error;
    }
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Handle change of parameter
 *
 */
// ----------------------------------------------------------------------

void NetCdfStreamer::paramChanged(size_t nextParamOffset)
{
  try
  {
    // Note: Netcdf varibles are created when first nonmissing querydata parameter is encountered

    if (itsDataVars.size() > 0)
    {
      for (size_t n = 0; (n < nextParamOffset); n++)
      {
        if (itsVarIterator != itsDataVars.end())
          itsVarIterator++;

        if ((itsVarIterator == itsDataVars.end()) && (itsParamIterator != itsDataParams.end()))
          throw Fmi::Exception(BCP, "paramChanged: internal: No more netcdf variables");
      }
    }
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Load chunk of data; called by DataStreamer to get format specific chunk.
 *
 */
// ----------------------------------------------------------------------

void NetCdfStreamer::getDataChunk(Engine::Querydata::Q q,
                                  const NFmiArea *area,
                                  NFmiGrid *grid,
                                  int /* level */,
                                  const NFmiMetTime & /* mt */,
                                  NFmiDataMatrix<float> & /* values */,
                                  string &chunk)
{
  try
  {
    if (itsMetaFlag)
    {
      // NcFile metadata generation is not thread safe

      Spine::WriteLock lock(myFileOpenMutex);

      requireNcFile();

      // Set geometry and dimensions

      setGeometry(q, area, grid);

      // Add variables

      addVariables(q->isRelativeUV());

      itsMetaFlag = false;
    }

    // Data is loaded from 'values'; set nonempty chunk to indicate data is available.

    chunk = " ";
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Load chunk of grid data; called by DataStreamer to get format specific chunk.
 *
 */
// ----------------------------------------------------------------------

void NetCdfStreamer::getGridDataChunk(const QueryServer::Query &gridQuery,
                                      int,
                                      const NFmiMetTime &,
                                      string &chunk)
{
  try
  {
    if (itsMetaFlag)
    {
      // NcFile metadata generation is not thread safe

      Spine::WriteLock lock(myFileOpenMutex);

      requireNcFile();

      // Set geometry and dimensions

      setGridGeometry(gridQuery);

      // Add variables

      addVariables(false);

      itsMetaFlag = false;
    }

    // Data is loaded from 'values'; set nonempty chunk to indicate data is available.

    chunk = " ";
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

}  // namespace Download
}  // namespace Plugin
}  // namespace SmartMet

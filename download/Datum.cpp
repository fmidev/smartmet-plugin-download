// ======================================================================
/*!
 * \brief Datum handling
 */
// ======================================================================

#include "Datum.h"

#include <newbase/NFmiArea.h>

#include <macgyver/HelmertTransformation.h>
#include <macgyver/StringConversion.h>
#include <spine/Exception.h>

#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/lexical_cast.hpp>

#include <ogr_spatialref.h>

#include <vector>

using namespace std;

namespace SmartMet
{
namespace Plugin
{
namespace Download
{
namespace Datum
{
// ----------------------------------------------------------------------
/*!
 * \brief Parse a datum setting
 */
// ----------------------------------------------------------------------

static bool datumShiftFromString(const string& setting, DatumShift& datumShift)
{
  try
  {
    static struct
    {
      const char* name;
      DatumShift datumShift;
    } datumShifts[] = {{"None", DatumShift::None},
                       {"FMI", DatumShift::Fmi},
                       {"EPSG", DatumShift::Epsg},
                       {"WGS84", DatumShift::Wgs84},
                       {"HPNoScale", DatumShift::HPNoScale},
                       {"HPNS", DatumShift::HPNoScale},
                       {"HPDefaultScale", DatumShift::HPDefaultScale},
                       {"HPDS", DatumShift::HPDefaultScale},
                       {"HPPreserveEWScale", DatumShift::HPPreserveEWScale},
                       {"HPPEWS", DatumShift::HPPreserveEWScale},
                       {"HPPreserveSNScale", DatumShift::HPPreserveSNScale},
                       {"HPPSNS", DatumShift::HPPreserveSNScale},
                       {nullptr, DatumShift::None}};

    string s = boost::trim_copy(Fmi::ascii_tolower_copy(setting));

    if (!s.empty())
      for (unsigned int i = 0; datumShifts[i].name; i++)
      {
        string name(datumShifts[i].name);

        if (Fmi::ascii_tolower_copy(name) == s)
        {
          datumShift = datumShifts[i].datumShift;
          return true;
        }
      }

    return false;
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

bool parseDatumShift(const string& setting, DatumShift& datumShift)
{
  try
  {
    string s = boost::trim_copy(setting);

    if (!s.empty())
      return datumShiftFromString(setting, datumShift);

    datumShift = DatumShift::None;

    return true;
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Return true if using datum shift to wgs84
 *
 */
// ----------------------------------------------------------------------

bool isDatumShiftToWGS84(DatumShift datumShift)
{
  try
  {
    return (datumShift >= DatumShift::Wgs84) && (datumShift <= DatumShift::HPPreserveSNScale);
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Extract helmert transformation parameters from proj4 '+towgs'
 * 		parameter
 *
 */
// ----------------------------------------------------------------------

void getHelmertTransformationParameters(DatumShift datumShift,
                                        const NFmiArea* Area,
                                        const OGRSpatialReference& SRS,
                                        double transformationParameters[7])
{
  try
  {
    OGRErr err;

    const double GR = M_PI / 180.0;
    const double lat0 = Area->CenterLatLon().X();
    const double lon0 = Area->CenterLatLon().Y();

    double R0 = SRS.GetSemiMajor(&err);
    if (err != OGRERR_NONE)
      throw Spine::Exception(
          BCP,
          "getTransformationParameters: GetSemiMajor() error " + boost::lexical_cast<string>(err));

    string towgs84;

    if ((datumShift == DatumShift::HPNoScale) || (datumShift == DatumShift::HPPreserveEWScale) ||
        (datumShift == DatumShift::HPPreserveSNScale))
    {
      enum Fmi::HelmertTransformation::FmiSphereConvScalingType scalingType =
          ((datumShift == DatumShift::HPNoScale)
               ? Fmi::HelmertTransformation::FMI_SPHERE_NO_SCALING
               : ((datumShift == DatumShift::HPPreserveEWScale)
                      ? Fmi::HelmertTransformation::FMI_SPHERE_PRESERVE_EAST_WEST_SCALE
                      : Fmi::HelmertTransformation::FMI_SPHERE_PRESERVE_SOUTH_NORTH_SCALE));
      towgs84 = Fmi::get_fmi_sphere_towgs84_proj4_string(R0, GR * lat0, GR * lon0, scalingType);
    }
    else
      // Use default scaling
      //
      towgs84 = Fmi::get_fmi_sphere_towgs84_proj4_string(R0, GR * lat0, GR * lon0);

    boost::algorithm::replace_first(towgs84, "+towgs84=", "");
    vector<string> flds;
    boost::algorithm::split(flds, towgs84, boost::is_any_of(","));

    if (flds.size() == 7)
    {
      for (int i = 0; (i < 7); i++)
        if ((i <= 2) || (i == 6))
        {
          boost::trim(flds[i]);

          try
          {
            transformationParameters[i] = boost::lexical_cast<double>(flds[i]);

            if (i == 6)
              return;
          }
          catch (...)
          {
            break;
          }
        }
        else
          transformationParameters[i] = 0.0;
    }

    throw Spine::Exception(
        BCP, "getTransformationParameters: invalid '+towgs84' parameter '" + towgs84 + "'");
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

}  // namespace Datum
}  // namespace Download
}  // namespace Plugin
}  // namespace SmartMet

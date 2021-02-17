// ======================================================================
/*!
 * \brief Datum handling
 */
// ======================================================================

#pragma once

#include <string>

class NFmiArea;
class OGRSpatialReference;

namespace SmartMet
{
namespace Plugin
{
namespace Download
{
namespace Datum
{
// Datum handling
//
typedef enum
{
  None,            // No datum transformation. Using newbase projection.
  FMI,             // No datum transformation. Using proj4 projection.
  EPSG,            // Output datum wgs84 using default scaling if implied by epsg projection
  WGS84,           // Output datum wgs84 using default scaling (same as HPDefaultScale)
  HPNoScale,       // Using no scaling when getting Helmert transformation parameters.
  HPDefaultScale,  // Using default scaling when getting Helmert transformation parameters (same as
                   // WGS84)
  HPPreserveEWScale,  // Preserving east/west scale when getting Helmert transformation parameters.
  HPPreserveSNScale  // Preserving south/north scale when getting Helmert transformation parameters.
} DatumShift;        //
                     // Note: HP...Scale values implies shift to wgs84 datum.
                     //
// Note: Do not change or break the ordering without checking/changing isDatumShiftToWGS84().
//

const std::string epsgWGS84DatumName("WGS_1984");

bool parseDatumShift(const std::string& setting, DatumShift& datumShift);

bool isDatumShiftToWGS84(DatumShift shift);
void getHelmertTransformationParameters(DatumShift datumShift,
                                        const NFmiArea* Area,
                                        const OGRSpatialReference& SRS,
                                        double helmertTransformationParameters[7]);

namespace Sphere
{
namespace Grib1
{
// Shape of the earth
//
typedef enum
{
  WGS84 = 6  // lsb0 bit position; unset for spherical (radius 6367.47), set for oblate spheroidal
             // (IAU in 1965 (6378.160 km, 6356.775 km, f = 1/297.0))
} Sphere;
}  // namespace Grib1

namespace Grib2
{
// Shape of the earth
//
typedef enum
{
  WGS84 = 5,        // WGS84; as used by ICAO since 1998
  Fmi_6371229m = 6  // Fmi; spherical with radius of 6,371,229.0 m
} Sphere;
}  // namespace Grib2

namespace NetCdf
{
// Shape of the earth
//
const float WGS84_semiMajor = 6378137.0;           // WGS84
const double WGS84_invFlattening = 298.257223563;  //
const float Fmi_6371220m = 6371220.0;              // Fmi

}  // namespace NetCdf
}  // namespace Sphere
}  // namespace Datum
}  // namespace Download
}  // namespace Plugin
}  // namespace SmartMet

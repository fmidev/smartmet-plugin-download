// ======================================================================
/*!
 * \brief SmartMet download service plugin; grib streaming
 */
// ======================================================================

#include "GribStreamer.h"
#include "Datum.h"
#include "Plugin.h"
#include <boost/foreach.hpp>
#include <boost/interprocess/sync/lock_options.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>
#include <fmt/format.h>
#include <gis/ProjInfo.h>
#include <macgyver/Exception.h>
#include <macgyver/StringConversion.h>
#include <newbase/NFmiEnumConverter.h>
#include <newbase/NFmiQueryData.h>
#include <newbase/NFmiQueryDataUtil.h>
#include <newbase/NFmiTimeList.h>
#include <sys/types.h>
#include <string>
#include <unistd.h>

using namespace std;

using namespace boost::interprocess;

namespace SmartMet
{
namespace Plugin
{
namespace Download
{
template <typename T>
boost::optional<vector<pair<T, T>>> nPairsOfValues(string &pvs, const char *param, size_t nPairs);

GribStreamer::GribStreamer(const Spine::HTTP::Request &req,
                           const Config &config,
                           const Query &query,
                           const Producer &producer,
                           const ReqParams &reqParams)
    : DataStreamer(req, config, query, producer, reqParams)
    , itsGrib1Flag(reqParams.outputFormat == Grib1)
{
  try
  {
    // Get grib handle

    grib_context *c = grib_context_get_default();
    itsGribHandle = grib_handle_new_from_samples(c, itsGrib1Flag ? "GRIB1" : "GRIB2");
    if (!itsGribHandle)
      throw Fmi::Exception(BCP,
                             string("Could not get handle for grib") + (itsGrib1Flag ? "1" : "2"));

    // Set tables version for grib2

    if (reqParams.grib2TablesVersion > 0)
      gset(itsGribHandle,
             "gribMasterTablesVersionNumber", (unsigned long)reqParams.grib2TablesVersion);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

GribStreamer::~GribStreamer()
{
  if (itsGribHandle)
    grib_handle_delete(itsGribHandle);
}

// ----------------------------------------------------------------------
/*!
 * \brief Determine grid x/y scanning directions
 */
// ----------------------------------------------------------------------

void GribStreamer::scanningDirections(long &iNegative, long &jPositive) const
{
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wswitch-enum"
  try
  {
    // newbase enum includes all kinds of variations
    // which are useless here. Should use specific
    // enums for specific purposes
    //
    // ???
    //
    // Did not quite get the comment above in plugin's point of view, using the same enums as
    // newbase
    // ...
    //
    //		e.g. NFmiGridBase.cpp:
    //
    //		switch(itsStartingCorner)
    //		{
    //		case kBottomLeft:
    //		  return true;
    //		case kBottomRight:
    //		  {
    //		  }
    //		case kTopLeft:
    //		  {
    //		  }
    //		case kTopRight:
    //		  {
    //		  }
    //		default:
    //		  {
    //		  }
    //		}

    switch (itsGridOrigo)
    {
      case kTopLeft:
        iNegative = 0;
        jPositive = 0;
        break;

      case kTopRight:
        iNegative = 1;
        jPositive = 0;
        break;

      case kBottomLeft:
        iNegative = 0;
        jPositive = 1;
        break;

      case kBottomRight:
        iNegative = 1;
        jPositive = 1;
        break;

      default:
        throw Fmi::Exception(BCP, "Unknown grid scanning mode");
    }
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
#pragma clang diagnostic pop
}

// ----------------------------------------------------------------------
/*!
 * \brief Set shape of the earth
 *
 */
// ----------------------------------------------------------------------

void GribStreamer::setShapeOfTheEarth(const NFmiArea *area)
{
  try
  {
    string ellipsoid;
    double radiusOrSemiMajor, invFlattening;

    OGRSpatialReference *geometrySRS = itsResources.getGeometrySRS();

    if ((!geometrySRS) && (!area))
      throw Fmi::Exception(BCP, "Internal error, either SRS or NFmiArea is required");

    const string WKT = (geometrySRS ? "" : area->WKT());

    extractSpheroidFromGeom(geometrySRS, WKT, ellipsoid, radiusOrSemiMajor, invFlattening);

    long resolAndCompFlags = get_long(itsGribHandle, "resolutionAndComponentFlags");

    if (itsGrib1Flag)
    {
      if (invFlattening > 0)
        resolAndCompFlags |= (1 << static_cast<int>(Datum::Grib1::Sphere::Wgs84));
      else
        resolAndCompFlags &= ~(1 << static_cast<int>(Datum::Grib1::Sphere::Wgs84));

      gset(itsGribHandle, "resolutionAndComponentFlags", resolAndCompFlags);
    }
    else
    {
      uint8_t shapeOfTheEarth;

      if (ellipsoid == "WGS 84")
        shapeOfTheEarth = 5;  // WGS84
      else if (ellipsoid == "GRS 1980")
        shapeOfTheEarth = 4;  // IAG-GRS80
      else if ((fabs(invFlattening - 297) < 0.01) && (fabs(radiusOrSemiMajor - 6378160.0) < 0.01))
        shapeOfTheEarth = 2;  // IAU in 1965
      else if (invFlattening > 0)
        shapeOfTheEarth = 7;
      else if (fabs(radiusOrSemiMajor - 6367470.0) < 0.01)
        shapeOfTheEarth = 0;
      else if (fabs(radiusOrSemiMajor - 6371229.0) < 0.01)
        shapeOfTheEarth = 6;
      else
      {
        // Spherical with radius specified by data producer

        shapeOfTheEarth = 1;
      }

      gset(itsGribHandle, "shapeOfTheEarth", shapeOfTheEarth);

      if (shapeOfTheEarth == 1)
      {
        gset(itsGribHandle, "scaleFactorOfRadiusOfSphericalEarth", 0.0);
        gset(itsGribHandle, "scaledValueOfRadiusOfSphericalEarth", radiusOrSemiMajor);
      }
      else if (shapeOfTheEarth == 7)
      {
        double semiMinor = radiusOrSemiMajor - (radiusOrSemiMajor * (1 / invFlattening));

        gset(itsGribHandle, "scaleFactorOfMajorAxisOfOblateSpheroidEarth", 0.0);
        gset(itsGribHandle, "scaledValueOfMajorAxisOfOblateSpheroidEarth", radiusOrSemiMajor);
        gset(itsGribHandle, "scaleFactorOfMinorAxisOfOblateSpheroidEarth", 0.0);
        gset(itsGribHandle, "scaledValueOfMinorAxisOfOblateSpheroidEarth", semiMinor);
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
 * \brief Set grib latlon projection metadata
 */
// ----------------------------------------------------------------------

void GribStreamer::setLatlonGeometryToGrib() const
{
  try
  {
    gset(itsGribHandle, "typeOfGrid", "regular_ll");

    gset(itsGribHandle, "longitudeOfFirstGridPointInDegrees", itsBoundingBox.bottomLeft.X());
    gset(itsGribHandle, "latitudeOfFirstGridPointInDegrees", itsBoundingBox.bottomLeft.Y());
    gset(itsGribHandle, "longitudeOfLastGridPointInDegrees", itsBoundingBox.topRight.X());
    gset(itsGribHandle, "latitudeOfLastGridPointInDegrees", itsBoundingBox.topRight.Y());

    gset(itsGribHandle, "Ni", itsNX);
    gset(itsGribHandle, "Nj", itsNY);

    double gridCellHeightInDegrees =
        fabs((itsBoundingBox.topRight.Y() - itsBoundingBox.bottomLeft.Y()) / (itsNY - 1));
    double gridCellWidthInDegrees =
        fabs((itsBoundingBox.topRight.X() - itsBoundingBox.bottomLeft.X()) / (itsNX - 1));

    long iNegative, jPositive;

    scanningDirections(iNegative, jPositive);

    gset(itsGribHandle, "jScansPositively", jPositive);
    gset(itsGribHandle, "iScansNegatively", iNegative);

    gset(itsGribHandle, "iDirectionIncrementInDegrees", gridCellWidthInDegrees);
    gset(itsGribHandle, "jDirectionIncrementInDegrees", gridCellHeightInDegrees);

    // DUMP(itsGribHandle, "geography");
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Set grib rotated latlon projection metadata
 *
 */
// ----------------------------------------------------------------------

void GribStreamer::setRotatedLatlonGeometryToGrib(const NFmiArea *area)
{
  try
  {
    BBoxCorners rotLLBBox;
    double slon = 0.0, slat = 0.0;

    if (itsReqParams.dataSource == QueryData)
    {
      auto geometrySRS = itsResources.getGeometrySRS();

      if ((!geometrySRS) && (!area))
        throw Fmi::Exception(BCP, "Internal error, either SRS or NFmiArea is required");

      auto srs = geometrySRS ? Fmi::SpatialReference(*geometrySRS) : area->SpatialReference();
      auto projInfo = srs.projInfo();

      auto opt_plat = projInfo.getDouble("o_lat_p");
      auto opt_plon = projInfo.getDouble("o_lon_p");

      if (*opt_plon != 0)
        throw Fmi::Exception(
            BCP, "GRIB does not support rotated latlon areas where longitude is also rotated");

      slon = *opt_plon;
      slat = -(*opt_plat);

      auto rotEqcSRS = srs.get();
      char *p4Str;
      rotEqcSRS->exportToProj4(&p4Str);
      string rotLLp4Str(boost::algorithm::replace_first_copy(string(p4Str), "eqc", "latlong"));
      CPLFree(p4Str);
      OGRSpatialReference rotLLSRS;
      rotLLSRS.importFromProj4(rotLLp4Str.c_str());
      rotLLSRS.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);

      OGRSpatialReference llSRS;
      llSRS.importFromProj4("+proj=latlong +datum=WGS84");
      llSRS.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);

      OGRCoordinateTransformation *ll2rotLLCT =
          itsResources.getCoordinateTransformation(&llSRS, &rotLLSRS);

      double lon[] = { itsBoundingBox.bottomLeft.X(), itsBoundingBox.topRight.X() };
      double lat[] = { itsBoundingBox.bottomLeft.Y(), itsBoundingBox.topRight.Y() };

      if (!(ll2rotLLCT->Transform(2, lon, lat)))
        throw Fmi::Exception(BCP, "Coordinate transformation failed");

      rotLLBBox = BBoxCorners{NFmiPoint(lon[0], lat[0]), NFmiPoint(lon[1], lat[1])};
    }
    else
    {
      // TODO: Negate slat ?

      slon = itsGridMetaData.southernPoleLon;
      slat = itsGridMetaData.southernPoleLat;

      rotLLBBox = *(itsGridMetaData.targetBBox);
    }

    if (slon)
      throw Fmi::Exception(
          BCP, "GRIB does not support rotated latlon areas where longitude is also rotated");

    gset(itsGribHandle, "typeOfGrid", "rotated_ll");

    gset(itsGribHandle, "latitudeOfSouthernPoleInDegrees", slat);

    gset(itsGribHandle, "longitudeOfFirstGridPointInDegrees", rotLLBBox.bottomLeft.X());
    gset(itsGribHandle, "latitudeOfFirstGridPointInDegrees", rotLLBBox.bottomLeft.Y());
    gset(itsGribHandle, "longitudeOfLastGridPointInDegrees", rotLLBBox.topRight.X());
    gset(itsGribHandle, "latitudeOfLastGridPointInDegrees", rotLLBBox.topRight.Y());

    gset(itsGribHandle, "Ni", itsNX);
    gset(itsGribHandle, "Nj", itsNY);

    double gridCellHeightInDegrees =
        fabs((rotLLBBox.topRight.Y() - rotLLBBox.bottomLeft.Y()) / (itsNY - 1));
    double gridCellWidthInDegrees =
        fabs((rotLLBBox.topRight.X() - rotLLBBox.bottomLeft.X()) / (itsNX - 1));

    long iNegative, jPositive;

    scanningDirections(iNegative, jPositive);

    gset(itsGribHandle, "jScansPositively", jPositive);
    gset(itsGribHandle, "iScansNegatively", iNegative);

    gset(itsGribHandle, "iDirectionIncrementInDegrees", gridCellWidthInDegrees);
    gset(itsGribHandle, "jDirectionIncrementInDegrees", gridCellHeightInDegrees);

    // DUMP(itsGribHandle, "geography");
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Set grib stereographic projection metadata
 *
 * Defaults obtained by a DUMP call once the projection is set:
 *
 * Nx = 16
 * Ny = 31
 * latitudeOfFirstGridPointInDegrees = 60
 * longitudeOfFirstGridPointInDegrees = 0
 * LaDInDegrees = 0
 * orientationOfTheGridInDegrees = 0
 * DxInMetres = 2000
 * DyInMetres = 2000
 * iScansNegatively = 0
 * jScansPositively = 0
 * jPointsAreConsecutive = 0
 * gridType = "polar_stereographic"
 * bitmapPresent = 0
 *
 * HOWEVER: GRIB1 has a fixed true latitude of 60 degrees, atleast if you
 * look at /usr/share/grib_api/definitions/grib1/grid_definition_5.def
 */
// ----------------------------------------------------------------------

void GribStreamer::setStereographicGeometryToGrib(const NFmiArea *area) const
{
  try
  {
    OGRSpatialReference *geometrySRS = itsResources.getGeometrySRS();

    if ((!geometrySRS) && (!area))
      throw Fmi::Exception(BCP, "Internal error, either SRS or NFmiArea is required");

    gset(itsGribHandle, "typeOfGrid", "polar_stereographic");

    // Note: grib2 longitude 0-360

    double lon = itsBoundingBox.bottomLeft.X(), lon_0, lat_0, lat_ts;

    if ((!itsGrib1Flag) && (lon < 0))
      lon += 360;

    gset(itsGribHandle, "longitudeOfFirstGridPointInDegrees", lon);
    gset(itsGribHandle, "latitudeOfFirstGridPointInDegrees", itsBoundingBox.bottomLeft.Y());

    gset(itsGribHandle, "Ni", itsNX);
    gset(itsGribHandle, "Nj", itsNY);

    gset(itsGribHandle, "DxInMetres", fabs(itsDX));
    gset(itsGribHandle, "DyInMetres", fabs(itsDY));

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

    if ((!itsGrib1Flag) && (lon_0 < 0))
      lon_0 += 360;

    gset(itsGribHandle, "orientationOfTheGridInDegrees", lon_0);

    long iNegative, jPositive;

    scanningDirections(iNegative, jPositive);

    gset(itsGribHandle, "jScansPositively", jPositive);
    gset(itsGribHandle, "iScansNegatively", iNegative);

    if (!itsGrib1Flag)
      gset(itsGribHandle, "LaDInDegrees", lat_ts);
    else if (lat_ts != 60)
      throw Fmi::Exception(
          BCP,
          "GRIB1 true latitude can only be 60 for polar stereographic projections with grib_api "
          "library");

    if (lat_0 != 90 && lat_0 != -90)
      throw Fmi::Exception(BCP, "GRIB format supports only polar stereographic projections");

    if (lat_0 != 90)
      throw Fmi::Exception(BCP, "Only N-pole polar stereographic projections are supported");

    // DUMP(itsGribHandle,"geography");
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Set grib mercator projection metadata
 *
 */
// ----------------------------------------------------------------------

void GribStreamer::setMercatorGeometryToGrib() const
{
  try
  {
    gset(itsGribHandle, "typeOfGrid", "mercator");

    // Note: grib2 longitude 0-360

    double lon = itsBoundingBox.bottomLeft.X();

    if ((!itsGrib1Flag) && (lon < 0))
      lon += 360;

    gset(itsGribHandle, "longitudeOfFirstGridPointInDegrees", lon);
    gset(itsGribHandle, "latitudeOfFirstGridPointInDegrees", itsBoundingBox.bottomLeft.Y());

    lon = itsBoundingBox.topRight.X();

    if ((!itsGrib1Flag) && (lon < 0))
      lon += 360;

    gset(itsGribHandle, "longitudeOfLastGridPointInDegrees", lon);
    gset(itsGribHandle, "latitudeOfLastGridPointInDegrees", itsBoundingBox.topRight.Y());

    gset(itsGribHandle, "Ni", itsNX);
    gset(itsGribHandle, "Nj", itsNY);

    gset(itsGribHandle, "DiInMetres", fabs(itsDX));
    gset(itsGribHandle, "DjInMetres", fabs(itsDY));

    long iNegative, jPositive;

    scanningDirections(iNegative, jPositive);

    gset(itsGribHandle, "jScansPositively", jPositive);
    gset(itsGribHandle, "iScansNegatively", iNegative);

    double lon_0 = 0, lat_ts = 0;

    OGRSpatialReference *geometrySRS = itsResources.getGeometrySRS();

    if (geometrySRS)
    {
      lon_0 = getProjParam(*geometrySRS, SRS_PP_CENTRAL_MERIDIAN);

      if ((!itsGrib1Flag) && (lon_0 < 0))
        lon_0 += 360;

      if (EQUAL(itsGridMetaData.projection.c_str(), SRS_PT_MERCATOR_2SP))
      {
        lat_ts = getProjParam(*geometrySRS, SRS_PP_STANDARD_PARALLEL_1);
      }
    }

    gset(itsGribHandle, "orientationOfTheGridInDegrees", lon_0);
    gset(itsGribHandle, "LaDInDegrees", lat_ts);

    // DUMP(itsGribHandle,"geography");
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Set grib lambert conformal projection metadata
 *
 */
// ----------------------------------------------------------------------

void GribStreamer::setLambertConformalGeometryToGrib(const NFmiArea *area) const
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

    gset(itsGribHandle, "typeOfGrid", "lambert");

    // Note: grib2 longitude 0-360

    double lon = itsBoundingBox.bottomLeft.X();

    if ((!itsGrib1Flag) && (lon < 0))
      lon += 360;

    gset(itsGribHandle, "longitudeOfFirstGridPointInDegrees", lon);
    gset(itsGribHandle, "latitudeOfFirstGridPointInDegrees", itsBoundingBox.bottomLeft.Y());

    gset(itsGribHandle, "Nx", itsNX);
    gset(itsGribHandle, "Ny", itsNY);

    gset(itsGribHandle, "DxInMetres", fabs(itsDX));
    gset(itsGribHandle, "DyInMetres", fabs(itsDY));

    long iNegative, jPositive;

    scanningDirections(iNegative, jPositive);

    gset(itsGribHandle, "jScansPositively", jPositive);
    gset(itsGribHandle, "iScansNegatively", iNegative);

    double southPoleLon = 0;
    double southPoleLat = -90;

    gset(itsGribHandle, "longitudeOfSouthernPoleInDegrees", southPoleLon);
    gset(itsGribHandle, "latitudeOfSouthernPoleInDegrees", southPoleLat);

    double lat_ts = getProjParam(*geometrySRS, SRS_PP_LATITUDE_OF_ORIGIN);
    double lon_0 = getProjParam(*geometrySRS, SRS_PP_CENTRAL_MERIDIAN);

    auto projection = geometrySRS->GetAttrValue("PROJECTION");
    if (!projection)
      throw Fmi::Exception(BCP, "Geometry PROJECTION not set");

    double latin1 = getProjParam(*geometrySRS, SRS_PP_STANDARD_PARALLEL_1);
    double latin2;

    if (EQUAL(projection, SRS_PT_LAMBERT_CONFORMAL_CONIC_2SP))
      latin2 = getProjParam(*geometrySRS, SRS_PP_STANDARD_PARALLEL_2);
    else
      latin2 = latin1;

    gset(itsGribHandle, "Latin1InDegrees", latin1);
    gset(itsGribHandle, "Latin2InDegrees", latin2);

    // Error with grib1 if setting LaDInDegrees (meps: to latin1) atleast if projection
    // truely is SP1 (latin2 == latin1)

    if ((!itsGrib1Flag) && (lon_0 < 0))
      lon_0 += 360;

    if (!itsGrib1Flag)
      gset(itsGribHandle, "LaDInDegrees", ((latin2 == latin1) ? latin1 : lat_ts));

    gset(itsGribHandle, "LoVInDegrees", lon_0);

    // DUMP(itsGribHandle,"geography");
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Set grib lambert equal area projection metadata
 *
 */
// ----------------------------------------------------------------------

void GribStreamer::setLambertAzimuthalEqualAreaGeometryToGrib() const
{
  try
  {
    if (itsGrib1Flag)
      throw Fmi::Exception(BCP, "LAEA is not supported in grib1 format");

    OGRSpatialReference *geometrySRS = itsResources.getGeometrySRS();

    if (!geometrySRS)
      throw Fmi::Exception(BCP, "SRS is not set");

    gset(itsGribHandle, "typeOfGrid", "lambert_azimuthal_equal_area");

    // Note: grib2 longitude 0-360

    double lon = itsBoundingBox.bottomLeft.X();

    if ((!itsGrib1Flag) && (lon < 0))
      lon += 360;

    gset(itsGribHandle, "longitudeOfFirstGridPointInDegrees", lon);
    gset(itsGribHandle, "latitudeOfFirstGridPointInDegrees", itsBoundingBox.bottomLeft.Y());

    gset(itsGribHandle, "Nx", itsNX);
    gset(itsGribHandle, "Ny", itsNY);

    gset(itsGribHandle, "DxInMetres", fabs(itsDX));
    gset(itsGribHandle, "DyInMetres", fabs(itsDY));

    long iNegative, jPositive;

    scanningDirections(iNegative, jPositive);

    gset(itsGribHandle, "jScansPositively", jPositive);
    gset(itsGribHandle, "iScansNegatively", iNegative);

    double lat_ts = getProjParam(*geometrySRS, SRS_PP_LATITUDE_OF_ORIGIN);
    double lon_0 = getProjParam(*geometrySRS, SRS_PP_LONGITUDE_OF_CENTER);

    if ((!itsGrib1Flag) && (lon_0 < 0))
      lon_0 += 360;

    gset(itsGribHandle, "standardParallelInDegrees", lat_ts);
    gset(itsGribHandle, "centralLongitudeInDegrees", lon_0);

    DUMP(itsGribHandle, "geography");
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Set grib named configuration settings.
 *
 */
// ----------------------------------------------------------------------

void GribStreamer::setNamedSettingsToGrib() const
{
  try
  {
    string producer;

    if (itsReqParams.dataSource == GridContent)
    {
      // Take producer name from radon parameter name T-K:MEPS:1093:6,...

      vector<string> paramParts;
      itsQuery.parseRadonParameterName(itsParamIterator->name(), paramParts);
      producer = paramParts[1];
    }
    else
      producer = itsReqParams.producer;

    const Producer &pr = itsCfg.getProducer(producer);
    auto setBeg = pr.namedSettingsBegin();
    auto setEnd = pr.namedSettingsEnd();
    const char *const centre = "centre";
    bool hasCentre = false;

    for (auto it = setBeg; (it != setEnd); it++)
    {
      gset(itsGribHandle, (it->first).c_str(), it->second);

      if (it->first == centre)
        hasCentre = true;
    }

    // Use default procuder's centre by default

    if (!hasCentre)
    {
      const Producer &dpr = itsCfg.defaultProducer();
      const auto dit = dpr.namedSettings.find(centre);

      if (dit != dpr.namedSettingsEnd())
        gset(itsGribHandle, (dit->first).c_str(), dit->second);
    }
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Set grib projection metadata
 *
 */
// ----------------------------------------------------------------------

void GribStreamer::setGeometryToGrib(const NFmiArea *area, bool relative_uv)
{
  try
  {
    int classId = (itsReqParams.areaClassId != A_Native)
        ? (int) itsReqParams.areaClassId
        : area->ClassId();

    itsValueArray.resize(itsNX * itsNY);

    switch (classId)
    {
      case kNFmiLatLonArea:
        setLatlonGeometryToGrib();
        break;
      case kNFmiRotatedLatLonArea:
        setRotatedLatlonGeometryToGrib(area);
        break;
      case kNFmiStereographicArea:
        setStereographicGeometryToGrib(area);
        break;
      case kNFmiMercatorArea:
        setMercatorGeometryToGrib();
        break;
      case kNFmiLambertConformalConicArea:
        setLambertConformalGeometryToGrib(area);
        break;
      case kNFmiEquiDistArea:
        throw Fmi::Exception(BCP, "Equidistant projection is not supported by GRIB");
      case kNFmiGnomonicArea:
        throw Fmi::Exception(BCP, "Gnomonic projection is not supported by GRIB");
      case kNFmiPKJArea:
        throw Fmi::Exception(BCP, "PKJ projection is not supported by GRIB");
      case kNFmiYKJArea:
        throw Fmi::Exception(BCP, "YKJ projection is not supported by GRIB");
      case kNFmiKKJArea:
        throw Fmi::Exception(BCP, "KKJ projection is not supported by GRIB");
      default:
        throw Fmi::Exception(BCP, "Unsupported projection in input data");
    }

    // Set packing type

    if (!itsReqParams.packing.empty())
      gset(itsGribHandle, "packingType", itsReqParams.packing);

    // Set shape of the earth

    setShapeOfTheEarth(area);

    // Set wind component relativeness

    long resolAndCompFlags = get_long(itsGribHandle, "resolutionAndComponentFlags");

    if (relative_uv)
      resolAndCompFlags |= (1 << 3);
    else
      resolAndCompFlags &= ~(1 << 3);

    gset(itsGribHandle, "resolutionAndComponentFlags", resolAndCompFlags);

    // Bitmap to flag missing values

    gset(itsGribHandle, "bitmapPresent", 1);
    gset(itsGribHandle, "missingValue", gribMissingValue);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Set grid grib projection metadata
 *
 */
// ----------------------------------------------------------------------

void GribStreamer::setGridGeometryToGrib(const QueryServer::Query &gridQuery)
{
  try
  {
    itsValueArray.resize(itsNX * itsNY);

    switch (itsGridMetaData.projType)
    {
      case T::GridProjectionValue::LatLon:
        setLatlonGeometryToGrib();
        break;
      case T::GridProjectionValue::RotatedLatLon:
        setRotatedLatlonGeometryToGrib();
        break;
      case T::GridProjectionValue::PolarStereographic:
        setStereographicGeometryToGrib();
        break;
      case T::GridProjectionValue::Mercator:
        setMercatorGeometryToGrib();
        break;
      case T::GridProjectionValue::LambertConformal:
        setLambertConformalGeometryToGrib();
        break;
      case T::GridProjectionValue::LambertAzimuthalEqualArea:
        setLambertAzimuthalEqualAreaGeometryToGrib();
        break;
      default:
        throw Fmi::Exception(BCP, "Unsupported projection in input data");
    }

    // Set packing type

    if (!itsReqParams.packing.empty())
      gset(itsGribHandle, "packingType", itsReqParams.packing);

    // Set shape of the earth

    setShapeOfTheEarth();

    // Set wind component relativeness

    long resolAndCompFlags = get_long(itsGribHandle, "resolutionAndComponentFlags");

    if (itsGridMetaData.relativeUV)
      resolAndCompFlags |= (1 << 3);
    else
      resolAndCompFlags &= ~(1 << 3);

    gset(itsGribHandle, "resolutionAndComponentFlags", resolAndCompFlags);

    // Bitmap to flag missing values

    gset(itsGribHandle, "bitmapPresent", 1);
    gset(itsGribHandle, "missingValue", gribMissingValue);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Set grib level and parameter. Parameter's index in config
 * 		table is returned in 'paramIdx'
 *
 */
// ----------------------------------------------------------------------

#define GroundLevel "groundOrWaterSurface"
#define PressureLevel "isobaricInhPa"
#define HybridLevel "hybrid"
#define EntireAtmosphere "entireAtmosphere"
#define HeightLevel "heightAboveSea"
#define HeightAboveGroundLevel "heightAboveGround"
#define DepthLevel "depthBelowSea"
#define NominalTopLevel "nominalTop"
#define MeanSeaLevel "meanSea"

string GribStreamer::gribLevelTypeAndLevel(bool gridContent, FmiLevelType levelType,
                                           NFmiLevel *cfgLevel, int &level) const
{
  if (gridContent)
  {
    if (isGroundLevel(levelType))
      return GroundLevel;
    else if (isEntireAtmosphereLevel(levelType))
      return EntireAtmosphere;
  }
  else
  {
    if (isSurfaceLevel(levelType))
    {
      if (cfgLevel)
      {
        level = boost::numeric_cast<int>(cfgLevel->LevelValue());
        return string(cfgLevel->GetName());
      }

      level = 0;
      return EntireAtmosphere;
    }
  }

  if (isPressureLevel(levelType, gridContent))
  {
    if (gridContent)
    {
      // Grid pressure levels are Pa, output level is hPa

      level /= 100;
    }

    return PressureLevel;
  }
  else if (isHybridLevel(levelType, gridContent))
    return HybridLevel;
  else if (isHeightLevel(levelType, level, gridContent))
    return HeightLevel;
  else if (isDepthLevel(levelType, level, gridContent))
    return DepthLevel;
  else if (isNominalTopLevel(levelType, gridContent))
  {
    level = 0;
    return NominalTopLevel;
  }
  else if (isMeanSeaLevel(levelType, gridContent))
  {
    if (level == 0)
      return MeanSeaLevel;

    return HeightLevel;
  }

  throw Fmi::Exception(
      BCP, "Unrecognized level type " + boost::lexical_cast<string>(levelType));
}

void GribStreamer::setLevelAndParameterToGrib(int level,
                                              const NFmiParam &theParam,
                                              const string &paramName,
                                              const ParamChangeTable &pTable,
                                              size_t &paramIdx)
{
  // Get parameter id, and configured level type and value for surface data.
  //
  // Using hardcoded level types for pressure, hybrid and height/depth data and for
  // surface data if level configuration is missing.

  try
  {
    string centre, levelTypeStr, radonProducer, radonParam;
    signed long usedParId = theParam.GetIdent();
    NFmiLevel *cfgLevel = nullptr;
    FmiLevelType levelType;
    T::ForecastType forecastType = 0;
    boost::optional<long> templateNumber;
    bool gridContent = (itsReqParams.dataSource == GridContent), foundParam = false;
    size_t i, j = pTable.size();
    auto paramIndexIter = paramConfigIndexes.end();

    paramIdx = pTable.size();

    if (gridContent)
    {
      // Take parameter name and level type from radon parameter name T-K:MEPS:1093:6,...

      vector<string> paramParts;
      itsQuery.parseRadonParameterName(paramName, paramParts);
      radonParam = paramParts[0];
      radonProducer = paramParts[1];

      levelType = FmiLevelType(getParamLevelId(paramName, paramParts));
      forecastType = getForecastType(paramName, paramParts);

      // Search map for the param and producer and return the parameter config index
      // if found and the radon parameter does not change (looping timesteps).
      //
      // TODO: Use parameter config index map for querydata and gridmapping queries too ?

      paramIndexIter = paramConfigIndexes.find(radonParam);

      if (paramIndexIter != paramConfigIndexes.end())
      {
        auto it = paramIndexIter->second.find(radonProducer);
        foundParam = (it != paramIndexIter->second.end());

        if (foundParam)
        {
          paramIdx = it->second;

          if (paramName == itsPreviousParam)
            return;
        }
      }

      itsPreviousParam = paramName;
    }
    else
      levelType = itsLevelType;

    if (!foundParam)
    {
      for (i = 0; i < pTable.size(); ++i)
        if (! gridContent)
        {
          if (usedParId == pTable[i].itsWantedParam.GetIdent())
          {
            // Preferring entry with level for surface data and without level for pressure and
            // hybrid data.
            // If preferred entry does not exist, taking the parameter id from the first entry
            // for the parameter.
            //
            cfgLevel = pTable[i].itsLevel;

            if ((isSurfaceLevel(levelType) && cfgLevel) || (!(isSurfaceLevel(levelType) || cfgLevel)))
              break;

            if (j == pTable.size())
              j = i;
          }
        }
        else if (pTable[i].itsRadonName == radonParam)
        {
          if (!
              (
               (itsGrib1Flag && pTable[i].itsGrib1Param) ||
               ((!itsGrib1Flag) && pTable[i].itsGrib2Param)
              )
             )
            continue;

          if (pTable[i].itsRadonProducer == radonProducer)
            break;

          if ((j == pTable.size()) && pTable[i].itsRadonProducer.empty())
            j = i;
        }
    }
    else
      i = paramIdx;

    if (i >= pTable.size())
    {
      if (gridContent && (j >= pTable.size()))
        throw Fmi::Exception(BCP, "No grib configuration for parameter " + radonParam);

      i = j;
    }

    paramIdx = i;

    if (i < pTable.size())
    {
      if (! gridContent)
        cfgLevel = pTable[i].itsLevel;
      else if (!foundParam)
      {
        if (paramIndexIter == paramConfigIndexes.end())
          paramIndexIter = paramConfigIndexes.insert(
              make_pair(radonParam, ParamConfigProducerIndexes())).first;

        paramIndexIter->second.insert(make_pair(radonProducer, paramIdx));
      }

      usedParId = pTable[i].itsOriginalParamId;
      centre = pTable[i].itsCentre;
      templateNumber = pTable[i].itsTemplateNumber;
    }

    levelTypeStr = gribLevelTypeAndLevel(gridContent, levelType, cfgLevel, level);

    if (!centre.empty())
      gset(itsGribHandle, "centre", centre);

    // Cannot set template number 0 unless stepType has been set
    //
    // Note: Comment above is weird because templateNumber is tested to be nonzero ?
    //
    // Since productDefinitionTemplateNumber is currently not available in radon (to dump
    // into plugin's grib parameter configuration), if template number is not set in configuration,
    // using templateNumber 0 (NormalProduct) for deterministic forecast data and 1
    // (EnsembleForecast) for ensemble forecasts when storing data fetched with radon names. The
    // logic does not work for all parameters though; the correct template number must be set to
    // configuration when needed.

    gset(itsGribHandle, "stepType", "instant");

    if (!itsGrib1Flag)
    {
      if (gridContent && (!templateNumber))
        templateNumber = (isEnsembleForecast(forecastType) ? 1 : 0);

      if (templateNumber && (gridContent || (*templateNumber != 0)))
        gset(itsGribHandle, "productDefinitionTemplateNumber", *templateNumber);
    }

    auto const &gribParam = (itsGrib1Flag ? pTable[i].itsGrib1Param : pTable[i].itsGrib2Param);

    if (gribParam)
    {
      if (itsGrib1Flag)
      {
        if (gribParam->itsTable2Version)
          gset(itsGribHandle, "table2Version", *(gribParam->itsTable2Version));

        gset(itsGribHandle, "indicatorOfParameter", *(gribParam->itsParamNumber));
      }
      else
      {
        gset(itsGribHandle, "discipline", *(gribParam->itsDiscipline));
        gset(itsGribHandle, "parameterCategory", *(gribParam->itsCategory));
        gset(itsGribHandle, "parameterNumber", *(gribParam->itsParamNumber));
      }
    }
    else
      gset(itsGribHandle, "paramId", usedParId);

    gset(itsGribHandle, "typeOfLevel", levelTypeStr);
    gset(itsGribHandle, "level", boost::numeric_cast<long>(abs(level)));
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Set step data into grib buffer
 */
// ----------------------------------------------------------------------

void GribStreamer::setStepToGrib(const ParamChangeTable &pTable,
                                 size_t paramIdx,
                                 bool setOriginTime,
                                 const Fmi::DateTime &validTime)
{
  try
  {
    // stepUnits always 'minute' to keep it simple

    string stepUnits = "m", stepType;
    Fmi::TimeDuration fromOriginTime(validTime - itsGribOriginTime);
    long step = (fromOriginTime.hours() * 60) + fromOriginTime.minutes();
    long startStep = step, endStep = step;

    // Set step type and calculate start and end step for aggregates.
    //
    // Note: There's no metadata available about whether given data/parameter has start or end time
    // stamping; stamping is selected with boolean 'bDataIsEndTimeStamped'.
    //
    // Even though the existence of parameter configuration block having format specific entry is
    // tested also when querying with radon names (when source=grid), the configuration
    // has been searched earlier and format specific configuration exists for the parameter.
    // Aggregate period length is currently not available as such in radon; it may have been
    // embedded in some parameter names but that is not checked. Period length will not be set if
    // it has not been manually set to configuration.

    const bool bDataIsEndTimeStamped = true;
    bool hasParamConfig = (paramIdx < pTable.size());
    bool hasStepType = (hasParamConfig && (!pTable[paramIdx].itsStepType.empty()));
    boost::optional<long> indicatorOfTimeRange, typeOfStatisticalProcessing;

    if (hasParamConfig && (!hasStepType))
    {
      auto const &config = pTable[paramIdx];

      if (itsGrib1Flag)
      {
        if (config.itsGrib1Param)
          indicatorOfTimeRange = config.itsGrib1Param->itsIndicatorOfTimeRange;

        hasStepType = (indicatorOfTimeRange ? true : false);
      }
      else if (config.itsGrib2Param)
      {
        typeOfStatisticalProcessing = config.itsGrib2Param->itsTypeOfStatisticalProcessing;
        hasStepType = (typeOfStatisticalProcessing ? true : false);
      }
    }

    if (hasStepType)
    {
      // Aggregate period length must be the same or multiple of data time step for time steps less
      // than day
      //
      long timeStep = ((itsReqParams.timeStep > 0) ? itsReqParams.timeStep : itsDataTimeStep);

      if (timeStep <= 0)
        throw Fmi::Exception(BCP,
                               "Invalid data timestep " + boost::lexical_cast<string>(timeStep) +
                                   " for producer '" + itsReqParams.producer + "'");

      if (pTable[paramIdx].itsPeriodLengthMinutes > 0)
      {
        if (((itsDataTimeStep < minutesInDay) &&
             (pTable[paramIdx].itsPeriodLengthMinutes % itsDataTimeStep)) ||
            ((timeStep >= minutesInDay) && (pTable[paramIdx].itsPeriodLengthMinutes != timeStep)) ||
            (timeStep > minutesInMonth))
          throw Fmi::Exception(
              BCP,
              "Aggregate period length " +
                  boost::lexical_cast<string>(pTable[paramIdx].itsPeriodLengthMinutes) +
                  " min is not valid for data time step " + boost::lexical_cast<string>(timeStep) +
                  " min");

        if (timeStep < minutesInDay)
        {
          Fmi::TimeDuration td(validTime.time_of_day());
          long validTimeMinutes = (td.hours() * 60) + td.minutes();
          long periodLengthMinutes = pTable[paramIdx].itsPeriodLengthMinutes;
          long periodStartMinutes = (validTimeMinutes / periodLengthMinutes) * periodLengthMinutes;

          if (bDataIsEndTimeStamped)
          {
            // Use validtime as end step
            //
            if (periodStartMinutes == validTimeMinutes)
            {
              // Set start step backwards to the start of ending/full aggregate period
              //
              startStep = step - periodLengthMinutes;
            }
            else
            {
              // Set start step backwards to the start of current/incomplete aggregate period
              //
              startStep = step - (validTimeMinutes - periodStartMinutes);
            }
          }
          else
          {
            // Set start step to the start of current/incomplete aggregate period and advance end
            // step
            //
            startStep = step - (validTimeMinutes - periodStartMinutes);
            endStep += itsDataTimeStep;
          }
        }
      }

      if (timeStep >= minutesInDay)
      {
        // Note: For daily and monthly data aggregate period length (if given/nonzero) must equal
        // time
        // step;
        //		 we do not support cumulative aggregates
        //
        Fmi::DateTime validTimeDate = validTime.date();
        Fmi::DateTime periodStart;
        Fmi::DateTime periodEnd;

        if (bDataIsEndTimeStamped)
        {
          if (timeStep == minutesInDay)
          {
            // Previous day
            //
            periodStart = Fmi::DateTime((validTimeDate - Fmi::TimeDuration(1, 0, 0)).date());
            periodEnd = validTimeDate;
          }
          else
          {
            // Previous month
            //
            Fmi::Date d((validTimeDate - Fmi::TimeDuration(1, 0, 0)).date());
            periodStart = Fmi::DateTime(Fmi::Date(d.year(), d.month(), 1));
            periodEnd = Fmi::DateTime(Fmi::Date(
                validTimeDate.date().year(), validTimeDate.date().month(), 1));
          }
        }
        else
        {
          if (timeStep == minutesInDay)
          {
            // Current day
            //
            periodStart = validTimeDate;
            periodEnd = Fmi::DateTime((periodStart + Fmi::TimeDuration(25, 0, 0)).date());
          }
          else
          {
            // Current month
            //
            periodStart = Fmi::DateTime(Fmi::Date(
                validTimeDate.date().year(), validTimeDate.date().month(), 1));
            Fmi::DateTime t(periodStart + Fmi::TimeDuration(32 * 24, 0, 0));
            periodEnd = Fmi::DateTime(Fmi::Date(t.date().year(), t.date().month(), 1));
          }
        }

        startStep = (periodStart - itsGribOriginTime).hours() * 60;
        endStep = (periodEnd - itsGribOriginTime).hours() * 60;
      }

      if (startStep < 0)
      {
        // Can't be negative, set start step to 0 and adjust origintime and end step accordingly
        //
        itsGribOriginTime -= Fmi::TimeDuration(0, -startStep, 0);
        endStep -= startStep;
        startStep = 0;

        setOriginTime = true;
      }

      if (pTable[paramIdx].itsStepType.empty())
      {
        if (itsGrib1Flag)
          gset(itsGribHandle, "indicatorOfTimeRange", *indicatorOfTimeRange);
        else
          gset(itsGribHandle, "typeOfStatisticalProcessing", *typeOfStatisticalProcessing);
      }
      else
        gset(itsGribHandle, "stepType", pTable[paramIdx].itsStepType);
    }

    if (setOriginTime)
    {
      Fmi::Date d = itsGribOriginTime.date();
      Fmi::TimeDuration t = itsGribOriginTime.time_of_day();

      long dateLong = d.year() * 10000 + d.month() * 100 + d.day();
      long timeLong = t.hours() * 100 + t.minutes();

      gset(itsGribHandle, "date", dateLong);
      gset(itsGribHandle, "time", timeLong);
    }

    // Set time step and unit

    gset(itsGribHandle, "stepUnits", stepUnits);
    gset(itsGribHandle, "startStep", startStep);
    gset(itsGribHandle, "endStep", endStep);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Return time adjusted backwards to even timestep
 *
 */
// ----------------------------------------------------------------------

Fmi::DateTime adjustToTimeStep(const Fmi::DateTime &pt, long timeStepInMinutes)
{
  try
  {
    if (timeStepInMinutes <= 0)
      throw Fmi::Exception(BCP,
                             "adjustToTimeStep: Invalid data timestep " +
                                 boost::lexical_cast<string>(timeStepInMinutes));

    if ((timeStepInMinutes == 60) || (timeStepInMinutes == 180) || (timeStepInMinutes == 360) ||
        (timeStepInMinutes == 720))
      return Fmi::DateTime(pt.date(),
                   Fmi::TimeDuration(pt.time_of_day().hours() -
                                     (pt.time_of_day().hours() % (timeStepInMinutes / 60)),
                                 0,
                                 0));
    else if (timeStepInMinutes == DataStreamer::minutesInDay)
      return Fmi::DateTime(pt.date(), Fmi::TimeDuration(0, 0, 0));
    else if (timeStepInMinutes == DataStreamer::minutesInMonth)
      return Fmi::DateTime(Fmi::Date(pt.date().year(), pt.date().month(), 1),
                   Fmi::TimeDuration(0, 0, 0));

    return pt;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Copy data (one level/param/time grid) into grib buffer
 *
 */
// ----------------------------------------------------------------------

void GribStreamer::addValuesToGrib(Engine::Querydata::Q q,
                                   const NFmiMetTime &vTime,
                                   int level,
                                   const NFmiDataMatrix<float> &dataValues,
                                   float scale,
                                   float offset)
{
  try
  {
    // Set named configuration settings

    setNamedSettingsToGrib();

    // Use first validtime as origintime if it is earlier than the origintime.
    //
    // Note: originTime is unset (is_not_a_date_time()) when called for first time instant.
    //
    //		 If the actual data origintime is used, adjust it backwards to even data timestep;
    //		 the output validtimes are set as number of timesteps forwards from the origintime.

    Fmi::DateTime oTime = q->originTime();
    Fmi::DateTime validTime = vTime;
    bool setOriginTime = (itsOriginTime.is_not_a_date_time() || (itsOriginTime != oTime));

    if (setOriginTime)
    {
      // Set origintime
      //
      itsOriginTime = oTime;
      itsGribOriginTime =
          ((validTime < itsOriginTime) ? validTime
                                       : adjustToTimeStep(itsOriginTime, itsDataTimeStep));
    }

    // Set level and parameter. Parameter's index in 'ptable' is returned in paramIdx (needed in
    // setStep())

    NFmiParam param(*(q->param().GetParam()));
    const ParamChangeTable &pTable = itsCfg.getParamChangeTable();
    size_t paramIdx = pTable.size();

    setLevelAndParameterToGrib(level, param, "", pTable, paramIdx);

    // Set start and end step and step type (for average, cumulative etc. data)

    setStepToGrib(pTable, paramIdx, setOriginTime, validTime);

    // Load the data, cropping the grid/values it if manual cropping is set

    bool cropxy = (itsCropping.cropped && itsCropping.cropMan);
    size_t x0 = (cropxy ? itsCropping.bottomLeftX : 0), y0 = (cropxy ? itsCropping.bottomLeftY : 0);
    size_t xN = (itsCropping.cropped ? (x0 + itsCropping.gridSizeX) : itsReqGridSizeX);
    size_t yN = (itsCropping.cropped ? (y0 + itsCropping.gridSizeY) : itsReqGridSizeY);

    size_t xStep = (itsReqParams.gridStepXY ? (*(itsReqParams.gridStepXY))[0].first : 1),
           yStep = (itsReqParams.gridStepXY ? (*(itsReqParams.gridStepXY))[0].second : 1), x, y;
    int i = 0;

    for (y = y0; (y < yN); y += yStep)
      for (x = x0; (x < xN); x += xStep, i++)
      {
        float value = dataValues[x][y];

        if (value != kFloatMissing)
          itsValueArray[i] = (value + offset) / scale;
        else
          itsValueArray[i] = gribMissingValue;
      }

    grib_set_double_array(itsGribHandle, "values", &itsValueArray[0], itsValueArray.size());
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Copy grid data (one level/param/time grid) into grib buffer
 *
 */
// ----------------------------------------------------------------------

void GribStreamer::addGridValuesToGrib(const QueryServer::Query &gridQuery,
                                       const NFmiMetTime &vTime,
                                       int level,
                                       float scale,
                                       float offset)
{
  try
  {
    // Set named configuration settings

    setNamedSettingsToGrib();

    // Use first validtime as origintime if it is earlier than the origintime.
    //
    // Note: originTime is unset (is_not_a_date_time()) when called for first time instant.
    //
    //		 If the actual data origintime is used, adjust it backwards to even data timestep;
    //		 the output validtimes are set as number of timesteps forwards from the origintime.
    //
    // For function parameters analysistime is available only if set in the request since the
    // parameters can have multiple producers and their latest origintime may not be the same
    // and/or the function may return data originating from multiple generations, or if the
    // query includes data parameters too in which case latest valid origintime is selected from
    // content records prior the data query.
    //
    // Grid -engine could (should ?) return analysistime if there's only one function
    // parameter or all parameters have the same producer, and the input data originates from
    // single generation

    Fmi::DateTime oTime = itsGridMetaData.gridOriginTime, validTime = vTime;

    if (oTime.is_not_a_date_time())
      // Query has function parameter(s) only, use each validtime as origintime too
      //
      oTime = vTime;

    bool setOriginTime = (itsOriginTime.is_not_a_date_time() || (itsOriginTime != oTime));

    if (setOriginTime)
    {
      // Set origintime
      //
      itsOriginTime = oTime;
      itsGribOriginTime =
          ((validTime < itsOriginTime) ? validTime
                                       : adjustToTimeStep(itsOriginTime, itsDataTimeStep));
    }

    // Set level and parameter. Parameter's index in 'ptable' is returned in paramIdx (needed in
    // setStep())

    NFmiParam param(itsParamIterator->number());
    const ParamChangeTable &pTable = itsCfg.getParamChangeTable();
    size_t paramIdx = pTable.size();

    setLevelAndParameterToGrib(level, param, itsParamIterator->name(), pTable, paramIdx);

    // Set start and end step and step type (for average, cumulative etc. data)

    setStepToGrib(pTable, paramIdx, setOriginTime, validTime);

    // Load the data, cropping the grid/values it if manual cropping is set

    bool cropxy = (itsCropping.cropped && itsCropping.cropMan);
    size_t x0 = (cropxy ? itsCropping.bottomLeftX : 0), y0 = (cropxy ? itsCropping.bottomLeftY : 0);
    size_t xN = (itsCropping.cropped ? (x0 + itsCropping.gridSizeX) : itsReqGridSizeX),
           yN = (itsCropping.cropped ? (y0 + itsCropping.gridSizeY) : itsReqGridSizeY);

    size_t xStep = (itsReqParams.gridStepXY ? (*(itsReqParams.gridStepXY))[0].first : 1),
           yStep = (itsReqParams.gridStepXY ? (*(itsReqParams.gridStepXY))[0].second : 1), x, y;
    int i = 0;

    const auto vVec = &(getValueListItem(gridQuery)->mValueVector);

    if (itsReqParams.dataSource == GridContent)
    {
      // No scaling applied

      for (y = y0; (y < yN); y += yStep)
      {
        int j = y * xN;

        for (x = x0; (x < xN); x += xStep, j += xStep, i++)
        {
          float value = (*vVec)[j];
          itsValueArray[i] = ((value != ParamValueMissing) ? value : gribMissingValue);
        }
      }
    }
    else
    {
      for (y = y0; (y < yN); y += yStep)
      {
        int j = y * xN;

        for (x = x0; (x < xN); x += xStep, j += xStep, i++)
        {
          float value = (*vVec)[j];

          if (value != ParamValueMissing)
            itsValueArray[i] = (value + offset) / scale;
          else
            itsValueArray[i] = gribMissingValue;
        }
      }
    }

    grib_set_double_array(itsGribHandle, "values", &itsValueArray[0], itsValueArray.size());
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Add given data values and return complete grib message
 */
// ----------------------------------------------------------------------

string GribStreamer::getGribMessage(Engine::Querydata::Q q,
                                    int level,
                                    const NFmiMetTime &mt,
                                    const NFmiDataMatrix<float> &values,
                                    float scale,
                                    float offset)
{
  try
  {
    addValuesToGrib(q, mt, level, values, scale, offset);

    const void *mesg;
    size_t mesg_len;
    grib_get_message(itsGribHandle, &mesg, &mesg_len);

    if (mesg_len == 0)
      throw Fmi::Exception(BCP, "Empty grib message returned");

    return string((const char *)mesg, mesg_len);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Add given grid data values and return complete grib message
 */
// ----------------------------------------------------------------------

string GribStreamer::getGridGribMessage(const QueryServer::Query &gridQuery,
                                        int level,
                                        const NFmiMetTime &mt,
                                        float scale,
                                        float offset)
{
  try
  {
    addGridValuesToGrib(gridQuery, mt, level, scale, offset);

    const void *mesg;
    size_t mesg_len;
    grib_get_message(itsGribHandle, &mesg, &mesg_len);

    if (mesg_len == 0)
      throw Fmi::Exception(BCP, "Empty grib message returned");

    return string((const char *)mesg, mesg_len);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Get next chunk of data. Called from SmartMet server code
 *
 */
// ----------------------------------------------------------------------

std::string GribStreamer::getChunk()
{
  try
  {
    try
    {
      ostringstream chunkBuf;
      string chunk;
      size_t chunkBufLength = 0, nChunks = 0;

      while (!itsDoneFlag)
      {
        // Get next chunk e.g. next param/level/validtime grid
        //
        extractData(chunk);
        nChunks++;

        if (chunk.empty())
          itsDoneFlag = true;
        else
          chunkBufLength += chunk.length();

        // To avoid small chunk transfer overhead collect chunks until max chunk length or max count
        // of collected chunks is reached

        if (itsDoneFlag || (nChunks >= itsMaxMsgChunks) || (chunkBufLength >= itsChunkLength))
        {
          if (itsDoneFlag)
            setStatus(ContentStreamer::StreamerStatus::EXIT_OK);

          if (nChunks > 1)
          {
            chunkBuf << chunk;
            return chunkBuf.str();
          }

          return chunk;
        }

        chunkBuf << chunk;
      }

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

// ----------------------------------------------------------------------
/*!
 * \brief Load chunk of data; called by DataStreamer to get format specific chunk.
 *
 */
// ----------------------------------------------------------------------

void GribStreamer::getDataChunk(Engine::Querydata::Q q,
                                const NFmiArea *area,
                                NFmiGrid * /* grid */,
                                int level,
                                const NFmiMetTime &mt,
                                NFmiDataMatrix<float> &values,
                                string &chunk)
{
  try
  {
    if (itsMetaFlag)
    {
      // Set geometry
      //
      setGeometryToGrib(area, q->isRelativeUV());
      itsMetaFlag = false;
    }

    // Build and get grib message

    chunk =
        getGribMessage(q, level, mt, values, itsScalingIterator->first, itsScalingIterator->second);
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

void GribStreamer::getGridDataChunk(const QueryServer::Query &gridQuery,
                                    int level,
                                    const NFmiMetTime &mt,
                                    string &chunk)
{
  try
  {
    if (itsMetaFlag)
    {
      // Set geometry
      //
      setGridGeometryToGrib(gridQuery);
      itsMetaFlag = (itsReqParams.dataSource == GridMapping);
    }

    // Build and get grib message

    chunk = getGridGribMessage(
        gridQuery, level, mt, itsScalingIterator->first, itsScalingIterator->second);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

}  // namespace Download
}  // namespace Plugin
}  // namespace SmartMet

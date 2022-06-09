#include "Resources.h"
#include <boost/stacktrace.hpp>
#include <newbase/NFmiAreaFactory.h>
#include <macgyver/Exception.h>

namespace SmartMet
{
namespace Plugin
{
namespace Download
{
Resources::~Resources()
{
  // Delete coordinate transformations
  //
  for (OGRCoordinateTransformation *ct : transformations)
  {
    OGRCoordinateTransformation::DestroyCT(ct);
  }

  // Delete cloned srs:s
  //
  // Note: If geometrySRS is nonnull, the object pointed by it gets deleted too
  //
  for (OGRSpatialReference *srs : spatialReferences)
  {
    OGRSpatialReference::DestroySpatialReference(srs);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Create area with given projection string
 */
// ----------------------------------------------------------------------

const NFmiArea *Resources::createArea(const std::string &projection)
{
  try
  {
    auto area = NFmiAreaFactory::Create(projection);

    if (!area.get())
      throw Fmi::Exception(BCP, "Could not create projection '" + projection + "'");

    areas.push_back(area);

    return area.get();
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Get current projected area object
 */
// ----------------------------------------------------------------------

const NFmiArea *Resources::getArea()
{
  try
  {
    return areas.empty() ? nullptr : areas.back().get();
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}
// ----------------------------------------------------------------------
/*!
 * \brief (Re)create grid
 */
// ----------------------------------------------------------------------

void Resources::createGrid(const NFmiArea &a, size_t gridSizeX, size_t gridSizeY)
{
  try
  {
    grid.reset(new NFmiGrid(&a, gridSizeX, gridSizeY));

    if (!grid.get())
      throw Fmi::Exception(BCP, "Internal: could not create grid");
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Check if suitable grid exists
 */
// ----------------------------------------------------------------------

bool Resources::hasGrid(const NFmiArea &a, size_t gridSizeX, size_t gridSizeY)
{
  try
  {
    NFmiGrid *g = grid.get();
    const NFmiArea *ga = (g ? g->Area() : nullptr);

    if (!(ga && (ga->ClassId() == a.ClassId()) && (g->XNumber() == gridSizeX) &&
          (g->YNumber() == gridSizeY)))
    {
      return false;
    }

    return true;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Return current grid if it (exists and) matches the area and
 * 	    gridsize given. Otherwise the grid is (re)created.
 */
// ----------------------------------------------------------------------

NFmiGrid *Resources::getGrid(const NFmiArea &a, size_t gridSizeX, size_t gridSizeY)
{
  try
  {
    if (!hasGrid(a, gridSizeX, gridSizeY))
      createGrid(a, gridSizeX, gridSizeY);

    return grid.get();
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Clone spatial reference
 */
// ----------------------------------------------------------------------

OGRSpatialReference *Resources::cloneCS(const OGRSpatialReference &SRS, bool isGeometrySRS)
{
  try
  {
    OGRSpatialReference *srs = SRS.Clone();

    if (srs)
    {
      spatialReferences.push_back(srs);

      if (isGeometrySRS)
        geometrySRS = srs;
    }

    return srs;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Clone geographic spatial reference
 */
// ----------------------------------------------------------------------

OGRSpatialReference *Resources::cloneGeogCS(const OGRSpatialReference &SRS, bool isGeometrySRS)
{
  try
  {
    OGRSpatialReference *srs = SRS.CloneGeogCS();

    if (srs)
    {
      spatialReferences.push_back(srs);

      if (isGeometrySRS)
        geometrySRS = srs;
    }

    return srs;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Get coordinate transformation
 */
// ----------------------------------------------------------------------

OGRCoordinateTransformation *Resources::getCoordinateTransformation(OGRSpatialReference *fromSRS,
                                                                    OGRSpatialReference *toSRS,
                                                                    bool isGeometrySRS)
{
  try
  {
    OGRCoordinateTransformation *ct = OGRCreateCoordinateTransformation(fromSRS, toSRS);

    if (ct)
    {
      // Store the target srs if output geometry will be set from it (instead of using qd's area)
      //
      if (isGeometrySRS)
      {
        OGRSpatialReference *srs;

        if (!(srs = toSRS->Clone()))
          throw Fmi::Exception(BCP,
                                 "getCoordinateTransformation: OGRSpatialReference cloning failed");
        else
          spatialReferences.push_back(srs);

        geometrySRS = srs;
      }

      transformations.push_back(ct);
    }

    return ct;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

}  // namespace Download
}  // namespace Plugin
}  // namespace SmartMet

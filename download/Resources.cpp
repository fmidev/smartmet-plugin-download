#include "Resources.h"
#include <boost/stacktrace.hpp>
#include <newbase/NFmiAreaFactory.h>
#include <spine/Exception.h>

namespace SmartMet
{
namespace Plugin
{
namespace Download
{
// ----------------------------------------------------------------------
/*!
 * \brief Create area with given projection string
 */
// ----------------------------------------------------------------------

void Resources::createArea(const std::string &projection,
                           const NFmiPoint &bottomLeft,
                           const NFmiPoint &topRight)
{
  try
  {
#if 1
    std::cout << "Creating proj " << projection << " with\nBL = " << bottomLeft
              << "TR = " << topRight << "\n";
    std::cout << boost::stacktrace::stacktrace();
#endif

    area = NFmiAreaFactory::CreateFromCorners(projection, bottomLeft, topRight);

    if (!area.get())
      throw Spine::Exception(BCP, "Could not create projection '" + projection + "'");
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Create area with given projection string
 */
// ----------------------------------------------------------------------

void Resources::createArea(const std::string &projection,
                           const NFmiPoint &center,
                           double width,
                           double height)
{
  try
  {
    area = NFmiAreaFactory::CreateFromCenter(projection, center, width, height);

    if (!area.get())
      throw Spine::Exception(BCP, "Could not create projection '" + projection + "'");
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
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
    return area.get();
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
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
      throw Spine::Exception(BCP, "Internal: could not create grid");
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
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
    throw Spine::Exception::Trace(BCP, "Operation failed!");
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
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Clone spatial reference
 */
// ----------------------------------------------------------------------

OGRSpatialReference *Resources::cloneCS(const OGRSpatialReference &SRS)
{
  try
  {
    OGRSpatialReference *srs = SRS.Clone();

    if (srs)
      spatialReferences.push_back(srs);

    return srs;
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Clone geographic spatial reference
 */
// ----------------------------------------------------------------------

OGRSpatialReference *Resources::cloneGeogCS(const OGRSpatialReference &SRS)
{
  try
  {
    OGRSpatialReference *srs = SRS.CloneGeogCS();

    if (srs)
      spatialReferences.push_back(srs);

    return srs;
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
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
        if (!(geometrySRS = toSRS->Clone()))
          throw Spine::Exception(BCP,
                                 "getCoordinateTransformation: OGRSpatialReference cloning failed");
        else
          spatialReferences.push_back(geometrySRS);
      }

      transformations.push_back(ct);
    }

    return ct;
  }
  catch (...)
  {
    throw Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

}  // namespace Download
}  // namespace Plugin
}  // namespace SmartMet

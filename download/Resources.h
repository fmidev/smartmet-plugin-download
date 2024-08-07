#pragma once

#include <memory>
#include <newbase/NFmiArea.h>
#include <newbase/NFmiGrid.h>
#include <newbase/NFmiPoint.h>
#include <list>
#include <ogr_spatialref.h>
#include <string>

// Resource management
//
// Resources class is the sole owner and thus responsible for releasing *ALL* objects created by
// calling its methods:
//
//      createArea()                       NFmiArea object
//      getGrid()                          NFmiGrid object
//      cloneGeogCS(), cloneCS()           OGRSpatialReference objects
//      getCoordinateTransformation()      OGRCoordinateTransformation objects
//
// Only one area and/or grid can exist at a given time; old object is released if a new object is
// created.
//
// Note: In download plugin implementation area is created only once (if at all) per processed
// query. Multiple grid's will
// be created during execution of a query if the query spans over multiple querydatas, native
// gridsize or given gridresolution
// is used and data gridsize changes.
//
// Note: OGRSpatialReference object pointed by geometrySRS (if nonnull) is one of the objects in
// 'spatialReferences' list;
// the object is *NOT* released using the geometrySRS pointer.

namespace SmartMet
{
namespace Plugin
{
namespace Download
{
class Resources
{
 public:
  Resources() = default;
  Resources(const Resources &other) = delete;
  Resources &operator=(const Resources &other) = delete;
  ~Resources();

  const NFmiArea *createArea(const std::string &projection);
  const NFmiArea *getArea();

  NFmiGrid *getGrid(const NFmiArea &a, std::size_t gsX, std::size_t gsY);
  NFmiGrid *getGrid() const { return grid.get(); }
  OGRSpatialReference *cloneGeogCS(const OGRSpatialReference &, bool isGeometrySRS = false);
  OGRSpatialReference *cloneCS(const OGRSpatialReference &, bool isGeometrySRS = false);
  OGRCoordinateTransformation *getCoordinateTransformation(OGRSpatialReference *,
                                                           OGRSpatialReference *,
                                                           bool isGeometrySRS = false);
  OGRSpatialReference *getGeometrySRS() const { return geometrySRS; }

 private:
  std::list<std::shared_ptr<NFmiArea>> areas;
  std::shared_ptr<NFmiGrid> grid;
  std::list<OGRSpatialReference *> spatialReferences;
  std::list<OGRCoordinateTransformation *> transformations;
  OGRSpatialReference *geometrySRS = nullptr;

  void createGrid(const NFmiArea &a, std::size_t gsX, std::size_t gsY);
  bool hasGrid(const NFmiArea &a, std::size_t gsX, std::size_t gsY);
};

}  // namespace Download
}  // namespace Plugin
}  // namespace SmartMet

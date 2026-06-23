// ======================================================================
/*!
 * \brief SmartMet download service plugin; GeoTIFF streaming
 */
// ======================================================================

#include "GeoTiffStreamer.h"
#include <boost/lexical_cast.hpp>
#include <gis/SpatialReference.h>
#include <macgyver/Exception.h>
#include <macgyver/StringConversion.h>
#include <newbase/NFmiMetTime.h>
#include <spine/Thread.h>
#include <cpl_conv.h>
#include <gdal_priv.h>
#include <unistd.h>

namespace
{
// GDAL driver registration and GTiff file creation are not guaranteed thread safe
SmartMet::Spine::MutexType myGdalMutex;
}  // namespace

using namespace std;

namespace SmartMet
{
namespace Plugin
{
namespace Download
{
GeoTiffStreamer::GeoTiffStreamer(const Spine::HTTP::Request& req,
                                 const Config& config,
                                 const Query& query,
                                 const Producer& producer,
                                 const ReqParams& reqParams)
    : DataStreamer(req, config, query, producer, reqParams),
      itsFilename(config.getTempDirectory() + "/dls_" + boost::lexical_cast<string>((int)getpid()) +
                  "_" + boost::lexical_cast<string>(boost::this_thread::get_id()) + ".tif")
{
}

GeoTiffStreamer::~GeoTiffStreamer()
{
  if (itsStream.is_open())
    itsStream.close();

  unlink(itsFilename.c_str());
}

// ----------------------------------------------------------------------
/*!
 * \brief Get next chunk of data. Called from SmartMet server code
 *
 *    The data is first collected into raster bands (one per slice). When
 *    extraction is complete the multi-band GeoTIFF is written to a temp
 *    file which is then streamed in chunks.
 */
// ----------------------------------------------------------------------

std::string GeoTiffStreamer::getChunk()
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
          // Collect all slices into raster bands. 'chunk' serves only as 'end of data' indicator;
          // the actual values are read from 'itsGridValues' (querydata) or the grid query result.

          do
          {
            extractData(chunk);

            if (chunk.empty())
              itsLoadedFlag = true;
            else
              storeBand();
          } while (!itsLoadedFlag);

          if (itsBands.empty())
            throw Fmi::Exception(BCP, "No data to write to GeoTIFF");

          writeFile();

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

// ----------------------------------------------------------------------
/*!
 * \brief Capture target grid geometry (geotransform and projection) from
 *        the first querydata slice.
 */
// ----------------------------------------------------------------------

void GeoTiffStreamer::captureGeometry(Engine::Querydata::Q q,
                                      const NFmiArea* area,
                                      const NFmiGrid* grid)
{
  try
  {
    itsWidth = itsNX;
    itsHeight = itsNY;

    if ((itsWidth < 1) || (itsHeight < 1))
      throw Fmi::Exception(BCP, "Invalid output grid size for GeoTIFF");

    if (!grid)
      grid = &q->grid();

    int classId =
        (itsReqParams.areaClassId != A_Native) ? (int)itsReqParams.areaClassId : area->ClassId();
    bool projected = (classId != kNFmiLatLonArea);
    bool datumShift = (itsReqParams.datumShift != Datum::DatumShift::None);

    size_t x0 = (itsCropping.cropped ? itsCropping.bottomLeftX : 0),
           y0 = (itsCropping.cropped ? itsCropping.bottomLeftY : 0);
    size_t xN = (itsCropping.cropped ? (x0 + itsCropping.gridSizeX) : itsReqGridSizeX),
           yN = (itsCropping.cropped ? (y0 + itsCropping.gridSizeY) : itsReqGridSizeY);
    size_t xStep = (itsReqParams.gridStepXY ? (*(itsReqParams.gridStepXY))[0].first : 1),
           yStep = (itsReqParams.gridStepXY ? (*(itsReqParams.gridStepXY))[0].second : 1);

    // Corner cell-center coordinates in the target spatial reference. For latlon output the
    // "world" coordinates are lon/lat degrees, otherwise projected metres.

    auto cornerXY = [&](size_t x, size_t y) -> NFmiPoint
    {
      if (projected)
        return datumShift ? itsTargetWorldXYs(x, y) : grid->GridToWorldXY(x, y);
      return datumShift ? NFmiPoint(itsTargetLatLons.x(x, y), itsTargetLatLons.y(x, y))
                        : grid->GridToLatLon(x, y);
    };

    NFmiPoint p0 = cornerXY(x0, y0);
    NFmiPoint pN = cornerXY(xN - 1, yN - 1);

    double stepX = xStep * ((itsWidth > 1) ? ((pN.X() - p0.X()) / (xN - x0 - 1)) : 0.0);
    double stepY = yStep * ((itsHeight > 1) ? ((pN.Y() - p0.Y()) / (yN - y0 - 1)) : 0.0);

    double pixelW = (stepX != 0) ? std::fabs(stepX) : 1.0;
    double pixelH = (stepY != 0) ? std::fabs(stepY) : 1.0;

    // GeoTIFF is north-up: row 0 is the northernmost row. Newbase grids normally run south to
    // north (stepY > 0), so the rows are flipped when storing band values.

    itsFlipY = (stepY >= 0);

    double yTop = (stepY >= 0) ? (p0.Y() + (itsHeight - 1) * std::fabs(stepY)) : p0.Y();
    double xLeft = (stepX >= 0) ? p0.X() : (p0.X() - (itsWidth - 1) * std::fabs(stepX));

    itsGeoTransform[0] = xLeft - pixelW / 2;  // x of top-left corner of top-left pixel
    itsGeoTransform[1] = pixelW;              // pixel width
    itsGeoTransform[2] = 0;
    itsGeoTransform[3] = yTop + pixelH / 2;  // y of top-left corner of top-left pixel
    itsGeoTransform[4] = 0;
    itsGeoTransform[5] = -pixelH;  // pixel height (negative, north-up)

    // Projection WKT. When reprojecting via an EPSG code or datum shift the resource manager
    // holds the actual target spatial reference (e.g. EPSG:3067), so prefer it. Only genuine
    // newbase YKJ output (projection=ykj, no target SRS) falls back to the hardcoded EPSG:2393.

    OGRSpatialReference* geometrySRS = itsResources.getGeometrySRS();

    if (geometrySRS)
      itsProjectionWKT = getWKT(geometrySRS);
    else if (classId == kNFmiYKJArea)
      itsProjectionWKT = getWKT(Fmi::SpatialReference(2393).get());
    else
      itsProjectionWKT = area->WKT();

    itsMissingValue =
        (itsReqParams.dataSource == QueryData) ? kFloatMissing : (float)gribMissingValue;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Capture target grid geometry from the first grid (engine) slice.
 */
// ----------------------------------------------------------------------

void GeoTiffStreamer::captureGridGeometry(const QueryServer::Query& gridQuery)
{
  try
  {
    itsWidth = itsNX;
    itsHeight = itsNY;

    if ((itsWidth < 1) || (itsHeight < 1))
      throw Fmi::Exception(BCP, "Invalid output grid size for GeoTIFF");

    bool projected = ((itsGridMetaData.projType != T::GridProjectionValue::LatLon) &&
                      (itsGridMetaData.projType != T::GridProjectionValue::RotatedLatLon));

    if (itsGridMetaData.projType == T::GridProjectionValue::RotatedLatLon)
      throw Fmi::Exception(BCP, "GeoTIFF output is not supported for rotated latlon geometry");

    size_t xN = itsReqGridSizeX, yN = itsReqGridSizeY;
    size_t xStep = (itsReqParams.gridStepXY ? (*(itsReqParams.gridStepXY))[0].first : 1),
           yStep = (itsReqParams.gridStepXY ? (*(itsReqParams.gridStepXY))[0].second : 1);

    OGRSpatialReference* geometrySRS = itsResources.getGeometrySRS();

    if (!geometrySRS)
      throw Fmi::Exception(BCP, "Grid geometry spatial reference is not set");

    const auto& coords = gridQuery.mQueryParameterList.front().mCoordinates;

    if (coords.size() != (xN * yN))
      throw Fmi::Exception(BCP,
                           "Number of coordinates (" + Fmi::to_string(coords.size()) +
                               ") and grid size (" + Fmi::to_string(xN) + "/" + Fmi::to_string(yN) +
                               ") mismatch");

    // Corner cell-center coordinates in the target spatial reference. The grid query coordinates
    // are geographic (lon/lat); for projected output they are transformed to the target CRS.

    NFmiPoint p0, pN;

    if (projected)
    {
      OGRSpatialReference llSRS;
      llSRS.CopyGeogCSFrom(geometrySRS);

      OGRCoordinateTransformation* ct =
          itsResources.getCoordinateTransformation(&llSRS, geometrySRS);

      double xc[] = {coords[0].x(), coords[coords.size() - 1].x()};
      double yc[] = {coords[0].y(), coords[coords.size() - 1].y()};
      int pabSuccess[2];

      if (!(ct->Transform(2, xc, yc, nullptr, pabSuccess) && pabSuccess[0] && pabSuccess[1]))
        throw Fmi::Exception(BCP, "Failed to transform grid corner coordinates");

      p0 = NFmiPoint(xc[0], yc[0]);
      pN = NFmiPoint(xc[1], yc[1]);
    }
    else
    {
      p0 = NFmiPoint(coords[0].x(), coords[0].y());
      pN = NFmiPoint(coords[coords.size() - 1].x(), coords[coords.size() - 1].y());
    }

    double stepX = xStep * ((itsWidth > 1) ? ((pN.X() - p0.X()) / (xN - 1)) : 0.0);
    double stepY = yStep * ((itsHeight > 1) ? ((pN.Y() - p0.Y()) / (yN - 1)) : 0.0);

    double pixelW = (stepX != 0) ? std::fabs(stepX) : 1.0;
    double pixelH = (stepY != 0) ? std::fabs(stepY) : 1.0;

    itsFlipY = (stepY >= 0);

    double yTop = (stepY >= 0) ? (p0.Y() + (itsHeight - 1) * std::fabs(stepY)) : p0.Y();
    double xLeft = (stepX >= 0) ? p0.X() : (p0.X() - (itsWidth - 1) * std::fabs(stepX));

    itsGeoTransform[0] = xLeft - pixelW / 2;
    itsGeoTransform[1] = pixelW;
    itsGeoTransform[2] = 0;
    itsGeoTransform[3] = yTop + pixelH / 2;
    itsGeoTransform[4] = 0;
    itsGeoTransform[5] = -pixelH;

    itsProjectionWKT = getWKT(geometrySRS);
    itsMissingValue = (float)gribMissingValue;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Store the current slice's values as a north-up raster band.
 */
// ----------------------------------------------------------------------

void GeoTiffStreamer::storeBand()
{
  try
  {
    bool cropxy = (itsCropping.cropped && itsCropping.cropMan);
    size_t x0 = (cropxy ? itsCropping.bottomLeftX : 0), y0 = (cropxy ? itsCropping.bottomLeftY : 0);
    size_t xN = (itsCropping.cropped ? (x0 + itsCropping.gridSizeX) : itsReqGridSizeX);
    size_t xStep = (itsReqParams.gridStepXY ? (*(itsReqParams.gridStepXY))[0].first : 1),
           yStep = (itsReqParams.gridStepXY ? (*(itsReqParams.gridStepXY))[0].second : 1);

    Band band;
    band.values.resize(itsWidth * itsHeight);
    band.paramName = itsCurrentParamName;
    band.validTime = itsCurrentValidTime;
    band.level = itsCurrentLevel;
    band.hasLevel = itsCurrentHasLevel;

    string lvl = (band.hasLevel ? (" level=" + Fmi::to_string(band.level)) : "");
    band.description = band.paramName + lvl + " " + band.validTime;

    bool isQueryData = (itsReqParams.dataSource == QueryData);
    bool gridContent = (itsReqParams.dataSource == GridContent);

    const std::vector<float>* vVec = nullptr;
    if (!isQueryData)
      vVec = &(getValueListItem(itsGridQuery)->mValueVector);

    for (size_t r = 0; r < itsHeight; r++)
    {
      // GeoTIFF row r (0 = north). Map to the source/output row index (0 = south when flipping).
      size_t j = itsFlipY ? (itsHeight - 1 - r) : r;
      size_t srcY = y0 + j * yStep;
      float* out = &band.values[r * itsWidth];

      for (size_t i = 0; i < itsWidth; i++)
      {
        size_t srcX = x0 + i * xStep;
        float value;

        if (isQueryData)
        {
          value = itsGridValues[srcX][srcY];

          if (value != kFloatMissing)
            value = (value + itsScalingIterator->second) / itsScalingIterator->first;
        }
        else
        {
          auto c = (srcY * xN) + srcX;
          value = (*vVec)[c];

          if (value != ParamValueMissing)
          {
            if (!gridContent)
              value = (value + itsScalingIterator->second) / itsScalingIterator->first;
          }
          else
            value = itsMissingValue;
        }

        out[i] = value;
      }
    }

    itsBands.push_back(std::move(band));
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Write the collected raster bands into a multi-band GeoTIFF file.
 */
// ----------------------------------------------------------------------

void GeoTiffStreamer::writeFile()
{
  try
  {
    Spine::WriteLock lock(myGdalMutex);

    GDALAllRegister();

    GDALDriver* driver = GetGDALDriverManager()->GetDriverByName("GTiff");
    if (!driver)
      throw Fmi::Exception(BCP, "GDAL GTiff driver is not available");

    const char* creationOptions[] = {"COMPRESS=DEFLATE", "PREDICTOR=3", nullptr};

    GDALDataset* ds = driver->Create(itsFilename.c_str(),
                                     (int)itsWidth,
                                     (int)itsHeight,
                                     (int)itsBands.size(),
                                     GDT_Float32,
                                     const_cast<char**>(creationOptions));
    if (!ds)
      throw Fmi::Exception(BCP, "Failed to create GeoTIFF file " + itsFilename);

    try
    {
      ds->SetGeoTransform(itsGeoTransform);

      if (!itsProjectionWKT.empty())
        ds->SetProjection(itsProjectionWKT.c_str());

      int bandNumber = 1;

      for (const auto& band : itsBands)
      {
        GDALRasterBand* rb = ds->GetRasterBand(bandNumber++);

        rb->SetNoDataValue(itsMissingValue);
        rb->SetDescription(band.description.c_str());
        rb->SetMetadataItem("PARAMETER", band.paramName.c_str());
        rb->SetMetadataItem("TIME", band.validTime.c_str());
        if (band.hasLevel)
          rb->SetMetadataItem("LEVEL", Fmi::to_string(band.level).c_str());

        CPLErr err = rb->RasterIO(GF_Write,
                                  0,
                                  0,
                                  (int)itsWidth,
                                  (int)itsHeight,
                                  const_cast<float*>(band.values.data()),
                                  (int)itsWidth,
                                  (int)itsHeight,
                                  GDT_Float32,
                                  0,
                                  0);
        if (err != CE_None)
          throw Fmi::Exception(BCP, "Failed to write GeoTIFF raster band");
      }
    }
    catch (...)
    {
      GDALClose(ds);
      throw;
    }

    GDALClose(ds);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Load chunk of querydata; called by DataStreamer for each slice.
 */
// ----------------------------------------------------------------------

void GeoTiffStreamer::getDataChunk(Engine::Querydata::Q q,
                                   const NFmiArea* area,
                                   NFmiGrid* grid,
                                   int level,
                                   const NFmiMetTime& mt,
                                   NFmiDataMatrix<float>& /* values */,
                                   string& chunk)
{
  try
  {
    if (itsMetaFlag)
    {
      captureGeometry(q, area, grid);
      itsMetaFlag = false;
    }

    itsCurrentParamName = itsParamIterator->name();
    itsCurrentValidTime = Fmi::to_iso_string(mt.PosixTime());
    itsCurrentLevel = level;
    itsCurrentHasLevel = !isSurfaceLevel(itsLevelType);

    // Values are read from 'itsGridValues' by storeBand(); set nonempty chunk to indicate data.

    chunk = " ";
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Load chunk of grid data; called by DataStreamer for each slice.
 */
// ----------------------------------------------------------------------

void GeoTiffStreamer::getGridDataChunk(const QueryServer::Query& gridQuery,
                                       int level,
                                       const NFmiMetTime& mt,
                                       string& chunk)
{
  try
  {
    if (itsMetaFlag)
    {
      captureGridGeometry(gridQuery);
      itsMetaFlag = false;
    }

    itsCurrentParamName = itsParamIterator->name();
    itsCurrentValidTime = Fmi::to_iso_string(mt.PosixTime());
    itsCurrentLevel = level;
    itsCurrentHasLevel = true;

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

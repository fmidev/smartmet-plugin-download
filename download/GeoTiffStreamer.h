// ======================================================================
/*!
 * \brief SmartMet download service plugin; GeoTIFF streaming
 *
 *    Each extracted param/time/level slice is written as a separate
 *    raster band into a single multi-band GeoTIFF. The complete file is
 *    built into a temporary file and then streamed in chunks, mirroring
 *    the NetCDF streamer.
 */
// ======================================================================

#pragma once

#include "DataStreamer.h"
#include <fstream>
#include <string>
#include <vector>

namespace SmartMet
{
namespace Plugin
{
namespace Download
{
class GeoTiffStreamer : public DataStreamer
{
 public:
  GeoTiffStreamer(const Spine::HTTP::Request& req,
                  const Config& config,
                  const Query& query,
                  const Producer& producer,
                  const ReqParams& reqParams);
  virtual ~GeoTiffStreamer();

  virtual std::string getChunk();

  virtual void getDataChunk(Engine::Querydata::Q q,
                            const NFmiArea* area,
                            NFmiGrid* grid,
                            int level,
                            const NFmiMetTime& mt,
                            NFmiDataMatrix<float>& values,
                            std::string& chunk);

  // Grid support
  //
  virtual void getGridDataChunk(const QueryServer::Query& gridQuery,
                                int level,
                                const NFmiMetTime& mt,
                                std::string& chunk);

 private:
  GeoTiffStreamer();

  // One raster band (a single param/time/level slice)
  struct Band
  {
    std::vector<float> values;  // north-up, row major (size itsWidth * itsHeight)
    std::string description;
    std::string paramName;
    std::string validTime;
    int level = 0;
    bool hasLevel = false;
  };

  void captureGeometry(Engine::Querydata::Q q, const NFmiArea* area, const NFmiGrid* grid);
  void captureGridGeometry(const QueryServer::Query& gridQuery);
  void storeBand();
  void writeFile();

  std::string itsFilename;
  std::ifstream itsStream;
  bool itsLoadedFlag = false;

  // Geometry captured from the first slice
  std::size_t itsWidth = 0;
  std::size_t itsHeight = 0;
  double itsGeoTransform[6] = {0, 1, 0, 0, 0, 1};
  bool itsFlipY = true;  // source grid rows run south->north; GeoTIFF wants north first
  std::string itsProjectionWKT;
  float itsMissingValue = kFloatMissing;

  // Descriptive info for the slice currently being processed
  std::string itsCurrentParamName;
  std::string itsCurrentValidTime;
  int itsCurrentLevel = 0;
  bool itsCurrentHasLevel = false;

  std::vector<Band> itsBands;
};

}  // namespace Download
}  // namespace Plugin
}  // namespace SmartMet

// ======================================================================
/*!
 * \brief SmartMet download service plugin; querydata streaming
 */
// ======================================================================

#pragma once

#include "DataStreamer.h"

namespace SmartMet
{
namespace Plugin
{
namespace Download
{
class QDStreamer : public DataStreamer
{
 public:
  QDStreamer(const Spine::HTTP::Request& req, const Config& config, const Producer& producer,
             const ReqParams &reqParams);
  virtual ~QDStreamer();

  virtual std::string getChunk();

  virtual void getDataChunk(Engine::Querydata::Q q,
                            const NFmiArea* area,
                            NFmiGrid* grid,
                            int level,
                            const NFmiMetTime& mt,
                            NFmiDataMatrix<float>& values,
                            std::string& chunk);

 private:
  QDStreamer();

  std::list<NFmiDataMatrix<float>> itsGrids;  // Stores all loaded data/grids for current parameter
  bool sendMeta;              // If set, send querydata headers (loading the first chunk)
  bool isLoaded;              // If set, all data has been loaded (but possibly not sent yet)
  size_t currentX, currentY;  // Current column and row; the grid cell to start the next chunk
};

}  // namespace Download
}  // namespace Plugin
}  // namespace SmartMet

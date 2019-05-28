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
  QDStreamer(const Spine::HTTP::Request& req, const Config& config, const Producer& producer);
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
  bool itsMetaFlag = true;     // If set, send querydata headers (loading the first chunk)
  bool itsLoadedFlag = false;  // If set, all data has been loaded (but possibly not sent yet)

  std::size_t itsCurrentX = 0;  // Current column and row; the grid cell to start the next chunk
  std::size_t itsCurrentY = 0;
};

}  // namespace Download
}  // namespace Plugin
}  // namespace SmartMet

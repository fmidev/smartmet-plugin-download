// ======================================================================
/*!
 * \brief SmartMet download service plugin; querydata streaming
 */
// ======================================================================

#pragma once

#include "Config.h"
#include "DataStreamer.h"
#include <engines/querydata/Q.h>
#include <cstddef>
#include <iosfwd>
#include <list>
#include <string>

namespace SmartMet::Plugin::Download
{
class Query;
struct Producer;
struct ReqParams;
}  // namespace SmartMet::Plugin::Download
namespace SmartMet::Spine::HTTP
{
class Request;
}  // namespace SmartMet::Spine::HTTP
template <class T>
class NFmiDataMatrix;

namespace SmartMet
{
namespace Plugin
{
namespace Download
{
class QDStreamer : public DataStreamer
{
 public:
  QDStreamer(const Spine::HTTP::Request& req, const Config& config, const Query& query,
             const Producer& producer, const ReqParams &reqParams);
  ~QDStreamer() override;

  std::string getChunk() override;

  void getDataChunk(Engine::Querydata::Q q,
                    const NFmiArea* area,
                    NFmiGrid* grid,
                    int level,
                    const NFmiMetTime& mt,
                    NFmiDataMatrix<float>& values,
                    std::string& chunk) override;

 private:
  QDStreamer() = delete;

  bool loadParamGrids();
  void writeGridValues(std::ostringstream& os, long& chunkLen);

  std::list<NFmiDataMatrix<float>> itsGrids;  // Stores all loaded data/grids for current parameter
  bool itsMetaFlag = true;     // If set, send querydata headers (loading the first chunk)
  bool itsLoadedFlag = false;  // If set, all data has been loaded (but possibly not sent yet)

  std::size_t itsCurrentX = 0;  // Current column and row; the grid cell to start the next chunk
  std::size_t itsCurrentY = 0;
};

}  // namespace Download
}  // namespace Plugin
}  // namespace SmartMet

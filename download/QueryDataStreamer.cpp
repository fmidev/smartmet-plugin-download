// ======================================================================
/*!
 * \brief SmartMet download service plugin data streaming
 */
// ======================================================================

#include "QueryDataStreamer.h"
#include <macgyver/Exception.h>
#include <newbase/NFmiQueryData.h>
#include <string>

using namespace std;

namespace SmartMet
{
namespace Plugin
{
namespace Download
{
QDStreamer::QDStreamer(const Spine::HTTP::Request& req,
                       const Config& config,
                       const Query& query,
                       const Producer& producer,
                       const ReqParams& reqParams)
    : DataStreamer(req, config, query, producer, reqParams)
{
}

QDStreamer::~QDStreamer() = default;

// ----------------------------------------------------------------------
/*!
 * \brief Load all data for the next parameter. Returns false if there is no more data.
 */
// ----------------------------------------------------------------------

bool QDStreamer::loadParamGrids()
{
  try
  {
    // Load all data for the next parameter. First store it's first grid (it was loaded in
    // previous call)
    //
    string chunk;
    auto it_p = itsParamIterator;
    itsCurrentX = itsCurrentY = 0;

    if (!itsGrids.empty())
    {
      itsGrids.clear();
      itsGrids.push_back(itsGridValues);
    }

    while (!itsDoneFlag)
    {
      extractData(chunk);

      // To handle missing/skipped parameters
      //
      if (itsGrids.empty())
        it_p = itsParamIterator;

      if (chunk.empty())
        itsDoneFlag = true;
      else if (it_p == itsParamIterator)
        itsGrids.push_back(itsGridValues);
      else
        break;
    }
    if (!(itsLoadedFlag = (!itsGrids.empty())))
      return false;

    itsReqGridSizeX = itsGrids.front().NX();
    itsReqGridSizeY = itsGrids.front().NY();
    return true;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Write parameter values until the chunk is full or all values have been written
 */
// ----------------------------------------------------------------------

void QDStreamer::writeGridValues(std::ostringstream& os, long& chunkLen)
{
  try
  {
    std::size_t valueSize = sizeof(itsGridValues[0][0]);

    for (; ((itsCurrentY < itsReqGridSizeY) && (chunkLen < itsChunkLength));
         itsCurrentY++, itsCurrentX = 0)
    {
      for (; (itsCurrentX < itsReqGridSizeX); itsCurrentX++)
      {
        // Note: Time is the fastest running querydata dimension; get the values from all grids
        // for
        // the current x/y cell
        //
        for (auto const &grid : itsGrids)
        {
          os.write((const char *)&grid[itsCurrentX][itsCurrentY], valueSize);
          chunkLen += valueSize;
        }
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
 * \brief Get next chunk of data. Called from SmartMet server code
 *
 */
// ----------------------------------------------------------------------

std::string QDStreamer::getChunk()
{
  try
  {
    try
    {
      if (itsDoneFlag && (!itsLoadedFlag))
      {
        setStatus(ContentStreamer::StreamerStatus::EXIT_OK);
        return "";
      }

      if (!itsLoadedFlag && !loadParamGrids())
      {
        setStatus(ContentStreamer::StreamerStatus::EXIT_OK);
        return "";
      }

      ostringstream os;
      std::size_t valueSize = sizeof(itsGridValues[0][0]);
      long chunkLen = 0;

      if (itsMetaFlag)
      {
        // Send querydata headers/metadata
        //
        os << *(itsQueryData->Info());
        itsMetaFlag = false;

        // "Backward compatibility when other than floats were supported"

        const int kFloat = 6;
        os << kFloat << '\n';

        //			if (FmiInfoVersion >= 6)
        //				os << itsSaveAsBinaryFlag << endl;
        os << true << '\n';

        os << itsQueryData->Info()->Size() * valueSize << '\n';

        chunkLen = os.tellp();
      }

      // Send parameter values

      writeGridValues(os, chunkLen);

      if (itsCurrentY >= itsReqGridSizeY)
      {
        itsLoadedFlag = false;

        if (itsDoneFlag)
        {
          // "Backward compatibility - not sure if needed"
          //
          os << '\n';

          setStatus(ContentStreamer::StreamerStatus::EXIT_OK);
        }
      }

      return os.str();
    }
    catch (...)
    {
      Fmi::Exception exception(BCP, "Request processing exception!", nullptr);
      exception.addParameter("URI", itsURI);

      std::cerr << exception.getStackTrace();
    }

    setStatus(ContentStreamer::StreamerStatus::EXIT_ERROR);

    itsDoneFlag = true;
    itsLoadedFlag = false;

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
 * 		Crop the grid's values if data needs manual cropping; otherwise
 * 		nothing to do
 *
 */
// ----------------------------------------------------------------------

void QDStreamer::getDataChunk(Engine::Querydata::Q q,
                              const NFmiArea * /* area */,
                              NFmiGrid *grid,
                              int /* level */,
                              const NFmiMetTime & /* mt */,
                              NFmiDataMatrix<float> &values,
                              string &chunk)
{
  try
  {
    if (!itsQueryData.get())
      // Create target querydata
      //
      createQD(grid ? *grid : q->grid());

    // Data is loaded from 'values'; set nonempty chunk to indicate data is available.

    chunk = " ";

    if ((!itsCropping.cropped) || (!itsCropping.cropMan))
      return;

    // Data must be cropped manually.

    NFmiDataMatrix<float> croppedValues(itsCropping.gridSizeX, itsCropping.gridSizeY);

    std::size_t x0 = itsCropping.bottomLeftX;
    std::size_t y0 = itsCropping.bottomLeftY;
    std::size_t xN = x0 + itsCropping.gridSizeX;
    std::size_t yN = y0 + itsCropping.gridSizeY;
    std::size_t cx = 0;
    std::size_t cy = 0;
    std::size_t x = 0;
    std::size_t y = 0;

    for (y = y0, cy = 0; (y < yN); y++, cy++)
      for (x = x0, cx = 0; (x < xN); x++, cx++)
        croppedValues[cx][cy] = values[x][y];

    values = croppedValues;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

}  // namespace Download
}  // namespace Plugin
}  // namespace SmartMet

// ======================================================================
/*!
 * \brief SmartMet download service plugin data streaming
 */
// ======================================================================

#include "QueryDataStreamer.h"

#include <newbase/NFmiQueryData.h>
#include <spine/Exception.h>

#include <string>

#include <boost/foreach.hpp>

using namespace std;

namespace SmartMet
{
namespace Plugin
{
namespace Download
{
QDStreamer::QDStreamer(const Spine::HTTP::Request &req,
                       const Config &config,
                       const Producer &producer)
    : DataStreamer(req, config, producer), sendMeta(true), isLoaded(false), currentX(0), currentY(0)
{
}

QDStreamer::~QDStreamer() {}

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
      if (isDone && (!isLoaded))
      {
        setStatus(ContentStreamer::StreamerStatus::EXIT_OK);
        return "";
      }

      if (!isLoaded)
      {
        // Load all data for the next parameter. First store it's first grid (it was loaded in
        // previous call)
        //
        string chunk;
        auto it_p = itsParamIterator;
        currentX = currentY = 0;

        if (itsGrids.size() > 0)
        {
          itsGrids.clear();
          itsGrids.push_back(itsGridValues);
        }

        while (!isDone)
        {
          extractData(chunk);

          // To handle missing/skipped parameters
          //
          if (itsGrids.size() == 0)
            it_p = itsParamIterator;

          if (chunk.empty())
            isDone = true;
          else if (it_p == itsParamIterator)
            itsGrids.push_back(itsGridValues);
          else
            break;
        }
        if (!(isLoaded = (itsGrids.size() > 0)))
        {
          setStatus(ContentStreamer::StreamerStatus::EXIT_OK);
          return "";
        }

        itsReqGridSizeX = itsGrids.front().NX();
        itsReqGridSizeY = itsGrids.front().NY();
      }

      ostringstream os;
      size_t valueSize = sizeof(itsGridValues[0][0]);
      long chunkLen = 0;

      if (sendMeta)
      {
        // Send querydata headers/metadata
        //
        os << *(itsQueryData->Info());
        sendMeta = false;

        // "Backward compatibility when other than floats were supported"

        const int kFloat = 6;
        os << kFloat << endl;

        //			if (FmiInfoVersion >= 6)
        //				os << itsSaveAsBinaryFlag << endl;
        os << true << endl;

        os << itsQueryData->Info()->Size() * valueSize << endl;

        chunkLen = os.tellp();
      }

      // Send parameter values

      for (; ((currentY < itsReqGridSizeY) && (chunkLen < itsChunkLength));
           currentY++, currentX = 0)
      {
        for (; (currentX < itsReqGridSizeX); currentX++)
        {
          // Note: Time is the fastest running querydata dimension; get the values from all grids
          // for
          // the current x/y cell
          //
          BOOST_FOREACH (auto const &grid, itsGrids)
          {
            os.write((const char *)&grid[currentX][currentY], valueSize);
            chunkLen += valueSize;
          }
        }
      }

      if (currentY >= itsReqGridSizeY)
      {
        isLoaded = false;

        if (isDone)
        {
          // "Backward compatibility - not sure if needed"
          //
          os << endl;

          setStatus(ContentStreamer::StreamerStatus::EXIT_OK);
        }
      }

      return os.str();
    }
    catch (...)
    {
      Spine::Exception exception(BCP, "Request processing exception!", NULL);
      exception.addParameter("URI", itsRequest.getURI());

      std::cerr << exception.getStackTrace();
    }

    setStatus(ContentStreamer::StreamerStatus::EXIT_ERROR);

    isDone = true;
    isLoaded = false;

    return "";
  }
  catch (...)
  {
    throw Spine::Exception(BCP, "Operation failed!", NULL);
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

    if ((!cropping.cropped) || (!cropping.cropMan))
      return;

    // Data must be cropped manually.

    NFmiDataMatrix<float> croppedValues;

    croppedValues.resize(cropping.gridSizeX, cropping.gridSizeY);

    size_t x0 = cropping.bottomLeftX, y0 = cropping.bottomLeftY;
    size_t xN = x0 + cropping.gridSizeX, yN = y0 + cropping.gridSizeY, cx, cy, x, y;

    for (y = y0, cy = 0; (y < yN); y++, cy++)
      for (x = x0, cx = 0; (x < xN); x++, cx++)
        croppedValues[cx][cy] = values[x][y];

    values = croppedValues;
  }
  catch (...)
  {
    throw Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

}  // namespace Download
}  // namespace Plugin
}  // namespace SmartMet

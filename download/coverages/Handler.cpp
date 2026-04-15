// ======================================================================
/*!
 * \brief OGC API Coverages request handler implementation
 *
 *        Initial scaffolding.  Metadata endpoints return JSON conforming
 *        to OGC API Common.  The /coverage endpoint is a placeholder
 *        that will be implemented to translate OGC parameters to
 *        ReqParams and delegate to the shared createStreamer() factory.
 */
// ======================================================================

#include "coverages/Handler.h"
#include <boost/algorithm/string.hpp>
#include <macgyver/Exception.h>
#include <macgyver/TimeFormatter.h>
#include <spine/Convenience.h>
#include <spine/FmiApiKey.h>
#include <spine/HostInfo.h>
#include <vector>

using namespace std;

namespace SmartMet
{
namespace Plugin
{
namespace Download
{
// ----------------------------------------------------------------------
/*!
 * \brief Split a URL path into segments
 */
// ----------------------------------------------------------------------

static vector<string> splitPath(const string &resource)
{
  vector<string> parts;
  boost::split(parts, resource, boost::is_any_of("/"));

  // Remove empty segments (leading slash produces one)
  parts.erase(
      std::remove_if(parts.begin(), parts.end(), [](const string &s) { return s.empty(); }),
      parts.end());

  return parts;
}

// ----------------------------------------------------------------------
/*!
 * \brief Set JSON response headers
 */
// ----------------------------------------------------------------------

static void setJsonResponse(Spine::HTTP::Response &theResponse,
                            const string &content)
{
  theResponse.setContent(content);
  theResponse.setStatus(Spine::HTTP::Status::ok);
  theResponse.setHeader("Content-Type", "application/json");
  theResponse.setHeader("Access-Control-Allow-Origin", "*");
}

// ----------------------------------------------------------------------
/*!
 * \brief Initialize handler
 */
// ----------------------------------------------------------------------

void CoveragesHandler::init(Config &config,
                            Engine::Querydata::Engine *qEngine,
                            Engine::Grid::Engine *gridEngine,
                            Engine::Geonames::Engine *geoEngine)
{
  itsConfig = &config;
  itsQEngine = qEngine;
  itsGridEngine = gridEngine;
  itsGeoEngine = geoEngine;
}

// ----------------------------------------------------------------------
/*!
 * \brief Route OGC API Coverages requests
 *
 *   /coverages                              → landing page
 *   /coverages/conformance                  → conformance declaration
 *   /coverages/collections                  → list collections
 *   /coverages/collections/{id}             → collection metadata
 *   /coverages/collections/{id}/schema      → field schema
 *   /coverages/collections/{id}/coverage    → coverage data
 */
// ----------------------------------------------------------------------

void CoveragesHandler::requestHandler(Spine::Reactor & /* theReactor */,
                                      const Spine::HTTP::Request &theRequest,
                                      Spine::HTTP::Response &theResponse)
{
  try
  {
    try
    {
      auto parts = splitPath(theRequest.getResource());

      // parts[0] == "coverages"
      // Dispatch based on remaining path segments

      if (parts.size() <= 1)
      {
        // /coverages or /coverages/
        handleLandingPage(theRequest, theResponse);
      }
      else if (parts[1] == "conformance")
      {
        handleConformance(theRequest, theResponse);
      }
      else if (parts[1] == "collections")
      {
        if (parts.size() == 2)
        {
          // /coverages/collections
          handleCollections(theRequest, theResponse);
        }
        else if (parts.size() == 3)
        {
          // /coverages/collections/{collectionId}
          handleCollection(theRequest, theResponse, parts[2]);
        }
        else if (parts.size() == 4)
        {
          if (parts[3] == "schema")
            handleSchema(theRequest, theResponse, parts[2]);
          else if (parts[3] == "coverage")
            handleCoverage(theRequest, theResponse, parts[2]);
          else
          {
            theResponse.setStatus(Spine::HTTP::Status::not_found);
            theResponse.setContent("Unknown resource: " + theRequest.getResource());
          }
        }
        else
        {
          theResponse.setStatus(Spine::HTTP::Status::not_found);
          theResponse.setContent("Unknown resource: " + theRequest.getResource());
        }
      }
      else
      {
        theResponse.setStatus(Spine::HTTP::Status::not_found);
        theResponse.setContent("Unknown resource: " + theRequest.getResource());
      }
    }
    catch (...)
    {
      Fmi::Exception exception(BCP, "Request processing exception!", nullptr);
      exception.addParameter("URI", theRequest.getURI());
      exception.addParameter("ClientIP", theRequest.getClientIP());
      exception.addParameter("HostName",
                             Spine::HostInfo::getHostName(theRequest.getClientIP()));

      const bool check_token = false;
      auto apikey = Spine::FmiApiKey::getFmiApiKey(theRequest, check_token);
      exception.addParameter("Apikey", (apikey ? *apikey : std::string("-")));

      exception.printError();

      theResponse.setStatus(Spine::HTTP::Status::bad_request);

      std::string msg = exception.what();
      boost::algorithm::replace_all(msg, "\n", " ");
      msg = msg.substr(0, 300);
      theResponse.setHeader("X-Download-Error", msg.c_str());
    }
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief OGC API landing page
 */
// ----------------------------------------------------------------------

void CoveragesHandler::handleLandingPage(const Spine::HTTP::Request & /* theRequest */,
                                         Spine::HTTP::Response &theResponse)
{
  // TODO: Generate from configuration; include proper links with server URL

  string json =
      "{\n"
      "  \"title\": \"SmartMet Server - OGC API Coverages\",\n"
      "  \"description\": \"OGC API Coverages interface to SmartMet meteorological data\",\n"
      "  \"links\": [\n"
      "    {\n"
      "      \"href\": \"/coverages\",\n"
      "      \"rel\": \"self\",\n"
      "      \"type\": \"application/json\",\n"
      "      \"title\": \"This document\"\n"
      "    },\n"
      "    {\n"
      "      \"href\": \"/coverages/conformance\",\n"
      "      \"rel\": \"http://www.opengis.net/def/rel/ogc/1.0/conformance\",\n"
      "      \"type\": \"application/json\",\n"
      "      \"title\": \"Conformance declaration\"\n"
      "    },\n"
      "    {\n"
      "      \"href\": \"/coverages/collections\",\n"
      "      \"rel\": \"data\",\n"
      "      \"type\": \"application/json\",\n"
      "      \"title\": \"Collections\"\n"
      "    }\n"
      "  ]\n"
      "}\n";

  setJsonResponse(theResponse, json);
}

// ----------------------------------------------------------------------
/*!
 * \brief OGC API conformance declaration
 */
// ----------------------------------------------------------------------

void CoveragesHandler::handleConformance(const Spine::HTTP::Request & /* theRequest */,
                                         Spine::HTTP::Response &theResponse)
{
  string json =
      "{\n"
      "  \"conformsTo\": [\n"
      "    \"http://www.opengis.net/spec/ogcapi-common-1/1.0/conf/core\",\n"
      "    \"http://www.opengis.net/spec/ogcapi-common-2/1.0/conf/collections\",\n"
      "    \"http://www.opengis.net/spec/ogcapi-coverages-1/1.0/conf/core\",\n"
      "    \"http://www.opengis.net/spec/ogcapi-coverages-1/1.0/conf/subsetting\",\n"
      "    \"http://www.opengis.net/spec/ogcapi-coverages-1/1.0/conf/fieldselection\",\n"
      "    \"http://www.opengis.net/spec/ogcapi-coverages-1/1.0/conf/scaling\",\n"
      "    \"http://www.opengis.net/spec/ogcapi-coverages-1/1.0/conf/crs\"\n"
      "  ]\n"
      "}\n";

  setJsonResponse(theResponse, json);
}

// ----------------------------------------------------------------------
/*!
 * \brief List available collections (producers)
 */
// ----------------------------------------------------------------------

void CoveragesHandler::handleCollections(const Spine::HTTP::Request & /* theRequest */,
                                         Spine::HTTP::Response &theResponse)
{
  // TODO: Build from Config producer list.  Each configured producer
  //       becomes a collection.  For now return a placeholder.

  string json =
      "{\n"
      "  \"links\": [\n"
      "    {\n"
      "      \"href\": \"/coverages/collections\",\n"
      "      \"rel\": \"self\",\n"
      "      \"type\": \"application/json\"\n"
      "    }\n"
      "  ],\n"
      "  \"collections\": []\n"
      "}\n";

  setJsonResponse(theResponse, json);
}

// ----------------------------------------------------------------------
/*!
 * \brief Single collection metadata
 */
// ----------------------------------------------------------------------

void CoveragesHandler::handleCollection(const Spine::HTTP::Request & /* theRequest */,
                                        Spine::HTTP::Response &theResponse,
                                        const std::string &collectionId)
{
  // TODO: Look up producer from Config and build proper extent/CRS/parameter metadata

  string json =
      "{\n"
      "  \"id\": \"" + collectionId + "\",\n"
      "  \"title\": \"" + collectionId + "\",\n"
      "  \"description\": \"Coverage collection for producer " + collectionId + "\",\n"
      "  \"links\": [\n"
      "    {\n"
      "      \"href\": \"/coverages/collections/" + collectionId + "\",\n"
      "      \"rel\": \"self\",\n"
      "      \"type\": \"application/json\"\n"
      "    },\n"
      "    {\n"
      "      \"href\": \"/coverages/collections/" + collectionId + "/coverage\",\n"
      "      \"rel\": \"http://www.opengis.net/def/rel/ogc/1.0/coverage\",\n"
      "      \"type\": \"application/netcdf\"\n"
      "    },\n"
      "    {\n"
      "      \"href\": \"/coverages/collections/" + collectionId + "/schema\",\n"
      "      \"rel\": \"describedby\",\n"
      "      \"type\": \"application/json\"\n"
      "    }\n"
      "  ],\n"
      "  \"extent\": {},\n"
      "  \"crs\": [\n"
      "    \"http://www.opengis.net/def/crs/OGC/1.3/CRS84\"\n"
      "  ]\n"
      "}\n";

  setJsonResponse(theResponse, json);
}

// ----------------------------------------------------------------------
/*!
 * \brief Collection schema (field/parameter descriptions)
 */
// ----------------------------------------------------------------------

void CoveragesHandler::handleSchema(const Spine::HTTP::Request & /* theRequest */,
                                    Spine::HTTP::Response &theResponse,
                                    const std::string & /* collectionId */)
{
  // TODO: Build from parameter configuration (grib.json / netcdf.json)

  string json =
      "{\n"
      "  \"$schema\": \"https://json-schema.org/draft/2020-12/schema\",\n"
      "  \"type\": \"object\",\n"
      "  \"properties\": {}\n"
      "}\n";

  setJsonResponse(theResponse, json);
}

// ----------------------------------------------------------------------
/*!
 * \brief Coverage data retrieval
 *
 *        This is the main endpoint that will translate OGC API parameters
 *        (bbox, subset, datetime, properties, scale-size, crs, f) into
 *        ReqParams and delegate to the shared createStreamer() factory.
 *
 *        Not yet implemented — returns 501.
 */
// ----------------------------------------------------------------------

void CoveragesHandler::handleCoverage(const Spine::HTTP::Request & /* theRequest */,
                                      Spine::HTTP::Response &theResponse,
                                      const std::string & /* collectionId */)
{
  // TODO: Translate OGC API Coverages parameters to ReqParams:
  //
  //   collectionId          → reqParams.producer
  //   properties=T,U,V      → reqParams param list
  //   bbox=l,b,r,t          → reqParams.bboxRect
  //   subset=pressure(...)  → reqParams.minLevel/maxLevel
  //   datetime=t1/t2        → reqParams.startTime/endTime
  //   scale-size=...        → reqParams.gridSizeXY
  //   crs=EPSG:xxxx         → reqParams.projection
  //   f=application/netcdf  → reqParams.outputFormat
  //
  //   Then call createStreamer() and stream the response.

  theResponse.setStatus(Spine::HTTP::Status::not_implemented);
  setJsonResponse(theResponse,
                  "{\n"
                  "  \"code\": \"NotImplemented\",\n"
                  "  \"description\": \"Coverage data retrieval is not yet implemented\"\n"
                  "}\n");
  theResponse.setStatus(Spine::HTTP::Status::not_implemented);
}

}  // namespace Download
}  // namespace Plugin
}  // namespace SmartMet

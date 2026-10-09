#include "ApiDefinition.h"
#include "../Config.h"
#include <macgyver/Exception.h>
#include <cctype>
#include <set>

namespace SmartMet
{
namespace Plugin
{
namespace Download
{
namespace Coverages
{
namespace
{
namespace OA = Spine::OpenAPI;

const char* const TAG_CAPABILITIES = "Capabilities";
const char* const TAG_COLLECTIONS = "Collections";
const char* const TAG_COVERAGE = "Coverage";

std::string component_name(const std::string& name)
{
  std::string ret = name;
  for (auto& c : ret)
    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '_' && c != '-')
      c = '_';
  return ret;
}

Json::Value object_schema()
{
  Json::Value ret;
  ret["type"] = "object";
  return ret;
}

Json::Value integer_schema(int minimum)
{
  Json::Value ret;
  ret["type"] = "integer";
  ret["minimum"] = minimum;
  return ret;
}

Json::Value fmi_extension(Json::Value parameter)
{
  parameter["x-fmi-extension"] = true;
  return parameter;
}

// The download plugin options which are also accepted by /coverage
struct Extension
{
  const char* name;
  const char* type;  // string, integer, array
  const char* description;
};

// clang-format off
const std::vector<Extension> extensions{
    {"source", "string", "Data source: querydata (default), grid, gridcontent or gridmapping"},
    {"starttime", "string", "Start time, an alternative to datetime; 'data' for the first time of the data"},
    {"endtime", "string", "End time, an alternative to datetime; 'data' for the last time of the data"},
    {"origintime", "string", "Origin time of the model run, the latest by default; 'data' for the latest"},
    {"timestep", "string", "Time step in minutes, or 'data' for the times of the data"},
    {"timesteps", "integer", "Number of time steps"},
    {"tz", "string", "Time zone of the times without an offset"},
    {"level", "string", "Level"},
    {"levels", "array", "Comma separated list of levels"},
    {"minlevel", "integer", "Minimum level"},
    {"maxlevel", "integer", "Maximum level"},
    {"datum", "string", "Datum shift of the output: FMI or WGS84"},
    {"projection", "string", "Output projection, an alternative to crs"},
    {"gridcenter", "string", "Center lon,lat and width,height in km of the output area"},
    {"gridsize", "string", "Output grid size width,height, an alternative to scale-size"},
    {"gridresolution", "string", "Output grid cell size in km, an alternative to scale-axes"},
    {"gridstep", "string", "Step of the output grid in the input grid cells, an alternative to scale-factor"},
    {"packing", "string", "GRIB packing type, e.g. grid_simple or grid_second_order"},
    {"bitspervalue", "integer", "GRIB bits per value"},
    {"tablesversion", "integer", "GRIB 2 tables version"},
    {"gridparamblocksize", "integer", "Number of parameters fetched from the grid engine at a time"},
    {"gridtimeblocksize", "integer", "Number of times fetched from the grid engine at a time"},
    {"format", "string", "Output format as a download plugin format name, an alternative to f"},
    {"param", "array", "Comma separated list of parameters, an alternative to properties"}};
// clang-format on

Json::Value extension_schema(const Extension& e)
{
  const std::string type = e.type;
  if (type == "integer")
    return integer_schema(0);
  if (type == "array")
    return OA::arraySchema(OA::stringSchema());
  return OA::stringSchema();
}

void add_components(OA::Document& doc, const Config& config, std::size_t maxEnum)
{
  using OA::arraySchema;
  using OA::parameter;
  using OA::ref;
  using OA::stringSchema;

  std::vector<std::string> ids;
  for (const auto& producer : config.getProducers())
    ids.push_back(producer.first);

  doc.addComponent("parameters",
                   "collectionId",
                   parameter("collectionId",
                             "path",
                             "Identifier of a collection (producer), see /collections",
                             OA::enumSchema(ids, maxEnum, "collections")));

  std::vector<std::string> formats;
  std::set<std::string> media_types;
  for (const auto& format : outputFormats())
  {
    formats.push_back(format.first);
    media_types.insert(format.second);
  }
  formats.insert(formats.end(), media_types.begin(), media_types.end());

  doc.addComponent("parameters",
                   "f",
                   parameter("f",
                             "query",
                             "Output format as a name or a media type, case insensitive. The "
                             "default is NetCDF.",
                             OA::enumSchema(formats, maxEnum)));
  doc.addComponent("parameters",
                   "f.json",
                   parameter("f", "query", "Format of the response", OA::enumSchema({"json"}, 1)));
  doc.addComponent(
      "parameters",
      "properties",
      parameter("properties",
                "query",
                "Comma separated list of the fields (parameters) to return as names or "
                "parameter numbers, see the schema of the collection",
                arraySchema(stringSchema())));
  doc.addComponent(
      "parameters",
      "subset",
      parameter("subset",
                "query",
                "Comma separated subsets axis(low:high) or axis(value), e.g. Lat(60:70) or "
                "pressure(850). The axes are Lat, Lon, time and the vertical axes pressure, "
                "height and level.",
                stringSchema()));
  doc.addComponent("parameters",
                   "datetime",
                   parameter("datetime",
                             "query",
                             "A time or an interval start/end (RFC 3339), where either end may "
                             "be '..'",
                             stringSchema()));
  Json::Value number;
  number["type"] = "number";
  Json::Value bbox = arraySchema(number);
  bbox["minItems"] = 4;
  bbox["maxItems"] = 4;
  doc.addComponent("parameters",
                   "bbox",
                   parameter("bbox", "query", "Bounding box minlon,minlat,maxlon,maxlat", bbox));
  doc.addComponent(
      "parameters",
      "crs",
      parameter("crs", "query", "Output CRS as an EPSG code or an OGC CRS URI", stringSchema()));
  doc.addComponent("parameters",
                   "scale-size",
                   parameter("scale-size",
                             "query",
                             "Output grid size per axis, e.g. Lon(400),Lat(300)",
                             stringSchema()));
  doc.addComponent("parameters",
                   "scale-factor",
                   parameter("scale-factor", "query", "Downsampling factor of the grid", number));
  doc.addComponent("parameters",
                   "scale-axes",
                   parameter("scale-axes",
                             "query",
                             "Downsampling factor per axis, e.g. Lon(2),Lat(2)",
                             stringSchema()));
  for (const auto& e : extensions)
    doc.addComponent("parameters",
                     std::string("x.") + e.name,
                     fmi_extension(parameter(e.name, "query", e.description, extension_schema(e))));

  // Schemas
  Json::Value collection = object_schema();
  collection["required"].append("id");
  collection["required"].append("links");
  collection["properties"]["id"] = stringSchema();
  collection["properties"]["title"] = stringSchema();
  collection["properties"]["description"] = stringSchema();
  collection["properties"]["links"] = arraySchema(ref("schemas", "link"));
  collection["properties"]["extent"] = object_schema();
  collection["properties"]["outputFormats"] = arraySchema(stringSchema());
  collection["properties"]["crs"] = arraySchema(stringSchema());
  doc.addComponent("schemas", "collectionDesc", collection);

  Json::Value collections = object_schema();
  collections["required"].append("links");
  collections["required"].append("collections");
  collections["properties"]["links"] = arraySchema(ref("schemas", "link"));
  collections["properties"]["collections"] = arraySchema(ref("schemas", "collectionDesc"));
  doc.addComponent("schemas", "collections", collections);

  Json::Value schema = object_schema();
  schema["description"] = "JSON Schema of the fields of the coverage";
  schema["properties"]["properties"] = object_schema();
  doc.addComponent("schemas", "fieldSchema", schema);

  // The coverage
  Json::Value coverage;
  coverage["description"] = "The coverage in the requested format";
  for (const auto& media_type : media_types)
  {
    Json::Value binary = stringSchema();
    binary["format"] = "binary";
    coverage["content"][media_type]["schema"] = binary;
  }
  coverage["headers"]["Content-Disposition"]["description"] = "Suggested file name";
  coverage["headers"]["Content-Disposition"]["schema"] = stringSchema();
  doc.addComponent("responses", "Coverage", coverage);

  Json::Value error_header;
  error_header["description"] = "The error message, also given in the response body";
  error_header["schema"] = stringSchema();
  Json::Value bad_request;
  bad_request["description"] = "The request is not valid.";
  bad_request["content"]["application/problem+json"]["schema"] = ref("schemas", "exception");
  bad_request["headers"]["X-Download-Error"] = error_header;
  doc.addComponent("responses", "CoverageError", bad_request);
}

void add_json_operation(OA::Document& doc,
                        const std::string& path,
                        const std::string& summary,
                        const std::string& operationId,
                        Json::Value parameters,
                        const std::string& schema)
{
  parameters.append(OA::ref("parameters", "f.json"));
  auto& op = doc.operation(path, "get");
  op["tags"].append(TAG_COLLECTIONS);
  op["summary"] = summary;
  op["operationId"] = operationId;
  op["parameters"] = parameters;
  op["responses"]["200"]["description"] = summary;
  op["responses"]["200"]["content"]["application/json"]["schema"] = OA::ref("schemas", schema);
  OA::Document::addErrorResponses(op);
}

void add_coverage_operation(OA::Document& doc,
                            const std::string& base,
                            const std::string& operationId,
                            Json::Value parameters,
                            const Json::Value& formatParameter)
{
  for (const char* name : {"properties",
                           "subset",
                           "datetime",
                           "bbox",
                           "crs",
                           "scale-size",
                           "scale-factor",
                           "scale-axes"})
    parameters.append(OA::ref("parameters", name));
  parameters.append(formatParameter);
  for (const auto& e : extensions)
    parameters.append(OA::ref("parameters", std::string("x.") + e.name));

  auto& op = doc.operation(base + "/coverage", "get");
  op["tags"].append(TAG_COVERAGE);
  op["summary"] = "The coverage, or a subset of it";
  op["operationId"] = operationId;
  op["parameters"] = parameters;
  op["responses"]["200"] = OA::ref("responses", "Coverage");
  op["responses"]["400"] = OA::ref("responses", "CoverageError");
  op["responses"]["404"] = OA::ref("responses", "NotFound");
  op["responses"]["500"] = OA::ref("responses", "ServerError");
}

}  // namespace

const std::vector<std::pair<std::string, std::string>>& outputFormats()
{
  // See mapOutputFormat in Handler.cpp
  static const std::vector<std::pair<std::string, std::string>> formats{
      {"grib2", "application/x-grib2"},
      {"grib1", "application/x-grib"},
      {"grib", "application/x-grib"},
      {"netcdf", "application/netcdf"},
      {"geotiff", "image/tiff"},
      {"gtiff", "image/tiff"},
      {"tiff", "image/tiff"},
      {"qd", "application/x-fmi-querydata"},
      {"querydata", "application/x-fmi-querydata"}};
  return formats;
}

std::vector<std::string> conformanceClasses(bool openapi)
{
  std::vector<std::string> ret{
      "http://www.opengis.net/spec/ogcapi-common-1/1.0/conf/core",
      "http://www.opengis.net/spec/ogcapi-common-2/1.0/conf/collections",
      "http://www.opengis.net/spec/ogcapi-coverages-1/1.0/conf/core",
      "http://www.opengis.net/spec/ogcapi-coverages-1/1.0/conf/subsetting",
      "http://www.opengis.net/spec/ogcapi-coverages-1/1.0/conf/fieldselection",
      "http://www.opengis.net/spec/ogcapi-coverages-1/1.0/conf/scaling",
      "http://www.opengis.net/spec/ogcapi-coverages-1/1.0/conf/crs"};
  if (openapi)
  {
    ret.emplace_back(OA::CONF_COMMON_OAS30);
    ret.emplace_back("http://www.opengis.net/spec/ogcapi-coverages-1/1.0/conf/oas30");
  }
  return ret;
}

Spine::OpenAPI::Document apiDefinition(const Config& config,
                                       const Spine::OpenAPI::Settings& settings)
{
  try
  {
    const auto max_enum = settings.max_enum;

    OA::Document doc("SmartMet OGC API - Coverages",
                     "Gridded forecast data as GRIB, NetCDF, GeoTIFF or querydata following OGC "
                     "API - Coverages, with subsetting, field selection, scaling and "
                     "reprojection.",
                     "1.0",
                     settings);
    doc.addTag(TAG_CAPABILITIES, "The landing page, conformance declaration and API definition");
    doc.addTag(TAG_COLLECTIONS, "Collections (producers)");
    doc.addTag(TAG_COVERAGE, "Coverage data");
    doc.json()["externalDocs"]["description"] = "OGC API - Coverages";
    doc.json()["externalDocs"]["url"] = "https://ogcapi.ogc.org/coverages/";

    doc.addCommonComponents();
    add_components(doc, config, max_enum);
    doc.addCoreOperations(TAG_CAPABILITIES, OA::ref("parameters", "f.json"));

    add_json_operation(doc,
                       "/collections",
                       "Collections",
                       "getCollections",
                       Json::Value(Json::arrayValue),
                       "collections");

    Json::Value collection(Json::arrayValue);
    collection.append(OA::ref("parameters", "collectionId"));
    add_json_operation(doc,
                       "/collections/{collectionId}",
                       "Collection description",
                       "getCollection",
                       collection,
                       "collectionDesc");
    add_json_operation(doc,
                       "/collections/{collectionId}/schema",
                       "Schema of the fields",
                       "getSchema",
                       collection,
                       "fieldSchema");
    add_coverage_operation(
        doc, "/collections/{collectionId}", "getCoverage", collection, OA::ref("parameters", "f"));

    // Collection specific coverage paths, with the formats the producer allows
    if (settings.expand_collections && config.getProducers().size() <= max_enum)
    {
      for (const auto& item : config.getProducers())
      {
        const auto& id = item.first;
        const auto& producer = item.second;
        std::vector<std::string> formats;
        for (const auto& format : outputFormats())
          if (format.second != "application/x-fmi-querydata" ||
              !producer.disabledReqParam("format"))
            formats.push_back(format.first);

        const auto name = component_name(id);
        doc.addComponent("parameters",
                         name + ".f",
                         OA::parameter("f",
                                       "query",
                                       "Output format, case insensitive. The default is NetCDF.",
                                       OA::enumSchema(formats, max_enum)));
        add_coverage_operation(doc,
                               "/collections/" + id,
                               "getCoverage_" + name,
                               Json::Value(Json::arrayValue),
                               OA::ref("parameters", name + ".f"));
      }
    }

    return doc;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Failed to generate the OGC API - Coverages definition");
  }
}

}  // namespace Coverages
}  // namespace Download
}  // namespace Plugin
}  // namespace SmartMet

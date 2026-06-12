# OGC API Coverages interface (`/coverages`)

In addition to the legacy `/download` query-string interface, the download
plugin implements an [OGC API – Coverages](https://docs.ogc.org/DRAFTS/19-087.html)
(OGC 19-087) interface at `/coverages`. Both interfaces share the same
streaming and encoding infrastructure, so equivalent requests produce
identical binary output.

Note: OGC API Coverages is still a draft standard. The implemented core
concepts (subsetting, field selection, scaling, CRS) are stable, but details
may change before the standard is approved.

## Endpoints

All endpoints are accessed with HTTP GET. Metadata endpoints return JSON
(`Content-Type: application/json`).

| Endpoint | Description |
|---|---|
| `/coverages` | Landing page with links to conformance and collections |
| `/coverages/conformance` | Conformance declaration |
| `/coverages/collections` | List of available collections |
| `/coverages/collections/{collectionId}` | Metadata for a single collection |
| `/coverages/collections/{collectionId}/schema` | Field (parameter) schema |
| `/coverages/collections/{collectionId}/coverage` | Coverage data retrieval |

A *collection* corresponds to a producer in download plugin terms: the
collection list is generated from the producers in the plugin configuration,
and the `{collectionId}` path segment selects the producer for data
retrieval. A collection ID that is not in the plugin configuration is still
accepted (with minimal metadata) if the querydata engine knows the producer.

## Conformance

The conformance declaration lists the implemented conformance classes:

- OGC API Common 1 — Core
- OGC API Common 2 — Collections
- OGC API Coverages 1 — Core
- OGC API Coverages 1 — Subsetting
- OGC API Coverages 1 — Field Selection
- OGC API Coverages 1 — Scaling
- OGC API Coverages 1 — CRS

## Coverage retrieval parameters

The `/coverage` endpoint translates OGC API parameters to their
download-equivalent parameters and delegates to the shared streamer factory.

| OGC parameter | Download equivalent | Description |
|---|---|---|
| `properties` | `param` | Field (parameter) selection, comma-separated |
| `bbox` | `bbox` | Spatial subsetting; identical syntax in both APIs |
| `datetime` | `starttime`/`endtime` | Temporal subsetting (ISO 8601 instant or interval) |
| `subset` | `level`/`minlevel`/`maxlevel`/`starttime`/`endtime` | N-dimensional subsetting by named axis |
| `scale-size` | `gridsize` | Resample to a target grid size |
| `scale-axes` | `gridresolution` | Per-axis grid resolution in kilometers |
| `scale-factor` | `gridstep` | Uniform downsampling (every Nth cell) |
| `crs` | `projection` | Output coordinate reference system |
| `f` | `format` | Output format (MIME type or short name) |

### Field selection (`properties`)

Comma-separated parameter names or numeric IDs, exactly as accepted by the
`/download` `param` option:

```
properties=Temperature,DewPoint,WindSpeedMS
```

With grid data (`source=grid`), FMI-name format parameter names are used; see
the main README section *Data sources*.

### Output format (`f`)

Both MIME types and short names are accepted (case-insensitive):

| Value | Output format |
|---|---|
| `application/x-grib2`, `grib2` | GRIB edition 2 |
| `application/x-grib`, `grib1`, `grib` | GRIB edition 1 |
| `application/netcdf`, `application/x-netcdf`, `netcdf` | NetCDF 4 |
| `application/x-fmi-querydata`, `qd`, `querydata` | QueryData (FMI native) |

The default output format is **NetCDF**. QueryData output is available for
the querydata data source only.

### Temporal subsetting (`datetime`)

ISO 8601 instants and intervals, with `..` for an open interval bound:

```
datetime=2024-01-01T00:00:00Z              single instant
datetime=2024-01-01/2024-01-02             closed interval
datetime=../2024-01-02                     open start
datetime=2024-01-01/..                     open end
```

A single instant returns exactly one timestep. An open bound leaves the
corresponding end of the range at the data default (start or end of the
dataset).

### Axis subsetting (`subset`)

Syntax is `subset=axisName(low:high)` for a range or `subset=axisName(value)`
for a single value. Multiple axes are given comma-separated within a single
`subset` parameter:

```
subset=pressure(500:850)
subset=height(2)
subset=time("2024-01-01T00:00:00Z":"2024-01-02T00:00:00Z")
subset=pressure(500:850),time("2024-01-01":"2024-01-02")
```

Recognized axes:

| Axis | Mapping |
|---|---|
| `pressure`, `height`, `level` | Range → `minlevel`/`maxlevel`, single value → `level` |
| `time` | Range → `starttime`/`endtime`, single value → both |

Either bound of a range may be omitted (`pressure(500:)`). Quotes around time
values are optional.

### Scaling (`scale-size`, `scale-axes`, `scale-factor`)

```
scale-size=Lon(300),Lat(400)       output grid of 300 x 400 cells
scale-axes=Lon(0.5),Lat(0.5)       grid cell size 0.5 x 0.5 km
scale-factor=2                     take every 2nd cell on both axes
```

The `Axis(value)` wrappers are optional; plain `scale-size=300,400` is also
accepted. `scale-size` and `scale-axes` are mutually exclusive.
`scale-factor` maps to integer grid stepping (`gridstep=N,N`).

### CRS (`crs`)

Accepted forms:

- OGC CRS URIs: `http://www.opengis.net/def/crs/EPSG/0/4326`,
  `http://www.opengis.net/def/crs/OGC/1.3/CRS84` (treated as EPSG:4326)
- EPSG codes: `epsg:4326` (case-insensitive)
- Any other CRS description GDAL understands (WKT or PROJ string). Newbase
  projection strings are **not** accepted, unlike in the `/download` API's
  `projection` option.

A GDAL description that matches a newbase projection (latlon, Mercator,
polar stereographic, azimuthal equidistant, Lambert conformal conic with the
WGS84 datum or a spherical earth and no false easting/northing or scaling) is
translated to the equivalent newbase projection string, so the output is
identical to the legacy `/download` API's. Geographic (non-projected) CRSs
are treated as latlon. If the CRS is omitted, the native projection of the
source data is used.

### Other parameters

Download parameters without an OGC equivalent are passed through unchanged
and may be combined with the OGC parameters:

| Parameter | Description |
|---|---|
| `source` | Data source: `querydata` (default), `grid`/`gridcontent`, `gridmapping` |
| `origintime` | Model run / analysis time selection |
| `starttime`, `endtime`, `timestep`, `timesteps` | Legacy time control (alternative to `datetime`) |
| `gridcenter` | Alternative bbox definition (center lon,lat + offsets in km); mutually exclusive with `bbox` |
| `packing`, `bitspervalue` | GRIB packing type and bits per value (0–32); must be given together, GRIB output only |
| `tablesversion` | GRIB2 tables version |
| `datum` | Datum shift handling |

## Collection metadata

`/coverages/collections/{collectionId}` returns OGC API Common collection
JSON with links to the coverage and schema resources, the supported output
format MIME types (QueryData is omitted when disabled for the producer), and
the supported CRS list.

## Schema endpoint

`/coverages/collections/{collectionId}/schema` returns a JSON Schema (draft
2020-12) document describing the available fields. It is built from the
plugin's NetCDF and GRIB parameter configuration tables (`netcdf.json`,
`grib.json`), with NetCDF entries preferred since they carry CF metadata.
Each property includes:

- `x-fmi-id` — newbase parameter ID
- `title` — CF long name (when configured)
- `x-ogc-propertyName` — CF standard name (when configured)
- `x-ogc-unit` — unit (when configured)

Currently the same schema is returned for all collections.

## Coverage response

Successful coverage requests stream the encoded data with:

- `Content-Type` matching the output format
  (`application/x-grib2`, `application/x-grib`, `application/netcdf`,
  `application/x-fmi-querydata`)
- `Content-Disposition: attachment; filename=...`
- `Cache-Control: public, max-age=60` and matching `Expires`/`Last-Modified`
- `Access-Control-Allow-Origin: *`

Responses use chunked HTTP streaming, so large downloads start immediately.

## Error handling

- Unknown resource paths return `404 Not Found`.
- Request processing errors return `400 Bad Request` with the error message
  (truncated to 300 characters) in the `X-Download-Error` response header.

## Examples

Surface temperature as GRIB2, four timesteps, resampled to a 45 x 50 grid
within a bounding box:

```
/coverages/collections/pal_skandinavia/coverage?properties=Temperature&f=grib2&datetime=2024-01-01T00:00:00Z/2024-01-01T12:00:00Z&scale-size=Lon(45),Lat(50)&bbox=15,58,38,71
```

Reprojected to EPSG:4326 using a grid center definition:

```
/coverages/collections/pal_skandinavia/coverage?properties=Temperature&f=grib2&gridcenter=25,60,200,200&crs=epsg:4326
```

Pressure level subset as NetCDF (default format):

```
/coverages/collections/harmonie_scandinavia_pressure/coverage?properties=Temperature,Humidity&subset=pressure(500:850)&datetime=2024-01-01/2024-01-02
```

Grid data source with FMI-name parameter format:

```
/coverages/collections/SMARTMET/coverage?source=grid&properties=T-K:SMARTMET:1096:6:2:1&f=grib2
```

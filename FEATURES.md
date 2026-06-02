# smartmet-plugin-download — Feature List

A structured inventory of capabilities provided by the SmartMet
download plugin. Use as a checklist when drafting release notes. When
new functionality is added, append the new entry under the matching
section (and bump the *Last updated* line at the bottom).

`smartmet-plugin-download` (output: `download.so`) is the bulk data
download plugin of the SmartMet Server. It serves gridded
meteorological data in GRIB1, GRIB2, NetCDF, and QueryData formats
through two API surfaces — a legacy SmartMet query-string interface
and an OGC API – Coverages interface — both backed by the same
streaming engine.

---

## 1. API surfaces

A single plugin handles two endpoints in parallel; both share the
same encoders and streaming pipeline.

- **`/download`** — legacy SmartMet query-string interface with
  parameters such as `format`, `producer`, `param`, `bbox`, etc.
- **`/coverages`** — OGC API – Coverages (OGC 19-087): collection
  metadata, subsetting, field selection, scaling, CRS.
- **Identical output** — equivalent requests on either endpoint
  produce byte-identical binary results.

## 2. Output formats

- **GRIB edition 1** — `format=grib1` (alias `grb`). Encoded via
  eccodes.
- **GRIB edition 2** — `format=grib2` (alias `grb2`). Encoded via
  eccodes.
- **NetCDF 4** — `format=netcdf` (alias `nc`). Encoded via
  `libnetcdf_c++4`; written to a temp file then streamed.
- **QueryData (FMI native)** — `format=qd`. Encoded via newbase.
  Available for the QueryData data source only.
- **Streamed delivery** — all responses use Spine's chunked HTTP
  streaming, so multi-gigabyte downloads start emitting bytes
  immediately.

## 3. Data sources

A single `source=` parameter picks the backend.

- **QueryData** (`source=querydata`, default) — FMI's native gridded
  format via the `querydata` engine. Parameters use newbase names
  (`Temperature`, `WindSpeedMS`, …) or numeric IDs.
- **Grid** (`source=grid`) — GRIB / NetCDF data via the `grid`
  engine. Parameters use the FMI-name format
  `<param>:<producer>:<geometryid>:<leveltypeid>:<level>:<forecasttype>[:<forecastnumber>]`.

## 4. Spatial selection

- **Bounding box** — `bbox=left,bottom,right,top` (lon/lat). Crops
  from the source grid when no reprojection is requested.
- **Centre + offset** — `gridcenter=cx,cy,offx,offy` defines the box
  by centre lon/lat plus offsets in kilometres.
- **CRS / projection** —
  - newbase projection strings or EPSG code for QueryData output,
  - EPSG code, proj4 string or WKT for grid output.
- **Cell selection / decimation**
  - `gridstep=x,y` — keep every Nth cell on each axis.
  - `gridsize=x,y` — explicit output grid dimensions.
  - `gridresolution=x,y` — output cell width/height in kilometres.
- **WGS84 datum shift** — `Datum.cpp` performs the WGS84 ↔ FMI
  spherical datum conversion when requested.

## 5. Temporal selection

- **Range** — `starttime=`, `endtime=`.
- **Step** — `timestep=<minutes>` (commonly 60, 180, 360, 720, 1440).
- **Count** — `timesteps=<n>`.
- **Wall-clock pinning** — `now=...` for reproducible tests.
- **Time zone** — `tz=...`.
- **Grid-engine origin time** — origin / analysis time controlled via
  the producer's geometry / generation selection.

## 6. Vertical / level selection

QueryData source:

- **Single level** — `level=...` (hybrid or hPa pressure).
- **Multiple levels** — `levels=1000,925,850,700,500,...`.
- **Default = all** — omitted means every available level.

Grid source:
- Level is part of the radon parameter name; the standalone `level` /
  `levels` options are not used.

## 7. Parameter selection

- **Comma-separated list** — `param=Temperature,DewPoint,...`.
- **Newbase names or IDs** — for QueryData source.
- **Radon parameter names** — for grid source, with embedded producer
  / geometry / level / forecast type / number.
- **Producer override** — `producer=...` / `model=...` (alias).
- **Per-parameter mapping** — `cnf/grib.json` and `cnf/netcdf.json`
  drive encoding (paramId / discipline / category for GRIB; CF
  `standard_name` / `long_name` / `unit` for NetCDF).

## 8. Streaming engine

`DataStreamer` is the abstract base for all output formats; it owns:

- **Grid setup and coordinate transforms** via `Resources` (RAII
  wrapper around `NFmiArea`, `NFmiGrid`, OGR spatial references and
  coordinate transforms).
- **Bounding-box and centre/offset resolution**.
- **Level iteration** (level / pressure / hybrid).
- **Time iteration** (with timestep / count semantics).
- **Chunked emission** — virtual `getDataChunk()` /
  `getGridDataChunk()` filled in by the concrete streamer.
- **Block-size tuning** — `gridparamblocksize`, `gridtimeblocksize`
  tune the streamer's iteration granularity.

Concrete streamers:

- **`GribStreamer`** — GRIB1/GRIB2 via eccodes.
- **`NetCdfStreamer`** — NetCDF4 via `libnetcdf_c++4`.
- **`QueryDataStreamer`** — QueryData via newbase.
- **`StreamerFactory::createStreamer()`** — picks the right streamer
  from `outputFormat`, wires up engines, checks data availability.

## 9. GRIB encoding details

- **eccodes-backed** — uses `grib_api` from the eccodes package.
- **Packing rule selection** — packing rules per producer from the
  config (e.g. simple, grid_simple, grid_jpeg, png).
- **Parameter mapping** — `cnf/grib.json` maps QueryData newbase IDs
  or grid radon names to GRIB `paramId` / `discipline` / `category`.
- **Edition selection** — `grib1` or `grib2` from the `format=`
  parameter.
- **Level type translation** — pressure / hybrid / surface levels
  encoded with the matching GRIB level type code.

## 10. NetCDF encoding details

- **NetCDF 4** classic + extensions via `libnetcdf_c++4`.
- **CF metadata** — `cnf/netcdf.json` maps parameters to CF
  `standard_name`, `long_name`, `units`.
- **Two-step writer** — full NetCDF file built on disk first, then
  streamed back to the client.
- **Multi-parameter / multi-time variables** — single file groups all
  selected parameters and time steps.

## 11. QueryData encoding details

- **FMI native** — uses newbase `NFmiQueryData` directly.
- **Single-producer per file** — output mirrors the producer's
  geometry, parameter set, and level structure.
- **Grid-source not supported** — QueryData output is only available
  when `source=querydata`.

## 12. Engine integration

The plugin uses the following engines (resolved at runtime via
dlopen):

- **`querydata`** — required for `source=querydata`.
- **`grid`** — required for `source=grid`.
- **`geonames`** — optional, used for place-name resolution.
- **Resource sharing** — engine references are passed into the
  streamer via `StreamerFactory`.

## 13. Configuration

Loaded from the plugin config (`Config.cpp`, libconfig with SmartMet
extensions):

- **Producer settings** — per-producer defaults and overrides.
- **Packing rules** — GRIB packing per producer / parameter.
- **Parameter mapping files** — `cnf/grib.json`, `cnf/netcdf.json`.
- **Default temp directory** — for the NetCDF two-step writer.
- **Standard SmartMet config extensions** — `@include`, `@ifdef`,
  `$(VAR)`, `%(DIR)`.

## 14. OGC API – Coverages translation (`/coverages`)

`coverages/Handler.cpp` translates OGC parameters into the plugin's
internal `ReqParams` so that:

- **Properties** become the `param` list.
- **Datetime** becomes `starttime`/`endtime`.
- **Subset** becomes `bbox` / level selection.
- **Scale-size / scale-axes** become `gridsize` / `gridstep`.
- **CRS** maps to the projection string.
- **`f=...`** picks the output format (`grib2`, `netcdf`, ...).
- **Collection metadata endpoints** — listing collections and their
  domains follow the OGC API – Coverages REST shape.

## 15. Testing

Integration-test suite under `test/`:

- **QueryData tests** — `make test-qd` (default in CI).
- **Grid tests** — `make test-grid` (skipped in CI; requires Redis).
- **Combined** — `make test`.
- **`LOCAL_TESTS_ONLY`** env var — restrict to QueryData tests
  locally.
- **Per-test scripts** — `test/scripts/*.get` build the HTTP request;
  expected output lives in `test/input/*.get`.
- **Two comparison modes**:
  - Binary (default): byte-compare the streamed output.
  - **Value comparison** (`*_val*.get`): dump grid values to text and
    diff (more robust to encoder metadata differences).
- **Failure capture** — `test/failures/` holds the actual output of
  failed tests.
- **WGS84 expected outputs** — `.wgs84` variants in `test/input/` for
  datum-shifted requests.
- **Per-backend configs** — `test/cnf/` and `test/grid/cnf/`.

## 16. Documentation

- **`README.md`** — full URL-parameter reference.
- **`docs/docker.md`** — running the plugin in Docker.

## 17. Build & integration

- **Output**: `download.so`.
- **Loaded at**: `$(prefix)/share/smartmet/plugins/download.so`.
- **Build**: `make`.
- **Format**: `make format` runs clang-format.
- **Install**: `make install`.
- **RPM**: `make rpm`.
- **CI**: CircleCI on RHEL 8 / RHEL 10 (`fmidev/smartmet-cibase-{8,10}`
  Docker images); only QueryData tests run in CI.
- **External libraries**: eccodes (`grib_api`), `libnetcdf_c++4`,
  GDAL, newbase, Boost.
- **SmartMet libraries**: spine, macgyver, newbase, gis,
  grid-files, grid-content.

---

*Last updated: 2026-06-01.*

# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

SmartMet Server download plugin (`smartmet-plugin-download`). Provides bulk meteorological data download from SmartMet Server in GRIB1, GRIB2, NetCDF, and QueryData formats. Handles parameter selection, time ranges, level filtering, bounding box cropping, reprojection, and grid resampling.

## Build commands

```bash
make                    # Build download.so
make test               # Run all tests (querydata + grid)
make test-qd            # Run querydata-source tests only
make test-grid          # Run grid-source tests only
make format             # clang-format all source files
make clean              # Clean build artifacts and test artifacts
make rpm                # Build RPM package
```

Tests require `smartmet-plugin-test` (an HTTP-based integration test runner), Redis, and optionally a geonames database. In CI, grid tests are skipped (only `test-qd` runs). Locally, both `test-qd` and `test-grid` run unless `LOCAL_TESTS_ONLY` is set.

## Testing architecture

Tests are **integration tests**, not unit tests. The test harness (`smartmet-plugin-test`) starts a SmartMet Server reactor with the locally-built `download.so` plugin, then sends HTTP GET requests and compares responses against expected output.

- `test/scripts/*.get` — shell scripts that produce the HTTP request URL
- `test/input/*.get` — expected binary output (GRIB/NetCDF/QD files); test names with `_val` prefix use `test/scripts/*_val*.get` dumper scripts for text-based value comparison
- `test/output/*.get` — actual output from the latest test run (binary); `.wgs84` variants contain WGS84-transformed output
- `test/cnf/` — server configuration (reactor.conf, download.conf, querydata.conf, geonames.conf, grid-engine.conf)
- `test/grid/` — separate test suite for grid-engine data source with its own Makefile and cnf/scripts/input/output

Test naming convention: `{format}[_val]_{producer}_{timesel}[_{options}].get`
- format: `grb2`, `grb`, `nc`, `qd`
- `_val` suffix means value-comparison test (text dump rather than binary comparison)
- producer: e.g. `pal-skd-dl` (pal_skandinavia), `ec-skd-pr` (ecmwf pressure)
- timesel: `last_tstep`, `many_tsteps`, etc.
- options: `bbox`, `proj`, `gsize`, `gresol`, `gstep`, `gcenter`, `packing`, etc.

## Architecture

### Class hierarchy

`DataStreamer` (abstract base, inherits `Spine::HTTP::ContentStreamer`) is the core — it handles grid setup, coordinate transforms, bounding box logic, level iteration, and the chunked streaming loop. Three concrete streamers:

- `GribStreamer` — GRIB1/GRIB2 output via eccodes (`grib_api`)
- `NetCdfStreamer` — NetCDF4 output via `libnetcdf_c++4` (writes complete temp file, then streams)
- `QDStreamer` — QueryData (FMI native binary format) output via newbase

### Request flow

`Plugin::query()` → parses `ReqParams` from HTTP query string → creates `Query` (parameter/time/level parsing) → selects `Producer` from config → instantiates appropriate streamer → calls `extractData()` / `extractGridData()` which iterates params × times × levels, calling virtual `getDataChunk()` / `getGridDataChunk()` for each message.

### Two data sources

1. **QueryData** (`source=querydata`, default) — uses `Engine::Querydata` to load FMI's native gridded data. Parameters use newbase names (Temperature, WindSpeedMS, etc.) or numeric IDs.
2. **Grid** (`source=grid`) — uses `Engine::Grid` for GRIB/NetCDF source data via grid-content/grid-files. Parameters use FMI-name format: `<param>:<producer>:<geometryid>:<leveltypeid>:<level>:<forecasttype>[:<forecastnumber>]`.

### Parameter configuration

- `cnf/grib.json` — maps between QueryData parameter IDs (newbaseid) or grid parameter names (radonname) and GRIB keys (paramId, discipline, category, etc.)
- `cnf/netcdf.json` — maps to NetCDF CF attributes (standardname, longname, unit)
- `ParamConfig.cpp` parses these JSON files into `ParamChangeTable`

### Key support classes

- `Config` — loads libconfig `.conf` file, parses producer settings, packing rules, parameter mappings
- `Query` — parses HTTP request parameters, handles radon parameter name parsing, level/forecast number range expansion
- `Resources` — RAII manager for NFmiArea, NFmiGrid, and OGR spatial reference / coordinate transform objects
- `Datum` — WGS84 datum shift handling
- `GribTools` — eccodes handle wrapper and helpers
- `Tools` — level type classification functions, radon parameter name parsing utilities

### Dependencies

Engines loaded at runtime (unresolved `SmartMet::Engine::` symbols resolved by server):
- `querydata` — weather data access
- `grid` — grid data content/query server access
- `geonames` — geographic name resolution

External libraries: eccodes, libnetcdf_c++4, GDAL/OGR, Boost (thread, iostreams), libconfig++, jasper

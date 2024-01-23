Table of Contents
=================

  * [SmartMet Server](#SmartMet Server)
  * [Introduction](#introduction)
  * [Interface](#interface)
    * [Levels](#levels)
    * [Parameters](#parameters)
      * [Regular querydata parameter names](#regular-querydata-parameter-names)
      * [Numeric querydata parameters](#numeric-querydata-parameters)
      * [Grid data parameters](#grid-data-parameters)
    * [Time control](#time-control)
      * [Origintime](#origintime)
      * [Time interval](#time-interval)
      * [Timestep](#timestep)
      * [Number of timesteps](#number-of-timesteps)
    * [Packing](#packing)
    * [Projections](#projections)
    * [Data sources](#data-sources)
      * [Grid data parameter name format](#grid-data-parameter-name-format)
      * [Grid data geometry id](#grid-data-geometry-id)
      * [Grid data level types](#grid-data-level-types)
      * [Grid data forecast types](#grid-data-forecast-types)
      * [Grid data forecast number](#grid-data-forecast-number)
  * [Configuration](#configuration)
    * [Main configuration file](#main-configuration-file)
      * [GRIB Configuration](#grib-configuration)
      * [NetCDF Configuration](#netcdf-configuration)
      * [Producers](#producers)
      * [Path for temporary NetCDF files](#path-for-temporary-netcdf-files)
    * [GRIB_API to QueryData parameter mapping](#grib_api-to-querydata-parameter-mapping)
      * [gribid](#gribid)
      * [newbaseid](#newbaseid)
      * [name](#name)
      * [offset](#offset)
      * [divisor](#divisor)
      * [leveltype](#leveltype)
      * [levelvalue](#levelvalue)
      * [centre](#centre)
      * [templatenumber](#templatenumber)
      * [aggregatetype](#aggregatetype)
      * [aggregatelength](#aggregatelength)
      * [Grib to QueryData parameter configuration example](#grib-to-querydata-parameter-configuration-example)
    * [GRIB_API to grid data parameter mapping](#grib_api-to-grid-data-parameter-mapping)
      * [radonname](#radonname)
      * [radonproducer](#radonproducer)
      * [name](#name-1)
      * [centre](#centre-1)
      * [discipline](#discipline)
      * [category](#category)
      * [parameternumber](#parameternumber)
      * [table2version](#table2version)
      * [indicatoroftimerange](#indicatoroftimerange)
      * [typeofstatisticalprocessing](#typeofstatisticalprocessing)
      * [templatenumber](#templatenumber-1)
      * [aggregatelength](#aggregatelength-1)
      * [Grib1 to grid data parameter configuration example](#grib1-to-grid-data-parameter-configuration-example)
      * [Grib2 to grid data parameter configuration example](#grib2-to-grid-data-parameter-configuration-example)
    * [NetCDF to QueryData parameter mapping](#netcdf-to-querydata-parameter-mapping)
      * [newbaseid](#newbaseid-1)
      * [name](#name-2)
      * [standardname](#standardname)
      * [longname](#longname)
      * [unit](#unit)
      * [offset](#offset-1)
      * [divisor](#divisor-1)
      * [aggregatetype](#aggregatetype-1)
      * [aggregatelength](#aggregatelength-2)
      * [NetCDF to QueryData parameter configuration example](#netcdf-to-querydata-parameter-configuration-example)
    * [NetCDF to grid data parameter mapping](#netcdf-to-grid-data-parameter-mapping)
      * [radonname](#radonname-1)
      * [radonproducer](#radonproducer-1)
      * [name](#name-3)
      * [standardname](#standardname-1)
      * [longname](#longname-1)
      * [unit](#unit-1)
      * [aggregatetype](#aggregatetype-2)
      * [aggregatelength](#aggregatelength-3)
      * [NetCDF to grid data parameter configuration example](#netcdf-to-grid-data-parameter-configuration-example)
    * [Docker](#docker)
  * [Example queries](#example-queries)

# SmartMet Server

[SmartMet Server](https://github.com/fmidev/smartmet-server) is a data and product server for MetOcean data. It
provides a high capacity and high availability data and product server
for MetOcean data. The server is written in C++, since 2008 it has
been in operational use by the Finnish Meteorological Institute FMI.


# Introduction 

The SmartMet download plugin provides access to the timeseries data
interpolated from the QueryData. It provides grid data from the qengine as
binary data. The plugin supports GRIB (GRIB1 or GRIB2), NetCDF and
QueryData formats. Supports all proj.4 projections depending on the
output format.

Download plugin supports slicing the data by area (bbox), elevation
(pressure and/or model level) and time (start time, end time and
origin time). With this plugin we can define the grid size and/or resolution by
selecting every Nth grid cell on the x and the y axis,
by grid size based on the number of cells on the x and the y
axis, or by grid cell width/height.

# Interface
The interface derives from the timeseries module. See it's documentation for the options below.
level/levels and model(producer) options are not used when fetching grid data; see [Data sources](#data-sources).

* starttime, endtime
* timestep=X (data timestep in minutes; 60, 180, 360, 720, 1440)
* timesteps=X (number of timesteps to fetch)
* level=X (hybrid or hPa pressure level; e.g. level=1000)
* levels=level,level,... (hybrid or hPa pressure level; e.g. levels=1000,925,850,700,500,400,300,250,200,100,50)
* param=param,param,... (e.g. param=Temperature,DewPoint,Humidity,WindSpeedMS,WindDirection,WindUMS,WindVMS,Pressure,Precipitation1h,TotalCloudCover,GeopHeight)
* projection=projdef (newbase projection string or epsg code (e.g. epsg:4326) for QueryData, or epsg code, proj4 projection string or wkt for grid data
* format=GRIB1|GRIB2|netcfd|qd (grid data output in qd (QueryData) format is not supported)
* model (or producer) (e.g. model=harmonie_scandinavia_surface)
* gridstep=x,y (select every Nth cell on the x and the y axis)
* gridsize=x,y (number of cells on the x and the y axis)
* gridresolution=x,y (grid cell width/height in kilometers). Similar to gridsize option, but in geographical units.
* bbox=left,bottom,right,top (bounding box edges as lon and lat coordinates, e.g. bbox=22,64,24,68. Data is cropped from source grid if no reprojecting)
* gridcenter=centerx,centery,offsetx,offsety (an alternative way to define bbox using grid center's lon/lat coordinates and offsets to grid edges as kilometers)

## Levels
By default all levels are returned. This can be limited with the options. Option is not used when fetching grid data; see [Data sources](#data-sources).

<pre><code>level=value
levels=value1,value2,...
</code></pre>

For example, to get levels 700 hPa and 850 hPa, give levels=700,850 as values in the levels option.

## Parameters

The parameter option is used for selecting the parameters that are to be extracted from the model. The option format is as follows:

<pre><code>param=name1,name2,...,nameN </code></pre>

### Regular querydata parameter names
Regular QueryData parameter names such as Temperature and WindSpeedMS are recognized as is.

### Numeric querydata parameters
QueryData parameter id numbers are allowed as parameter identifiers. The QueryData parameters can be fetched using qdinfo. For example, the QueryData parameterid 1 (Pressure) refers to netcdf parameter "air_pressure_at_sea_level", which has standardname "air_pressure_at_sea_level" and a longname "Air pressure at sea level"

### Grid data parameters
FMI-name format parameter names are used to fetch grid data. See [Data sources](#data-sources)

## Time control
There are three basic methods for extracting/accessing the data with timestamps:

1. Using specific or the latest available model run/analysistime
2. Using specific data time range or fetching all available time instants
3. Using specific data timestep in minutes or fetching time instants as is

### Origintime

The query string field "origintime" can be used to select a specific run/analysistime read by the SmartMet server. Each QueryData file should have a distinct origintime, i.e, the time the data was created. This is used more as a unique identification number for a data file than as a definition of the data contents. Origintime itself does not necessarily relate to the time parameters in the data rows. For example, the QueryData files can be created with the same forecast contents multiple times, but the origintime changes for each run.

<pre><code>origintime=t</code></pre>

where t is timestamp, e.g. in 12-digit (YYYYMMDDHHMM) form.
If this field is not given, SmartMet server uses the latest origintime available for the data model/producer.

QueryData can be configured to be loaded in multifile mode, in which case all available/needed datasets are used to fullfill the requested time range if specific origintime is not given.
Download plugin does not support multifile mode when fetching grid data.

### Time interval
Option syntax: 
<pre><code>starttime=t1&endtime=t2 </code></pre>
Both options can be omitted. The default values for the options are:
* starttime = start time of the dataset (starttime=data)
* endtime = end time of the dataset (endtime=data)

Normally the times are given in 12-digit timestamp form.

Unlike in Timeseries-plugin and in common practice, if no offset is defined in given timestamp, the time is understood as UTC-time. For example, 2016-02-13T14:00+02 is 12:00 UTC and 14:00 in localtime, but the timestamp 201602131400 or 2016-02-13T14:00 is interpreted as 14:00 UTC and 16:00 in localtime.

If the value is an integer, it is assumed to be epoch seconds. However, if the integer is preceded by a sign, it is interpreted as the number of minutes to be added to wall clock.

Note that the option [Number of timesteps](#number-of-timesteps) can be used alternatively to specify the end time.

### Timestep
Option syntax:
<pre><code> timestep=data|minutes</code></pre>

If the timestep is "data", the selected model timesteps are used. Minimum allowed timestep is the data timestep. For example, for Harmonie the data timestep is 60.

If specific timestep is given in minutes, it must be a divisor of 1440 (minutes in a day). For example, the timesteps  60, 120, 180, 240 or 360 which translate to 1, 2, 3, 4 and 6 hours respectively.

The given timestep is used to produce available data time points by counting from 00:00 hours forwards.

<pre><code><b>
The example below shows the timestep parameter's behaviour</b> 

If a timestep of value 180 is given, the produced time points are
00:00, 03:00, 06:00, 09:00, 12:00, 15:00, 18:00, 21:00. Time points are in local time.

When starttime is +0h (the request time) for an example 2014-11-15
15:58 in local time (+02:00 offset from UTC in Finland during the winter time).

From timestep produced time points it is then checked what is the next
available time point for a given starttime. In this example when the
starttime is at 15:58 the next available time point is at 18:00.

The data returned (at least in GRIB2-format) now contains a GRIB
message that contains the time for 2014-11-15T16:00Z (UTC time). 
The next message contains data for time 2014-11-15T19:00Z which 
is three hours after the previous time point.
</code></pre>

The default timestep is the data timestep. If the timestep is nonpositive, an error message is returned.

### Number of timesteps

Option syntax: 
<pre><code>timesteps=N</code></pre>
This option is used to limit the number of timesteps returned. This is an alternative way to specify endtime.

If the value is nonpositive, an error message is returned.
 
## Packing

Option syntax: 
<pre><code>packing=type </code></pre>
This option is used to select the packing type for the GRIB format output. The given type is used as it is. The default type is GRIB_simple.

Note: some packing types can cause overhead at the server and these types should not be applied unless there are special reasons such as it is necessary to transfer less data due to the slow communication link etc.

## Data sources

Default data source is QueryData (source=querydata).
If grid data (use of GRIB and NetCDF source data) is enabled in smartmet-server i.e. grid-engine is available,
the data can be fetched in GRIB or NetCDF format with source=grid and by using FMI-name format parameter names.
The available grid data producers and their data/parameters can be examined with grid-gui plugin.

At FMI, since download plugin's grid data GRIB parameter configuration is much more comprehensive than NetCDF configuration, it's advisable to use GRIB output format.

### Grid data parameter name format

Format (FMI-name) is
<pre><code>&lt;paramname&gt;:&lt;producer&gt;:&lt;geometryid&gt;:&lt;leveltypeid&gt;:&lt;level&gt;:&lt;forecasttype&gt;[:&lt;forecastnumber&gt;]</code></pre>
e.g. T-K:SMARTMET:1096:6:2:1

Every parameter to fetch must have the same geometry. Function parameters calculated on the fly can be fetched using notation
<pre><code>&lt;funcparam&gt; as &lt;outputparam&gt;</code></pre>
where funcparam is an expression accepted by grid-engine and outputparam is a FMI-name,
e.g. /avg{T-K:SMARTMET:1096:6:2:1;-5;0;60} as T-K-AVG6H:SMARTMET:1096:6:2:1.

Function output parameters must have GRIB and/or NetCDF parameter configuration just as fetched regular data parameters do, and they must have the same geometry as other output parameters and/or regular data parameters (if any) returned by the query.

### Grid data geometry id

Geometry id identifies parameter's native geometry (grid projection and size).

### Grid data level types

Supported level types are
- 1: Ground or water surface (grib output only)
- 2: Pressure level
- 3: Hybrid level
- 5: Top of atmosphere (grib output only)
- 6: Height above ground in meters
- 7: Mean sea level (grib output only)
- 8: Entire atmosphere (grib output only)
- 10: Depth below some surface

### Grid data forecast types

Forecast types are
- 1: Deterministic forecast
- 2: Analysis
- 3: Ensemble forecast, perturbation
- 4: Ensemble forecast, control forecast
- 5: Statistical post processing

### Grid data forecast number

Forecast number applies to ensemble forecasts only.

## Projections

The plugin uses gdal, <a href="www.gdal.org">Geospatial Data Abstraction Library</a>, so in theory any projection known by proj.4 may work. In practice, only some of them are feasible for GRIB and NetCDF. The following projections are safe choices:

* EPSG:4326
* EPSG:3995
* stereographic (Polar Stereographic, latitude of origin 60, central meridian 0)

If projection is not given, the native projection of the source data is used.

### Source is querydata

Supported input projections are as follows
- Output as NetCDF
  - LatLon
  - Polar Stereographic
  - YKJ
- Output as GRIB
  - LatLon
  - RotatedLatLon
  - Polar Stereographic
  - Mercator

### Source is grid-engine (`source=grid`)

Supported input projections are as follows

- Output as NetCDF
  - LatLon
  - Rotated LatLon
  - Polar Stereographic
  - Mercator
  - LambertConformal
- Output as GRIB
  - LatLon
  - Rotated LatLon
  - Polar Stereographic
  - Mercator
  - LambertConformal
  - LambertAzimuthalEqualArea


# Configuration

## Main configuration file

The Download plugin's main configuration file is
set in SmartMet server's configuration. The configuration file defines
format specific configuration files for parameter mapping.

### GRIB Configuration
Configuration file for GRIB parameter mapping can be given as follows. If GRIB
output is not required this setting can be omitted. See [GRIB_API to QueryData parameter mapping](#grib_api-to-querydata-parameter-mapping)
and [GRIB_API to grid data parameter mapping](#grib_api-to-grid-data-parameter-mapping) for configuration details.

<pre><code> 
gribconfig = <filename>;
</code></pre>

### NetCDF Configuration

Configuration file for NetCDF parameter mapping can be given as follows. If NetCDF output is not required this setting can be omitted. See [NetCDF to QueryData parameter mapping](#netcdf-to-querydata-parameter-mapping)
and [NetCDF to grid data parameter mapping](#netcdf-to-grid-data-parameter-mapping) for configuration details.

<pre><code> 
netcdfconfig = <filename>;
</code></pre>

### Producers
The Default producer at FMI is "pal_skandinavia". All available QueryData producers can be used or only a subset of producers can be enabled by configuration.

### Path for temporary NetCDF files.
<pre><code> 
tempdirectory = <pathname>;
</code></pre>

* default: /dev/shm. 
* Note: for better performance memory mapped file system should be used. Complete NetCDF files (multiple files when  processing simultaneous download requests) are written to this  location; disk space availability could become an issue.

## GRIB_API to QueryData parameter mapping

Configuration file grib.json contains a list of elements and each element represents a single QueryData-GRIB_API mapping [ mapping_node1, mapping_node2 ...]

Each mapping_node contains key-value-pairs where the key can be any of the following:

gribid, newbaseid, name, offset, divisor, leveltype, levelvalue, centre, templatenumber, aggregatetype, aggregatelength

### gribid
GRIB-API edition independent paramId

### newbaseid
QueryData parameter number

### name
Text representing the name of the parameter

### offset
Parameter value scaling offset. Default: 0

out_value = ( in_value + offset ) / divisor

### divisor
Parameter value scaling divisor. Default: 1

out_value = ( in_value + offset ) / divisor

### leveltype
GRIB-API level type

### levelvalue
Level value

### centre
GRIB-API centre name. efkl for the local FMI definitions.

### templatenumber
GRIB-API grib2 product definition template number

See <a href="https://codes.ecmwf.int/grib/format/grib2/ctables/4/0/">GRIB-API productDefinitionTemplateNumber</a>

### aggregatetype
GRIB-API method of aggregate

### aggregatelength
Duration of aggregation in minutes
For example, for 00-24 UTC maximum temperature at 2 meters, which has a gribid of 51 and newbaseid 358, has an aggregate length of 1440 minutes, i.e., 24 hours * 60 minutes.

### Grib to QueryData parameter configuration example

{
  "gribid" : 167,
  "newbaseid" : 4,
  "name" : "2t",
  "offset" : 273.15,
  "leveltype" : "heightAboveGround",
  "levelvalue" : 2
}

## GRIB_API to grid data parameter mapping

Configuration file grib.json contains a list of elements and each element represents a single grid data-GRIB_API mapping [ mapping_node1, mapping_node2 ...]

Each mapping_node contains key-value-pairs where the key can be any of the following:

radonname, radonproducer, name, centre, discipline, category, parameternumber, indicatoroftimerange, typeofstatisticalprocessing, templatenumber, aggregatelength

### radonname
Grid data parameter name

### radonproducer
Grid data producer name. If producer name is omitted, the configuration entry is used as the default for the parameter for producers which have no named entry.

### name
Text representing the name of the parameter

### centre
GRIB-API centre name

### discipline
GRIB-API grib2 discipline

### category
GRIB-API grib2 category

### parameternumber
GRIB-API parameter number

### table2version
GRIB-API grib1 tables version

### indicatoroftimerange
GRIB-API grib1 method of aggregate

See <a href="https://codes.ecmwf.int/grib/format/grib1/ctable/5/">GRIB-API indicatoroftimerange</a>

### typeofstatisticalprocessing
GRIB-API grib2 method of aggregate

See <a href="https://codes.ecmwf.int/grib/format/grib2/ctables/4/10/">GRIB-API typeofstatisticalprocessing</a>

### templatenumber
GRIB-API grib2 product definition template number

See <a href="http://apps.ecmwf.int/codes/grib/format/grib2/ctables/4/0/">GRIB-API productDefinitionTemplateNumber</a>

### aggregatelength
Duration of aggregation in minutes

### Grib1 to grid data parameter configuration example

{
  "radonname" : "P-STDDEV-PA",
  "radonproducer" : "GFSMTA",
  "grib1" :
  {
    "table2version" : 207,
    "parameternumber" : 171,
    "indicatoroftimerange" : 10
  }
}

### Grib2 to grid data parameter configuration example

{
  "radonname" : "WGV-MS",
  "grib2" :
  {
    "templatenumber" : 11,
    "discipline" : 0,
    "category" : 2,
    "parameternumber" : 24,
    "typeofstatisticalprocessing" : 2
  },
  "aggregatelength" : 60
}

## NetCDF to QueryData parameter mapping

The configuration file netcdf.json which contains a list of elements and each element represents a single QueryData-NetCDF_API mapping [ mapping_node1, mapping_node2 ...]

Each mapping_node contains key-value-pairs where the key can be any of the following:

newbaseid, name, standardname, longname, unit, offset, divisor, aggregatetype, aggregatelength

### newbaseid
QueryData parameter number

### name
Text representing the name of the parameter

### standardname
NetCDF CF parameter standard name

### longname
NetCDF CF parameter long name

### unit
NetCDF CF parameter unit

### offset
Parameter value scaling offset. Default: 0

out_value = ( in_value + offset ) / divisor

### divisor
Parameter value scaling divisor. Default: 1

out_value = ( in_value + offset ) / divisor

### aggregatetype
NetCFD method of aggregate

### aggregatelength
Aggregate duration in minutes

### NetCDF to QueryData parameter configuration example

{
  "newbaseid" : 1,
  "name" : "air_pressure_at_sea_level",
  "standardname" : "air_pressure_at_sea_level",
  "longname" : "Air pressure at sea level",
  "divisor" : 0.01,
  "unit" : "Pa"
}

## NetCDF to grid data parameter mapping

The configuration file netcdf.json which contains a list of elements and each element represents a single grid data-NetCDF_API mapping [ mapping_node1, mapping_node2 ...]

Each mapping_node contains key-value-pairs where the key can be any of the following:

radonname, radonproducer, name, standardname, longname, unit, aggregatetype, aggregatelength

### radonname
Grid data parameter name

### radonproducer
Grid data producer name. If producer name is omitted, the configuration entry is used as the default for the parameter for producers which have no named entry.

### name
Text representing the name of the parameter

### standardname
NetCDF CF parameter standard name

### longname
NetCDF CF parameter long name

### unit
NetCDF CF parameter unit

### aggregatetype
NetCFD method of aggregate

### aggregatelength
Aggregate duration in minutes

### NetCDF to grid data parameter configuration example

{
  "radonname" : "T-K",
  "standardname" : "air_temperature",
  "name" : "air_temperature",
  "longname" : "Temperature",
  "unit" : "K"
}

## Docker

SmartMet Server can be dockerized. This [tutorial](docs/docker.md)
explains how to how to configure the server when using Docker.


# Example queries

* Default producer, GRIB2
<pre><code>
https://data.fmi.fi/fmi-apikey/your-api-key/download?param=temperature,windspeedms,winddirection,humidity,dewpoint&format=grib2
</code></pre>
* Default producer, GRIB1
<pre><code>
https://data.fmi.fi/fmi-apikey/your-api-key/download?param=temperature,windspeedms,winddirection,humidity,dewpoint&format=grib1
</code></pre>
* Harmonie (MEPS) pressure levels, GRIB2
<pre><code>
https://data.fmi.fi/fmi-apikey/your-api-key/download?param=temperature,windspeedms,winddirection,humidity,dewpoint&format=grib2&producer=harmonie_scandinavia_pressure
</code></pre>
* Harmonie (MEPS) pressure levels, GRIB1
<pre><code>
https://data.fmi.fi/fmi-apikey/your-api-key/download?param=temperature,windspeedms,winddirection,humidity,dewpoint&format=grib1&producer=harmonie_scandinavia_pressure
 </code></pre>
* Harmonie (MEPS), bbox, GRIB2
<pre><code>
https://data.fmi.fi/fmi-apikey/your-api-key/download?param=temperature,windspeedms,winddirection,humidity,dewpoint&format=grib2&producer=harmonie_scandinavia_surface&bbox=19,59,30,70
</code></pre>
* Harmonie (MEPS), bbox, GRIB1
<pre><code>
https://data.fmi.fi/fmi-apikey/your-apt-key/download?param=temperature,windspeedms,winddirection,humidity,dewpoint&format=grib1&producer=harmonie_scandinavia_surface&bbox=19,59,30,70
</code></pre>
* Harmonie (MEPS) pressure levels, bbox, levels, GRIB2
<pre><code>
https://data.fmi.fi/fmi-apikey/your-api-key/download?param=temperature,windspeedms,winddirection,humidity,dewpoint&format=grib2&producer=harmonie_scandinavia_pressure&bbox=19,59,30,70&levels=50,100,300,400,700
</code></pre>
* Harmonie (MEPS), bbox, timestep, GRIB2
<pre><code>
https://data.fmi.fi/fmi-apikey/your-api-key/download?param=temperature,windspeedms,winddirection,humidity,dewpoint&format=grib2&model=harmonie_scandinavia_surface&bbox=19,59,30,70&amp;timestep=180
</code></pre>

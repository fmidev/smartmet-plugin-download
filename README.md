Table of Contents
=================

  * [SmartMet Server](#SmartMet Server)
  * [Introduction](#introduction)
  * [Interface](#interface)
    * [Levels](#levels)
    * [Parameters](#parameters)
      * [Regular parameter names](#regular-parameter-names)
      * [Numeric parameters](#numeric-parameters)
    * [Time control](#time-control)
      * [Origintime](#origintime)
      * [Time interval](#time-interval)
      * [Timestep](#timestep)
      * [Number of timesteps](#number-of-timesteps)
    * [Packing](#packing)
    * [Projections](#projections)
  * [Configuration](#configuration)
    * [Main configuration file](#main-configuration-file)
      * [GRIB Configuration](#grib-configuration)
      * [NetCDF Configuration](#netcdf-configuration)
      * [Producers](#producers)
      * [Path for temporary NetCDF files.](#path-for-temporary-netcdf-files)
    * [GRIB_API to QueryData parameter mapping.](#grib_api-to-querydata-parameter-mapping)
      * [gribid](#gribid)
      * [QueryDataid](#querydataid)
      * [name](#name)
      * [offset](#offset)
      * [divisor](#divisor)
      * [leveltype :](#leveltype-)
      * [levelvalue](#levelvalue)
      * [center](#center)
      * [aggregatetype](#aggregatetype)
      * [aggregatelength](#aggregatelength)
      * [templatenumber](#templatenumber)
    * [NetCDF to QueryData parameter mapping](#netcdf-to-querydata-parameter-mapping)
      * [QueryDataid](#querydataid-1)
      * [name](#name-1)
      * [offset](#offset-1)
      * [divisor](#divisor-1)
      * [leveltype :](#leveltype--1)
      * [levelvalue](#levelvalue-1)
      * [aggregatetype](#aggregatetype-1)
      * [aggregatelength](#aggregatelength-1)
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
origin time). With this plugin we can define the grid resolution by
selecting every Nth grid point along the x and the y direction and
also by grid size based on the number of cells in the x and the y
direction.

# Interface
The interface derives from the timeseries module. See it's documentation for the options below:

* starttime, endtime
* timestep=X (minutes 15,60,180,360,720,1440)
* timesteps=X
* level=X
* levels=1000,925,850,700,500,400,300,250,200,100,50
* param=Temperature,DewPoint,Humidity,WindSpeedMS,WindDirection,WindUMS,WindVMS,Pressure,Precipitation1h,TotalCloudCover,GeopHeight
* projection (f.eg. projection=epsg:4326)
* format=GRIB1|GRIB2|netcfd|qd
* model(or producer)=hirlam_eurooppa_pinta|hirlam_eurooppa_painepinta
* gridstep=x,y (select every Nth cell to the x and the y direction)
* gridsize=x,y (number of cells in the x and the y direction). Applies only when reprojections may change the output grid.
* gridresolution=x,y (distance between cells in the x and the y direction in kilometers). Similar to gridsize option, but in geographical units.
* bbox=left,bottom,right,top (f.eg. bbox=22,64,24,68)
* gridcenter=centerx,centery,offsetx,offsety; an alternative way to define bbox using grid center's lon/lat coordinates and offsets to grid corners in kilometers

## Levels
By default all levels are printed. This can be limited with the options.

<pre><code>level=value
levels=value1,value2,...
</code></pre>

For example, to get levels 700 hPa and 850 hPa, give levels=700,850 as values in the  levels option.

## Parameters

The parameter option is used for selecting the parameters that are to be extracted from the model. The option format is as follows:

<pre><code>param=name1,name2,...,nameN </code></pre>

### Regular parameter names

Regular QueryData parameter names such as Temperature, WindSpeedMS are are recognized as it  is.

### Numeric parameters
QueryData numbers are allowed as parameter identifiers. The QueryData parameters can be fetched using qdinfo. For example, the QueryDataid, 1, referes to the "air_pressure_at_sea_level", which has standardname "air_pressure_at_sea_level" and a longname  "Air pressure at sea level".


## Time control
There are three basic methods for extracting/accessing the data with timestamps:

1. Using a specific timestep in minutes
2. Extracting specific hours or times of the day
The timezone determines which times will actually be extracted from the original data. For example, if one extracts temperature data at 3 hour intervals for Helsinki and Stockholm, the wall clock will be the same in each country for the output data, but the UTC times differ by one hour.

It is not possible to specify both a timestep and specific times, but it is possible to request specific hours and times simultaneously.


### Origintime

The query string field "origintime" can be used to select a specific data file to be read by the SmartMet server. Each QueryData file should have a distinct origintime, i.e,   the time the data was created. This is used more as a unique identification number for a data file than as a definition of the data contents. Origintime itself does not necessarily relate to the time parameters in the data rows. For example,  the QueryData files can be created with the same forecast contents multiple times, but the origintime changes for  each run.

The origintime for the data to use can be chosen with the option:

<pre><code>origintime=t</code></pre>

where t can be a 12 character timestamp.  
If this field  is not used, SmartMet server uses the earliest origintime available for the used data model/producer.

### Time interval
Option syntax: 
<pre><code>starttime=t1&endtime=t2 </code></pre>
Both options can be omitted. The default values for the options are:
* starttime = origin time of the dataset
* endtime = end time of the dataset

Normally the times are given in 12-digit timestamp form.

Unlike in Timeseries-plugin and in common practice, if no offset is defined in given timestamp, the time is understood as UTC-times. For example, 2016-02-13T14:00+02 is 12:00 UTC and 14:00 in localtime, but the timestamp 201602131400 or 2016-02-13T14:00 is interpreted as 14:00 UTC and 16:00 in localtime.

If the value is an integer, it is assumed to be epoch seconds. However, if the integer is preceded by a sign, it is interpreted as the number of minutes to be added to wall clock.

Both starttime and endtime can also be the special value "data"

<pre><code>data </code></pre>
the relevant times depend on the model being used.

Note that the 
<pre><code>timesteps </code></pre>
keyword can be used alternatively to specify the end time.

### Timestep
Option syntax: 
<pre><code> timestep=data|minutes</code></pre>

If the timestep is "data", the selected model timesteps are used. Minimum allowed timestep is the data timestep. For example, for HIRLAM, the timetep  is 60  and for Harmonie, the timestep is  15.

If the timestep is given in minutes, it  must be a divisor of 1440 (minutes in a day). For example, the timesteps  60, 120, 180, 240 or 360 which translate to 1, 2, 3, 4 and 6 hours respectively.

The given timestep is used to produce available data time points by counting from 00:00 hours forwards.


<pre><code><b>
The example below shows the timestep parameter's behaviour</b> 

If a timestep of value 180 is given, the produced time points are
00:00, 03:00, 06:00, 09:00, 12:00, 15:00, 18:00, 21:00. Time points
are in local time.

When starttime is +0h (the request time) for an example 2014-11-15
15:58 in local time (+02:00 offset from UTC in Finland during
the  winter time).

From timestep produced time points it is then checked what is the next
available time point for a given starttime. In this example when the
starttime is at 15:58 the next available time point is at 18:00.

The data returned (at least in GRIB2-format) now contains a GRIB
message that contains the time for 2014-11-15T16:00Z (UTC time). 
The next message contains data for time 2014-11-15T19:00Z which 
is three  hours after the previous time point.
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

## Projections

The plugin uses gdal, <a href="www.gdal.org">Geospatial Data Abstraction Library</a>, so in theory any projection known by proj.4 may work. In practice, only some of them are feasible for GRIB. The following projections are safe choices:

* EPSG:4326
* EPSG:3995
* stereographic (Polar Stereographic, latitude of origin 60, central meridian 0)

If projection is not given, the  original projection of the data is returned.

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
Configuration file for GRIB parameter mapping can be given as follows.  If GRIB
output is not required this setting can be omitted. The configuraion file for the GRIB parameter mapping is given in the subsection  [GRIB_API to QueryData parameter mapping.](#grib_api-to-querydata-parameter-mapping)

<pre><code> 
gribconfig = <filename>;
</code></pre>

### NetCDF Configuration

Configuration file for NetCDF parameter mapping can be given as follows.  If NetCDF output is not required this setting can be omitted. The configuraion file for the NetCDF  parameter mapping is given in the subsection [NetCDF to QueryData parameter mapping](#netcdf-to-querydata-parameter-mapping)

<pre><code> 
netcdfconfig = <filename>;
</code></pre>


### Producers
The Default producer is "hirlam". We can specify the names of other producers in  a list and can enable the producer which is to be used. The default producer is the top in the list. Producer specific settings are loaded in the order set in producers.enabled.

### Path for temporary NetCDF files.
<pre><code> 
tempdirectory = <pathname>;
</code></pre>

* default: /dev/shm. 
* Note: for better performance memory mapped file system should be used. Complete NetCDF files (multiple files when  processing simultaneous download requests) are written to this  location; disk space availability could become an issue.

## GRIB_API to QueryData parameter mapping.

Configuration file grib.json contains  a list of elements and each element represents a single QueryData-GRIB_API mapping [ mapping_node1, mapping_node2 ...]

Each mapping_node contains key-value-pairs where the key can be any of the following: 

gribid, QueryDataid, name, offset, divisor, leveltype, levelvalue, centre, aggregatetype, aggregatelength, templatenumber

### gribid
value of a GRIB-API paramId

### QueryDataid
value of a QueryData parameter number

### name
Text representing the name of the element

### offset
out_value = ( in_value + offset ) / divisor
default: 0

### divisor
out_value = ( in_value + offset ) / divisor
default = 1

### leveltype :
The level type definition in QueryData

### levelvalue
The level value for QueryData

### center
GRIB-API centre definition. efkl for the local FMI definitions. Note: center != centre

### aggregatetype
GRIB-API method of aggregate

### aggregatelength
Duration of aggregation  in minutes
For example, for  00-24 UTC maximum temperature at 2 meters, which has a gribid of 51 and  QueryDataid 358, has an aggregate length of 1440 minutes, i.e., 24 hours * 60 minutes.

### templatenumber

<a href="http://apps.ecmwf.int/codes/grib/format/grib2/ctables/4/0">GRIB-APIn productDefinitionTemplateNumber</a>

## NetCDF to QueryData parameter mapping

The configuration file  netcdf.json which contains  a list of elements and each element represents a single QueryData-NetCDF_API mapping [ mapping_node1, mapping_node2 ...]

Each mapping_node contains key-value-pairs where the key can be any of the following: 

QueryDataid, name, standardname, longname, unit, divisor, offset,aggregatetype, aggregatelength.

### QueryDataid
value of a QueryData parameter number

### name
Text representing the name of the element

### offset
out_value = ( in_value + offset ) / divisor
default: 0

### divisor
out_value = ( in_value + offset ) / divisor
default = 1

### leveltype :
The level type definition in QueryData

### levelvalue
The level value for QueryData


### aggregatetype
NetCFD method of aggregate

### aggregatelength
Aggregate duration  in minutes.


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

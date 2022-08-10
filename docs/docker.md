# Tutorial

This tutorial explains how to configure the Download plugin when using Docker.

## Prereqs

Docker software has been installed on some Linux server where you have access to and the smartmetserver docker container is up and running.
The Download plugin and the configuration file it uses have been defined in the main configuration file smartmet.conf already.

### File download.conf

The purpose of the file download.conf is to define where the format specific configuration files grib.json and  netcdf.json for parameter mapping can be found.

If you followed the “SmartMet Server Tutorial (Docker)” you have your configuration folders and files in the host machine at $HOME/docker-smartmetserver/smartmetconf but inside Docker they show up under /etc/smartmet. 

1. Go to the correct directory and enter command below to review the file:

```
$ less download.conf
```
You will see something like this:
```
gribconfig = "/etc/smartmet/plugins/download/grib.json";
netcdfconfig = "/etc/smartmet/plugins/download/netcdf.json";
```

### File grib.json

This file contains a list of elements and each element represents a single QueryData-GRIB_API mapping [ mapping_node1, mapping_node2 ...].

Each mapping_node contains key-value-pairs where the key can be any of the following:

* gribid
* QueryDataid
* name
* offset 
* divisor
* leveltype 
* levelvalue
* centre
* aggregatetype
* aggregatelength
* templatenumber

Go to the directory where the grib.json file is and review the file:

```
$ less grib.json
```
You will see something like this:
```
// http://www.ecmwf.int/publications/manuals/d/gribapi/param/
// SmartMet from GRIB to QueryData Parameter Conversion Table
[
    //Boundary Layer Height
    {
        "gribid" : 159,
        "newbaseid" : 180,
        "name" : "BoundaryLayerHeight"
    },
    //Snow Depth in m
    {
        "gribid" : 3066,
        "newbaseid" : 51,
        "name" : "sd"
    },
    //Land-Sea mask
    {
        "gribid" : 172,
        "newbaseid" : 281,
        "name" : "LandSeaCoverage"
    },
etc...
```

### File netcdf.json

This file contains a list of elements and each element represents a single QueryData-NetCDF_API mapping [ mapping_node1, mapping_node2 ...].

Each mapping_node contains key-value-pairs where the key can be any of the following:

* QueryDataid
* name
* standardname
* longname
* unit
* divisor
* offset
* aggregatetype
* aggregatelength

Review the file netcdf.json:
```
$ less netcdf.json
```

You will see something like this:
```
[
    // Boundary Layer Height
    //  grib: 159;180;BoundaryLayerHeight
    //
    {
        "newbaseid" : 180,
        "name" : "atmosphere_boundary_layer_thickness",
        "standardname" : "atmosphere_boundary_layer_thickness",
        "longname" : "Boundary layer height",
        "unit" : "m"
    },
    // Land-Sea mask (0 - 1)
    //  grib: 172;281;LandSeaCoverage
    //
    {
        "newbaseid" : 281,
        "name" : "land_binary_mask",
        "standardname" : "land_binary_mask",
        "longname" : "Land-Sea mask",
        "unit" : "1"
etc...
```
2. Use Nano or some other editor to update these files if needed.

3. Test the plugin. The URL of the HTTP request contains parameters that have to be delivered to the download plugin. You should be able to save the file.

http://hostaname:8080/download?param=temperature,windspeedms,winddirection,humidity,dewpoint&format=grib2 

![](https://github.com/fmidev/smartmet-plugin-wms/wiki/images/Download.PNG)

**Note:** Replace hostname with your host machine name, by localhost or by host-ip. This depends on the machine that you are using (Cloud server/Native Linux/Mac or Windows).
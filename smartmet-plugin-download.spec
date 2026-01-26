%define DIRNAME download
%define SPECNAME smartmet-plugin-%{DIRNAME}
Summary: SmartMet Download Plugin
Name: %{SPECNAME}
Version: 25.11.27
Release: 1%{?dist}.fmi
License: MIT
Group: SmartMet/Plugins
URL: https://github.com/fmidev/smartmet-plugin-download
Source0: %{name}.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

# https://fedoraproject.org/wiki/Changes/Broken_RPATH_will_fail_rpmbuild
%global __brp_check_rpaths %{nil}

%if 0%{?rhel} && 0%{rhel} < 9
%define smartmet_boost boost169
%else
%define smartmet_boost boost
%endif

BuildRequires: %{smartmet_boost}-devel
BuildRequires: gcc-c++
BuildRequires: gdal312-devel
BuildRequires: eccodes-devel <= 2.31.1
BuildRequires: jsoncpp-devel >= 1.8.4
BuildRequires: libconfig17 >= 1.7.3
BuildRequires: smartmet-library-spine-devel >= 25.10.27
BuildRequires: smartmet-library-macgyver-devel >= 25.11.5
BuildRequires: smartmet-library-timeseries-devel >= 25.8.1
BuildRequires: smartmet-library-newbase-devel >= 25.3.20
BuildRequires: smartmet-library-grid-content-devel >= 25.11.4
BuildRequires: smartmet-library-grid-files-devel >= 25.11.27
BuildRequires: netcdf-devel
BuildRequires: smartmet-engine-querydata-devel >= 25.9.17
BuildRequires: smartmet-engine-geonames-devel >= 25.11.3
BuildRequires: smartmet-engine-grid-devel >= 25.11.27
BuildRequires: netcdf-cxx4-devel
BuildRequires: bzip2-devel
BuildRequires: jasper-devel
Requires: gdal312-libs
Requires: eccodes <= 2.31.1
Requires: jsoncpp >= 1.8.4
Requires: libconfig17 >= 1.7.3
Requires: jasper-libs
Requires: smartmet-library-macgyver >= 25.11.5
Requires: smartmet-library-timeseries >= 25.8.1
Requires: smartmet-library-spine >= 25.10.27
Requires: smartmet-library-newbase >= 25.3.20
Requires: smartmet-engine-querydata >= 25.9.17
Requires: smartmet-library-grid-content >= 25.11.4
Requires: smartmet-library-grid-files >= 25.11.27
Requires: smartmet-engine-grid >= 25.11.27
Requires: smartmet-server >= 25.10.27
Requires: %{smartmet_boost}-iostreams
Requires: %{smartmet_boost}-system
Requires: %{smartmet_boost}-thread
Requires: netcdf-cxx4
Provides: %{SPECNAME}
Obsoletes: smartmet-brainstorm-dlsplugin < 16.11.1
Obsoletes: smartmet-brainstorm-dlsplugin-debuginfo < 16.11.1
#TestRequires: %{smartmet_boost}-devel
#TestRequires: bzip2-devel
#TestRequires: eccodes <= 2.31.1
#TestRequires: redis
#TestRequires: gcc-c++
#TestRequires: libconfig17-devel
#TestRequires: smartmet-engine-geonames >= 25.11.3
#TestRequires: smartmet-engine-grid >= 25.10.15
#TestRequires: smartmet-engine-querydata >= 25.9.17
#TestRequires: smartmet-utils-devel >= 25.10.10
#TestRequires: smartmet-library-spine-plugin-test >= 25.10.27
#TestRequires: smartmet-library-newbase-devel >= 25.3.20
#TestRequires: smartmet-qdtools >= 25.11.3
#TestRequires: smartmet-test-data >= 25.8.13
#TestRequires: smartmet-test-db >= 25.6.18
#TestRequires: smartmet-engine-grid-test >= 25.10.15
#TestRequires: wgrib
#TestRequires: wgrib2
#TestRequires: zlib-devel

# makefile.inc side effect (otherwise fails on top level Makefile)
#TestRequires: gdal312-devel
#TestRequires: jsoncpp-devel
#TestRequires: ctpp2-devel

%description
SmartMet Download Plugin

%prep
rm -rf $RPM_BUILD_ROOT

%setup -q -n %{SPECNAME}

%build -q -n %{SPECNAME}
make %{_smp_mflags}

%install
%makeinstall

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(0775,root,root,0775)
%{_datadir}/smartmet/plugins/download.so

%changelog
* Thu Nov 27 2025 Andris Pavēnis <andris.pavenis@fmi.fi> 25.11.27-1.fmi
- Repackage due to grid-file changes

* Thu Nov 13 2025 Andris Pavēnis <andris.pavenis@fmi.fi> 25.11.13-1.fmi
- Fix netCDF generation

* Tue Nov 11 2025 Mika Heiskanen <mika.heiskanen@fmi.fi> - 25.11.11-1.fmi
- Disabled useless stack trace when client requests data that is not available

* Mon Oct 27 2025 Andris Pavēnis <andris.pavenis@fmi.fi> 25.10.27-1.fmi
- Update due to smartmet-library-spine ABI changes

* Mon Oct 20 2025 Andris Pavēnis <andris.pavenis@fmi.fi> 25.10.20-1.fmi
- Use netcdf-cxx4 instead of old netcdf-cxx

* Wed Oct 15 2025 Mika Heiskanen <mika.heiskanen@fmi.fi> - 25.10.15-1.fmi
- Repackaged due to grid-files API changes

* Tue Sep  9 2025 Pertti Kinnia <pertti.kinnia@fmi.fi> - 25.9.9-1.fmi
- Added support for fetching grid content data reprojected to given grid geometry (BRAINSTORM-3241)

* Mon Sep  1 2025 Andris Pavēnis <andris.pavenis@fmi.fi> 25.9.1-1.fmi
- Update according to smartmet-library-spine ABI changes

* Tue Jun 17 2025 Pertti Kinnia <pertti.kinnia@fmi.fi> - 25.6.17-1.fmi
- Support for grid level type 21 on grib output (MostUnstableParcelDeparture, typeOfFirstFixedLevel 17); BRAINSTORM-3210

* Fri May  2 2025 Pertti Kinnia <pertti.kinnia@fmi.fi> - 25.5.2-1.fmi
- Log content record count and content server query parameters if number of content records is not what expected (PAK-4808)

* Tue Apr  8 2025 Mika Heiskanen <mika.heiskanen@fmi.fi> - 25.4.8-1.fmi
- Repackaged due to base library ABI changes

* Thu Apr  3 2025 Pertti Kinnia <pertti.kinnia@fmi.fi> - 25.4.3-2.fmi
- Fixed jScansPositively to follow bbox latitude value order with grid data grib output (BRAINSTORM-3150)

* Thu Apr  3 2025 Pertti Kinnia <pertti.kinnia@fmi.fi> - 25.4.3-1.fmi
- More grid data corner handling fixes (BRAINSTORM-3150)

* Wed Mar 26 2025 Pertti Kinnia <pertti.kinnia@fmi.fi> - 25.3.26-1.fmi
- Set global grid data corner longitudes to 0.1 and 360 on grib output (BRAINSTORM-3150)

* Thu Mar 20 2025 Pertti Kinnia <pertti.kinnia@fmi.fi> - 25.3.20-1.fmi
- Adjust global grid data corner longitudes (botleft -0.1, topright 0) to -180..180 or 0..360 range on latlon output which fixes iDirectionIncrementInDegrees calculation too (BRAINSTORM-3150)

* Wed Mar 19 2025 Mika Heiskanen <mika.heiskanen@fmi.fi> - 25.3.19-1.fmi
- Repackaged due to base library ABI changes

* Thu Feb 27 2025 Pertti Kinnia <pertti.kinnia@fmi.fi> - 25.2.27-1.fmi
- Scale grid data dx/dy values by 1000 (km to m) if less than 100 since old grid test data apparently has unscaled values and lot of tests fail. NOTE: should ENFUSER grid data be loaded with download, scaling (hack) must not be done anymore since ENFUSER grid resolution is 50m (or whatever); test data should be fixed/updated (BRAINSTORM-3142)
- Removed 2 tests which fetched grid data using both bbox and gridresolutiuon which is not supported

* Wed Feb 26 2025 Pertti Kinnia <pertti.kinnia@fmi.fi> - 25.2.26-1.fmi
- Store grib2 ensemble/perturbationnumber for ensemble data (BRAINSTORM-3141)

* Tue Feb 18 2025 Andris Pavēnis <andris.pavenis@fmi.fi> 25.2.18-1.fmi
- Update to gdal-3.10, geos-3.13 and proj-9.5

* Thu Jan  9 2025 Mika Heiskanen <mika.heiskanen@fmi.fi> - 25.1.9-1.fmi
- Repackaged due to GRID-library changes

* Tue Dec 31 2024 Mika Heiskanen <mika.heiskanen@fmi.fi> - 24.12.31-1.fmi
- Print optional apikey on errors

* Mon Dec 16 2024 Mika Heiskanen <mika.heiskanen@fmi.fi> - 24.12.16-1.fmi
- Relaxed eccodes dependencies in order to get a RHEL9 build

* Fri Dec 13 2024 Mika Heiskanen <mika.heiskanen@fmi.fi> - 24.12.13-1.fmi
- Fixed eccodes depenency

* Fri Nov  8 2024 Andris Pavēnis <andris.pavenis@fmi.fi> 24.11.8-1.fmi
- Repackage due to smartmet-library-spine ABI changes

* Wed Oct 23 2024 Mika Heiskanen <mika.heiskanen@fmi.fi> - 24.10.23-1.fmi
- Repackaged due to ABI changes

* Wed Oct 16 2024 Mika Heiskanen <mika.heiskanen@fmi.fi> - 24.10.16-1.fmi
- Repackaged due to ABI changes in grid libraries

* Tue Oct 15 2024 Mika Heiskanen <mika.heiskanen@fmi.fi> - 24.10.15-1.fmi
- Removed support for landscaped parameters as obsolete

* Tue Sep  3 2024 Andris Pavēnis <andris.pavenis@fmi.fi> 24.9.3-1.fmi
- Repackage due smartmlibrary-grid-data and smartmet-engine-querydata changes

* Wed Aug  7 2024 Andris Pavēnis <andris.pavenis@fmi.fi> 24.8.7-1.fmi
- Update to gdal-3.8, geos-3.12, proj-94 and fmt-11

* Tue Jul 30 2024 Andris Pavēnis <andris.pavenis@fmi.fi> 24.7.30-1.fmi
- DataStreamer: update according to qengine ABI change

* Fri Jul 12 2024 Andris Pavēnis <andris.pavenis@fmi.fi> 24.7.12-1.fmi
- Replace many boost library types with C++ standard library ones

* Mon Jun  3 2024 Mika Heiskanen <mika.heiskanen@fmi.fi> - 24.6.3-2.fmi
- Updated grid-content requirement

* Mon Jun  3 2024 Mika Heiskanen <mika.heiskanen@fmi.fi> - 24.6.3-1.fmi
- Repackaged due to library ABI changes on level information

* Thu May 16 2024 Andris Pavēnis <andris.pavenis@fmi.fi> 24.5.16-1.fmi
- Clean up boost date-time uses

* Wed May  8 2024 Mika Heiskanen <mika.heiskanen@fmi.fi> - 24.5.8-1.fmi
- Fixed spatial references to use shared pointers

* Tue May  7 2024 Andris Pavēnis <andris.pavenis@fmi.fi> 24.5.7-1.fmi
- Use Date library (https://github.com/HowardHinnant/date) instead of boost date_time

* Fri May  3 2024 Mika Heiskanen <mika.heiskanen@fmi.fi> - 24.5.3-1.fmi
- Repackaged due to changes in GRID libraries

* Fri Feb 23 2024 Mika Heiskanen <mika.heiskanen@fmi.fi> 24.2.23-1.fmi
- Full repackaging

* Tue Feb 20 2024 Mika Heiskanen <mika.heiskanen@fmi.fi> 24.2.20-1.fmi
- Repackaged due to grid-files ABI changes

* Mon Feb  5 2024 Mika Heiskanen <mika.heiskanen@fmi.fi> 24.2.5-1.fmi
- Repackaged due to grid-files ABI changes

* Tue Jan 30 2024 Mika Heiskanen <mika.heiskanen@fmi.fi> 24.1.30-1.fmi
- Repackaged due to newbase ABI changes

* Thu Jan  4 2024 Mika Heiskanen <mika.heiskanen@fmi.fi> - 24.1.4-1.fmi
- Repackaged due to TimeSeriesGenerator ABI changes

* Fri Dec 22 2023 Mika Heiskanen <mika.heiskanen@fmi.fi> - 23.12.22-1.fmi
- Repackaged due to ThreadLock ABI changes

* Thu Dec  7 2023 Pertti Kinnia <pertti.kinnia@fmi.fi> - 23.12.7-1.fmi
- Bug fix: handle missing generation info (e.g. unknown producer) when fetching both grid data and function parameters; data parameters are omitted

* Tue Dec  5 2023 Mika Heiskanen <mika.heiskanen@fmi.fi> - 23.12.5-2.fmi
- Repackaged due to an ABI change in SmartMetPlugin

* Tue Dec  5 2023 Pertti Kinnia <pertti.kinnia@fmi.fi> - 23.12.5-1.fmi
- Grid function parameter support on netcdf output

* Mon Dec  4 2023 Mika Heiskanen <mika.heiskanen@fmi.fi> - 23.12.4-1.fmi
- Repackaged due to QEngine ABI changes
- Grid function parameter support on grib output

* Fri Nov 17 2023 Pertti Kinnia <pertti.kinnia@fmi.fi> - 23.11.17-1.fmi
- Repackaged due to API changes in grid-files and grid-content

* Wed Nov 15 2023 Pertti Kinnia <pertti.kinnia@fmi.fi> - 23.11.15-1.fmi
- Use global (not producer specific) radon parameter configuration entries for grib1 output too (BRAINSTORM-2792)
- Use uppercase radon parameter and producer names (BRAINSTORM-2791)
- Added grid data tests. Note: currently grid data tests do not work (without few temporary kludges in code) since the test redis database parameter T-K has undefined leveltype 0 (BRAINSTORM-2741)

* Fri Nov 10 2023 Mika Heiskanen <mika.heiskanen@fmi.fi> - 23.11.10-1.fmi
- Repackaged due to API changes in grid-content

* Fri Nov  3 2023 Pertti Kinnia <pertti.kinnia@fmi.fi> - 23.11.3-1.fmi
- In addition to requested projection, take gridsize and gridresolution into account when determining whether grid source data is cropped or not. gridresolution has currently no effect with bbox since gridengine ignores it; requested resolution and resúlting number of cells hardly matches the given bbox exactly (BRAINSTORM-2778)
- Use grid.llbox for latlon and rotlat grids too instead of grid.crop.llbox. grid.crop.llbox is assumed to reflect source grid y -axis direction and grib.llbox corners are assumed to have increasing latitude order regardless of source grid y -axis direction (BRAINSTORM-2782)

* Tue Oct 31 2023 Pertti Kinnia <pertti.kinnia@fmi.fi> - 23.10.31-1.fmi
- Fixed grid query starttime=data option to return data starting from 1'st timestep instead of 'now' and endtime=data to return data upto last timestep instead of 'now' (BRAINSTORM-2775)

* Mon Oct 30 2023 Mika Heiskanen <mika.heiskanen@fmi.fi> - 23.10.30-1.fmi
- Repackaged due to ABI changes in GRID libraries

* Fri Oct 27 2023 Pertti Kinnia <pertti.kinnia@fmi.fi> - 23.10.27-1.fmi
- Enable use of grib1/grib2 parameter configuration blocks for nongrid parameters too to be able to set parameter discipline, category and parameter number (etc) instead of edition independent paramId which for some reason currently does not work atleast for paramId 260268

* Wed Oct 25 2023 Pertti Kinnia <pertti.kinnia@fmi.fi> - 23.10.25-1.fmi
- Fixed parsing of request parameter 'timestep=data'

* Fri Oct 20 2023 Pertti Kinnia <pertti.kinnia@fmi.fi> - 23.10.20-1.fmi
- Take grib y -axis scanning direction into account when setting latitude of first/last grid point (BRAINSTORM-2759)

* Wed Oct 18 2023 Pertti Kinnia <pertti.kinnia@fmi.fi> - 23.10.18-1.fmi
- Since it seems setting grib handle keys/values repeatedly slows down, set grib geomertry for each grid only if source=gridmapping, and if source=gridcontent set parameter (discipline, category, parameternumber), level etc. only when radon parameter changes (avoid unnecessary settings when looping time instants and parameter runs in outer loop as by default). Do not apply scaling at all when source=gridcontent since scale and offset values are always fixed (1 and 0) and would have no effect (BRAINSTORM-2747)
- Fixed gridstep handling for grid data (BRAINSTORM-2748)

* Thu Oct 12 2023 Andris Pavēnis <andris.pavenis@fmi.fi> 23.10.12-1.fmi
- Repackage due to smartmet-library-grid-files and smartmet-library-grid-files changes

* Thu Oct  5 2023 Pertti Kinnia <pertti.kinnia@fmi.fi> - 23.10.5-1.fmi
- Load grid parameter content records by using producer generation ids instead of producer name, thus avoiding loading lot of unneeded content records (BRAINSTORM-2728)

* Fri Sep 29 2023 Mika Heiskanen <mika.heiskanen@fmi.fi> - 23.9.29-1.fmi
- Repackaged due to changes in grid-libraries

* Mon Sep 25 2023 Pertti Kinnia <pertti.kinnia@fmi.fi> - 23.9.25-1.fmi
- Use grid.crop.llbox as cropped latlon bbox (BRAINSTORM-2727)

* Wed Sep 13 2023 Pertti Kinnia <pertti.kinnia@fmi.fi> - 23.9.13-1.fmi
- Added grib output support for mean sea level grid source data (BRAINSTORM-2721)
- Ignore parameters with unsupported level types at query initialization instead of throwing error after fetching data (BRAINSTORM-2720)

* Tue Sep 12 2023 Pertti Kinnia <pertti.kinnia@fmi.fi> - 23.9.12-1.fmi
- Fetch multiple grid data parameters (gridparamblocksize=n) or timesteps (gridtimeblocksize=n) as a block if requested, possibly resulting better throughput depending on the layout of underlying grid data
- Added request parameter (chunksize=n) to set nondefault grib output chunk size to avoid unnecessary copying of data to chunk buffer. Small chunk size is used for grid content data by default

* Mon Sep 11 2023 Mika Heiskanen <mika.heiskanen@fmi.fi> - 23.9.11-1.fmi
- Repackaged due to ABI changes in grid-files

* Wed Aug 30 2023 Pertti Kinnia <pertti.kinnia@fmi.fi> - 23.8.30-1.fmi
- Fixed Query object usage causing crashes (BRAINSTORM-2699), caused by changes made Aug 4
- Fixed bug in checking default value for grib2 tablesversion

* Tue Aug 22 2023 Pertti Kinnia <pertti.kinnia@fmi.fi> - 23.8.22-1.fmi
- Added grib output support for nominal top level grid source data

* Fri Aug 11 2023 Pertti Kinnia <pertti.kinnia@fmi.fi> - 23.8.11-1.fmi
- Load grid data producers too at startup to use named config settings (e.g. originating centre) for the producers
- Using grib paramconfig setting name 'centre' (in addition to 'center') to specify originating centre
- Added optional config setting for default grib tables version

* Fri Aug  4 2023 Pertti Kinnia <pertti.kinnia@fmi.fi> - 23.8.4-1.fmi
- Store and use parsed radon parameter name fields instead of reparsing

* Fri Jul 28 2023 Andris Pavēnis <andris.pavenis@fmi.fi> 23.7.28-1.fmi
- Repackage due to bulk ABI changes in macgyver/newbase/spine

* Wed Jul 12 2023 Andris Pavēnis <andris.pavenis@fmi.fi> 23.7.12-1.fmi
- Use postgresql 15, gdal 3.5, geos 3.11 and proj-9.0

* Tue Jul 11 2023 Mika Heiskanen <mika.heiskanen@fmi.fi> - 23.7.11-1.fmi
- Repackaged due to QEngine API changes

* Tue Jun 13 2023 Mika Heiskanen <mika.heiskanen@fmi.fi> - 23.6.13-1.fmi
- Support internal and environment variables in configuration files

* Tue Jun  6 2023 Mika Heiskanen <mika.heiskanen@fmi.fi> - 23.6.6-1.fmi
- Repackaged due to GRID ABI changes

* Tue May 30 2023 Andris Pavēnis <andris.pavenis@fmi.fi> 23.5.30-1.fmi
- Repackage due to omniORB upgrade to 4.3.0

* Wed May 24 2023 Pertti Kinnia <pertti.kinnia@fmi.fi> - 23.5.24-1.fmi
- Use newbase parameter id as netcdf variable name suffix for querydata producers; BRAINSTORM-2611

* Fri May 12 2023 Pertti Kinnia <pertti.kinnia@fmi.fi> - 23.5.12-1.fmi
- New release version. Changes and bug fixes to grid data query with radon names:
- Create netcdf level dimensions for leveltype/levels sets instead of leveltype/level
- Fixed bug in setting data to netcdf variables; level index was not set correctly
- grib output level value division by 100 for pressure level data only
- Changed radon name level and forecastnumber range delimiter back to '-'

* Mon Apr 17 2023 Mika Heiskanen <mika.heiskanen@fmi.fi> - 23.4.17-1.fmi
- Repackaged due to GRID ABI changes

* Tue Mar 28 2023 Pertti Kinnia <pertti.kinnia@fmi.fi> - 23.3.28-1.fmi
- Allow negative level value/range for height level (e.g. SAL-PSU:HBM_EC:608:6:-5:1:-1)
- Changed level (and forecastnumber) range delimiter to '/' since negative values are accepted

* Tue Mar 21 2023 Pertti Kinnia <pertti.kinnia@fmi.fi> - 23.3.21-1.fmi
- Bug fix to selecting latest common origintime for grid data
- Fixed bug in handling request parameter source=grid (query with radon names), the request parameter value is tested in few places instead of using enumerated data source value
- Grid data query object's mParameterKeyType was left unset; set it to FMI_NAME

* Fri Mar 17 2023 Pertti Kinnia <pertti.kinnia@fmi.fi> - 23.3.17-1.fmi
- If starttime or endtime option is not given, using default time to query all available grid content information
- In addition to &source=gridcontent, request parameter &source=grid is taken as grid content query too and radon parameter names are expected to be given

* Thu Mar 16 2023 Pertti Kinnia <pertti.kinnia@fmi.fi> - 23.3.16-1.fmi
- Fixed bug in searching grib parameter configuration entry for radon parameters

* Tue Mar 14 2023 Pertti Kinnia <pertti.kinnia@fmi.fi> - 23.3.14-1.fmi
- Added setting of grib1 table2Version and fixed setting of grib1 parameter number
- Changed grib1 param configuration 'tableVersion' field name to 'table2Version'
- Use configured grib format specific key to set aggregation type for data fetched using radon names. Since template number is not available in radon, if it is not manually set in parameter configuration use NormalProduct or EnsembleForecast depending on forecast type. That does not work for all parameters though, correct template numbers for the parameters must be manually entered to configuration when needed. Also parameter's aggregate period length is unavailable in radon and must be manually entered to configuration or it will be left unset on output
- Load radon producer name too from grib configuration since configuration for radon parameters can be producer specific. Also load grib1/2 aggregation type and grib1 TableVersion and parameter number
- Added producer name to GribParamIdentification since configuration for radon parameters can be producer specific. Store grib1/2 aggregation type and grib1 TableVersion too
- When checking for known grib parameters by radon names, check for producer specific parameter configuration first and non producer specific configuration (wmo parameter numbering) secondarily

* Fri Mar 10 2023 Pertti Kinnia <pertti.kinnia@fmi.fi> - 23.3.10-1.fmi
- Fixed check for latest validtime of parameter's newest analysis to be equal to or later than latest validtime for 2'nd newest analysis. The is obsolete though since run status is now taken into account when metadata is collected
- Use producer specific grib parameter configuration block for radon parameter if such exists

* Mon Mar  6 2023 Pertti Kinnia <pertti.kinnia@fmi.fi> - 23.3.6-1.fmi
- Use cached grid content data instead of (unintentionally) loading it from redis
- Ignore intermediate grid content data
- When loading parameter configuration set parameter name from parameter's radon name if name attribute is not given

* Fri Mar  3 2023 Pertti Kinnia <pertti.kinnia@fmi.fi> - 23.3.3-1.fmi
- Fixed height/depth leveltype handling in netcdf output; level dimension was not created

* Tue Feb 28 2023 Pertti Kinnia <pertti.kinnia@fmi.fi> - 23.2.28-1.fmi
- Support grid data queries with radon parameter names when source=gridcontent (STU-20107)

* Mon Feb 13 2023 Mika Heiskanen <mika.heiskanen@fmi.fi> - 23.2.13-1.fmi
- Fixed stack trace print for the host name

* Thu Jan 19 2023 Mika Heiskanen <mika.heiskanen@fmi.fi> - 23.1.19-1.fmi
- Repackaged due to ABI changes in grid libraries

* Fri Dec 16 2022 Mika Heiskanen <mika.heiskanen@fmi.fi> - 22.12.16-1.fmi
- Allow POST requests

* Mon Dec 12 2022 Mika Heiskanen <mika.heiskanen@fmi.fi> - 22.12.12-1.fmi
- Repackaged due to ABI changes

* Mon Dec  5 2022 Andris Pavēnis <andris.pavenis@fmi.fi> 22.12.5-1.fmi
- Check HTTP request type and handle only POST and OPTIONS requests

* Tue Nov  8 2022 Mika Heiskanen <mika.heiskanen@fmi.fi> - 22.11.8-1.fmi
- Repackaged due to base library ABI changes

* Thu Oct 20 2022 Mika Heiskanen <mika.heiskanen@fmi.fi> - 22.10.20-1.fmi
- Repackaged due to base library ABI changes

* Mon Oct 10 2022 Mika Heiskanen <mika.heiskanen@fmi.fi> - 22.10.10-1.fmi
- Repackaged due to base library ABI changes

* Wed Oct  5 2022 Mika Heiskanen <mika.heiskanen@fmi.fi> - 22.10.5-1.fmi
- Do not use boost::noncopyable

* Thu Aug 25 2022 Mika Heiskanen <mika.heiskanen@fmi.fi> - 22.8.25-1.fmi
- Use a generic exception handler for configuration file errors

* Wed Aug 24 2022 Mika Heiskanen <mika.heiskanen@fmi.fi> - 22.8.24-1.fmi
- Fixed resolution calculations in DataStreamer

* Mon Aug 22 2022 Pertti Kinnia <pertti.kinnia@fmi.fi> - 22.8.22-1.fmi
- Removed WGS84 ifdefs; merge from master_newbase_neutral_BRAINSTORM-2328

* Tue Aug 10 2022 Pertti Kinnia <pertti.kinnia@fmi.fi> - 22.8.10-1.fmi
- Fixed grid data level interpolation checking related bugs; BRAINSTORM-2378
- Removed unused variables and added internal error checking; BRAINSTORM-2374

* Tue Aug  2 2022 Pertti Kinnia <pertti.kinnia@fmi.fi> - 22.8.2-1.fmi
- Check for empty query result for 1'st grid when initializing query; BRAINSTORM-2370

* Thu Jul 28 2022 Mika Heiskanen <mika.heiskanen@fmi.fi> - 22.7.28-1.fmi
- Repackaged due to QEngine ABI change

* Tue Jun 21 2022 Andris Pavēnis <andris.pavenis@fmi.fi> 22.6.21-1.fmi
- Add support for RHEL9, upgrade libpqxx to 7.7.0 (rhel8+) and fmt to 8.1.1

* Thu Jun 16 2022 Pertti Kinnia <pertti.kinnia@fmi.fi> 22.6.16-1.fmi
- Still handling geographic epsg projections as latlon instead of loading sr using epsg code, problems with bbox

* Tue Jun 14 2022 Pertti Kinnia <pertti.kinnia@fmi.fi> 22.6.14-1.fmi
- Removed WGS84 ifdefs. Test result changes mainly due to newbase changes. BRAINSTORM-2328

* Tue May 31 2022 Andris Pavēnis <andris.pavenis@fmi.fi> 22.5.31-1.fmi
- Repackage due to smartmet-engine-querydata and smartmet-engine-observation ABI changes

* Tue May 24 2022 Mika Heiskanen <mika.heiskanen@fmi.fi> - 22.5.24-1.fmi
- Repackaged due to NFmiArea ABI changes

* Fri May 20 2022 Mika Heiskanen <mika.heiskanen@fmi.fi> - 22.5.20-1.fmi
- Repackaged due to ABI changes to newbase LatLon methods

* Tue May  3 2022 Andris Pavenis <andris.pavenis@fmi.fi> 22.5.3-1.fmi
- Repackage due to SmartMet::Spine::Reactor ABI changes

* Mon Mar 28 2022 Mika Heiskanen <mika.heiskanen@fmi.fi> - 22.3.28-1.fmi
- Repackaged due to ABI changes in grid-content library

* Mon Mar 21 2022 Andris Pavēnis <andris.pavenis@fmi.fi> 22.3.21-1.fmi
- Update due to changes in smartmet-library-spine and smartnet-library-timeseries

* Thu Mar 10 2022 Mika Heiskanen <mika.heiskanen@fmi.fi> - 22.3.10-1.fmi
- Repackaged due to base library ABI changes

* Wed Mar  9 2022 Pertti Kinnia <pertti.kinnia@fmi.fi> - 22.3.9-1.fmi
- Fixed bugs in handling (projected) epsg projections (axis mapping strategy was not set in coord.transformation etc)
- Added lcc and YKJ (nonnative) support for querydata source
- Setting correct target cs spheroid (and wkt for netcdf) with querydata source instead of fixed sphere
- Disabled automatic datum shift based on WKT DATUM for epsg projections
- Added (but disabled by ifdefs) metric bbox conversion and handling of geographic epsg projections by their definition instead of as newbase latlon

* Tue Mar  8 2022 Mika Heiskanen <mika.heiskanen@fmi.fi> - 22.3.8-1.fmi
- Use the new TimeSeries library

* Mon Mar  7 2022 Mika Heiskanen <mika.heiskanen@fmi.fi> - 22.3.7-1.fmi
- Repackaged due to base library API changes

* Mon Feb 28 2022 Mika Heiskanen <mika.heiskanen@fmi.fi> - 22.2.28-1.fmi
- Repackaged due to base library/engine ABI changes
- Improved projection support

* Wed Feb  9 2022 Mika Heiskanen <mika.heiskanen@fmi.fi> - 22.2.9-1.fmi
- Repackaged due to ABI changes in grid libraries

* Tue Jan 25 2022 Mika Heiskanen <mika.heiskanen@fmi.fi> - 22.1.25-1.fmi
- Repackaged due to ABI changes in libraries/engine

* Fri Jan 21 2022 Andris Pavēnis <andris.pavenis@fmi.fi> 22.1.21-1.fmi
- Repackage due to upgrade of packages from PGDG repo: gdal-3.4, geos-3.10, proj-8.2

* Thu Jan 13 2022 Pertti Kinnia <pertti.kinnia@fmi.fi> - 22.1.13-1.fmi
- Added netcdf YKJ support for querydata source

* Tue Dec  7 2021 Andris Pavēnis <andris.pavenis@fmi.fi> 21.12.7-1.fmi
- Update to postgresql 13 and gdal 3.3

* Mon Nov 15 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.11.15-1.fmi
- Repackaged due to ABI changes in base grid libraries

* Fri Oct 29 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.10.29-1.fmi
- Repackaged due to ABI changes in base grid libraries

* Tue Oct 19 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.10.19-1.fmi
- Repackaged due to ABI changes in base grid libraries

* Tue Oct 12 2021 Pertti Kinnia <pertti.kinnia@fmi.fi> - 21.10.12-1.fmi
- Added missing netcdf test configuration for grid-files

* Mon Oct 11 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.10.11-1.fmi
- Simplified grid storage structures

* Mon Oct  4 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.10.4-1.fmi
- Repackaged due to grid-files ABI changes

* Thu Sep 23 2021 Andris Pavēnis <andris.pavenis@fmi.fi> 21.9.23-1.fmi
- Repackage to prepare for moving libconfig to different directory

* Thu Sep 16 2021 Pertti Kinnia <pertti.kinnia@fmi.fi> - 21.9.16-1.fmi
- Added WGS84 test results
- Merge from WGS84 branch
- Copy data only for needed levels and validtimes when using in-memory qd (BRAINSTORM-2144)
- Fixed vertical interpolation bug with qd -format output (BRAINSTORM-2147)
- Bug fix; set in-memory qd level when level is not interpolated (BRAINSTORM-2149)
- Buf fix; allow missing parameter scaling information with qd -output (BRAINSTORM-2152)

* Thu Sep  9 2021 Andris Pavenis <andris.pavenis@fmi.fi> 21.9.9-1.fmi
- Repackage due to dependency change (libconfig->libconfig17)

* Tue Aug 31 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.8.31-1.fmi
- Repackaged due to Spine ABI changes

* Wed Aug 25 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.8.25-1.fmi
- Refactored classes a bit for clarity

* Tue Aug 17 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.8.17-1.fmi
- Use the new shutdown API

* Thu Aug 12 2021 Pertti Kinnia <pertti.kinnia@fmi.fi> - 21.8.12-1.fmi
- Using 64bit offset format for netcdf output to support files exceeding 2GB
- Removed some obsolete calls to requireNcFile(). NcFile object is created upon loading first grid, and file operations are valid thereafter

* Mon Aug  2 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.8.2-1.fmi
- Repackaged since GeoEngine ABI changed by switching to boost::atomic_shared_ptr

* Tue Jun  8 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.6.8-1.fmi
- Repackaged due to memory saving ABI changes in base libraries

* Tue Jun  1 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.6.1-1.fmi
- Repackaged due to ABI changes in grid libraries

* Tue May 25 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.5.25-1.fmi
- Repackaged due to API changes in grid-files

* Thu May  6 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.5.6-1.fmi
- Repackaged due to ABI changes in NFmiAzimuthalArea

* Fri Apr 16 2021 Pertti Kinnia <pertti.kinnia@fmi.fi> - 21.4.16-1.fmi
- Use source qd level order for qd output; BRAINSTORM-2045

* Wed Apr  7 2021 Pertti Kinnia <pertti.kinnia@fmi.fi> - 21.4.7-1.fmi
- Misc bug fixes and changes. Added LAEA grib2 support

* Fri Mar  5 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.3.5-1.fmi
- Merged grib-branch to master

* Wed Mar  3 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.3.3-1.fmi
- Grid-engine may now be disabled

* Tue Feb 23 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.2.23-1.fmi
- Fixed HIRLAM downloads to work

* Fri Feb 19 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.2.19-1.fmi
- Repackaged

* Wed Feb 17 2021 Pertti Kinnia <pertti.kinnia@fmi.fi> - 21.2.17-1.fmi
- Merge from master

* Mon Feb 15 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.2.15-1.fmi
- Updated to use new interpolation APIs

* Wed Feb  3 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.2.3-1.fmi
- Use time_t in the GRIB-engine calls

* Wed Jan 27 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.1.27-1.fmi
- Repackaged due to base library ABI changes

* Tue Jan 19 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.1.19-1.fmi
- Repackaged due to base library ABI changes

* Thu Jan 14 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.1.14-1.fmi
- Repackaged smartmet to resolve debuginfo issues

* Mon Jan 11 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.1.11-1.fmi
- Repackaged due to grid-files API changes

* Mon Jan  4 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.1.4-1.fmi
- Ported to GDAL 3.2

* Wed Dec 30 2020 Andris Pavenis <andris.pavenis@fmi.fi> - 20.12.30-1.fmi
- Rebuild due to jsoncpp upgrade for RHEL7

* Tue Dec 15 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.12.15-1.fmi
- Upgrade to pgdg12

* Thu Dec  3 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.12.3-1.fmi
- Repackaged due to library ABI changes

* Mon Nov 30 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.11.30-1.fmi
- Repackaged due to grid-content library API changes

* Tue Nov 24 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.11.24-1.fmi
- Repackaged due to library ABI changes

* Thu Oct 22 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.10.22-1.fmi
- Use new GRIB library API

* Thu Oct 15 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.10.15-1.fmi
- Repackaged due to library ABI changes

* Wed Oct  7 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.10.7-1.fmi
- Repackaged due to library ABI changes

* Tue Oct  6 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.10.6-1.fmi
- Enable sensible relative libconfig include paths

* Thu Oct  1 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.10.1-1.fmi
- Repackaged due to library ABI changes

* Wed Sep 23 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.9.23-1.fmi
- Use Fmi::Exception instead of Spine::Exception

* Fri Sep 18 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.9.18-1.fmi
- Repackaged due to library ABI changes

* Tue Sep 15 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.9.15-1.fmi
- Repackaged due to library ABI changes

* Mon Sep 14 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.9.14-1.fmi
- Repackaged due to library ABI changes

* Mon Sep  7 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.9.7-1.fmi
- Repackaged due to library ABI changes

* Mon Aug 31 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.8.31-1.fmi
- Repackaged due to library ABI changes

* Fri Aug 21 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.8.21-1.fmi
- Upgrade to fmt 6.2

* Tue Aug 18 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.8.18-1.fmi
- Repackaged due to grid library ABI changes

* Fri Aug 14 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.8.14-1.fmi
- Repackaged due to grid library ABI changes

* Mon Jun 15 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.6.15-1.fmi
- Renamed .so to enable simultaneous installation of download and gribdownload

* Mon Jun  8 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.6.8-1.fmi
- Repackaged due to base library changes

* Thu May 28 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.5.28-1.fmi
- Ported to use the latest newbase API

* Mon May 25 2020 Pertti Kinnia <pertti.kinnia@fmi.fi> - 20.5.25-1.fmi
- Some bugfixes. New release version

* Fri May 15 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.5.15-1.fmi
- Create enseble dimension only when applicable
- Added lock protecting netcdf metadata generation for grid data (not thread safe)
- Added grid support
- Added GRIB multifile property

* Wed May 13 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.5.13-1.fmi
- Repackaged since Spine Parameter class ABI changed

* Fri May  8 2020 Pertti Kinnia <pertti.kinnia@fmi.fi> - 20.5.8-1.fmi
- Fixed bug in netcdf output when skipping missing querydata parameters (BS-1823)

* Thu Apr 30 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.4.30-1.fmi
- Repackaged due to base library API changes

* Sun Apr 26 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.4.26-1.fmi
- Use Fmi::CoordinateMatrix

* Sat Apr 18 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.4.18-1.fmi
- Upgraded to Boost 1.69

* Wed Apr  8 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.4.8-1.fmi
- Protect the entire NetCDF metadata generation section with a mutex for thread safety

* Fri Apr  3 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.4.3-1.fmi
- Repackaged due to library API changes

* Thu Apr  2 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.4.2-1.fmi
- Fixed NetCDF mutex to be a global variable instead of a class member variable

* Tue Mar 31 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.3.31-1.fmi
- Use a mutex to protect opening temporary NetCDF files, which does not seem to be thread safe

* Thu Mar 19 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.3.19-1.fmi
- Improved safety of NcFile initialization

* Fri Dec 13 2019 Mika Heiskanen <mika.heiskanen@fmi.fi> - 19.12.13-1.fmi
- Repackaged due to NFmiArea API changes

* Thu Dec 12 2019 Mika Heiskanen <mika.heiskanen@fmi.fi> - 19.12.12-1.fmi
- Upgrade to GDAL 3.0

* Fri Nov 22 2019 Pertti Kinnia <pertti.kinnia@fmi.fi> - 19.11.22-1.fmi
- Added config setting 'logrequestdatavalues' (default is 0; no logging). Request is written to stderr if given number of output data values is exceeded

* Wed Nov 20 2019 Mika Heiskanen <mika.heiskanen@fmi.fi> - 19.11.20-1.fmi
- Rebuilt due to newbase API changes

* Thu Sep 26 2019 Mika Heiskanen <mika.heiskanen@fmi.fi> - 19.9.26-1.fmi
- Fixed thread safety issue (TSAN)
- Added support for ASAN & TSAN builds

* Mon Sep  2 2019 Mika Heiskanen <mika.heiskanen@fmi.fi> - 19.9.2-1.fmi
- Repackaged since Spine::Location ABI changed

* Mon Apr 15 2019 Pertti Kinnia <pertti.kinnia@fmi.fi> - 19.4.15-1.fmi
- Reapplied the changes to use in-memory querydata, just not using in-mem qd for multifile data (BS-1567)
- Increased output chunk size. Allocating output buffer from heap when loading netcdf data

* Fri Apr 12 2019 Mika Heiskanen <mika.heiskanen@fmi.fi> - 19.4.12-1.fmi
- Revert to older version, optimized code is bugged

* Fri Mar  8 2019 Pertti Kinnia <pertti.kinnia@fmi.fi> - 19.3.8-1.fmi
- Fixed bug with levels, current level index was left changed when loading in-memory querydata

* Wed Mar  6 2019 Pertti Kinnia <pertti.kinnia@fmi.fi> - 19.3.6-1.fmi
- Loading/using in-memory querydata object for each parameter for better throughput
- Loading/using input buffer for stringstream output for better querydata format throughput

* Fri Feb 15 2019 Mika Heiskanen <mika.heiskanen@fmi.fi> - 19.2.15-1.fmi
- Report client IP in stack traces to ease resolving problems

* Thu Nov  8 2018 Mika Heiskanen <mika.heiskanen@fmi.fi> - 18.11.8-1.fmi
- Do not throw in destructors in C++11

* Thu Nov  1 2018 Mika Heiskanen <mika.heiskanen@fmi.fi> - 18.11.1-1.fmi
- Set ncopts=NC_VERBOSE, thus not letting the NetCDF library to call exit()

* Thu Oct 18 2018 Mika Heiskanen <mika.heiskanen@fmi.fi> - 18.10.18-1.fmi
- Disabled stack trace if the requested model is unknown to reduce log sizes

* Mon Aug 13 2018 Mika Heiskanen <mika.heiskanen@fmi.fi> - 18.8.13-1.fmi
- Repackaged since Spine::Location size changed

* Tue Aug  7 2018 Pertti Kinnia <pertti.kinnia@fmi.fi> - 18.8.7-1.fmi
- Use default producer's originating centre if producer's centre has not been configured

* Wed Jul 25 2018 Mika Heiskanen <mika.heiskanen@fmi.fi> - 18.7.25-1.fmi
- Prefer nullptr over NULL

* Tue Jun 12 2018 Pertti Kinnia <pertti.kinnia@fmi.fi> - 18.6.12-1.fmi
- Limit number of data values for a single query (BS-1221). Default limit is 1G values

* Fri Jun  8 2018 Pertti Kinnia <pertti.kinnia@fmi.fi> - 18.6.8-1.fmi
- Check result grid size at query initialization (BS-1157)

* Tue Jun  5 2018 Pertti Kinnia <pertti.kinnia@fmi.fi> - 18.6.5-1.fmi
- netcdf: Use axis names 'X' and 'Y' for lon/lat coordinate axes too for CF-Convention Compliance (BS-970) 
- netcdf: Allocate lat/lon arrays from heap (BS-517) 

* Wed May 23 2018 Mika Heiskanen <mika.heiskanen@fmi.fi> - 18.5.23-1.fmi
- Use model's U/V reference information to control whether U and V are rotated when reprojecting

* Wed Apr 11 2018 Mika Heiskanen <mika.heiskanen@fmi.fi> - 18.4.11-1.fmi
- Allow any parseable parameter with a known newbase number even if marked to be a meta parameter

* Sat Apr  7 2018 Mika Heiskanen <mika.heiskanen@fmi.fi> - 18.4.7-1.fmi
- Upgrade to boost 1.66

* Tue Mar 20 2018 Mika Heiskanen <mika.heiskanen@fmi.fi> - 18.3.20-1.fmi
- Full recompile of all server plugins

* Thu Feb 22 2018 Mika Heiskanen <mika.heiskanen@fmi.fi> - 18.2.22-1.fmi
- Disabled printing of stack traces for some frequent user input errors

* Fri Feb  9 2018 Mika Heiskanen <mika.heiskanen@fmi.fi> - 18.2.9-1.fmi
- Repackaged due to TimeZones API change

* Mon Feb  5 2018 Mika Heiskanen <mika.heiskanen@fmi.fi> - 18.2.5-1.fmi
- Changed default download directory to be /var/tmp instead of /tmp

* Thu Sep 14 2017 Mika Heiskanen <mika.heiskanen@fmi.fi> - 17.9.14-2.fmi
- Fixed to handle MetCoop data which is defined using generic WKT

* Thu Sep 14 2017 Mika Heiskanen <mika.heiskanen@fmi.fi> - 17.9.14-1.fmi
- Switched from grib_api to eccodes

* Mon Aug 28 2017 Mika Heiskanen <mika.heiskanen@fmi.fi> - 17.8.28-1.fmi
- Upgrade to boost 1.65

* Thu Jul 13 2017 Mika Heiskanen <mika.heiskanen@fmi.fi> - 17.7.13-1.fmi
- Added possibility to configure enabled/disabled GRIB packing types

* Mon Apr 10 2017 Mika Heiskanen <mika.heiskanen@fmi.fi> - 17.4.10-1.fmi
- gribconfig and netcdfconfig paths can now be relative

* Sat Apr  8 2017 Mika Heiskanen <mika.heiskanen@fmi.fi> - 17.4.8-1.fmi
- Simplified error reporting

* Wed Mar 15 2017 Mika Heiskanen <mika.heiskanen@fmi.fi> - 17.3.15-1.fmi
- Recompiled since Spine::Exception changed

* Tue Mar 14 2017 Mika Heiskanen <mika.heiskanen@fmi.fi> - 17.3.14-1.fmi
- Switched to use macgyver StringConversion tools 

* Sat Feb 11 2017 Mika Heiskanen <mika.heiskanen@fmi.fi> - 17.2.11-1.fmi
- Repackaged due to newbase API change

* Fri Jan 27 2017 Mika Heiskanen <mika.heiskanen@fmi.fi> - 17.1.27-1.fmi
- Recompiled due to NFmiQueryData object size change

* Wed Jan  4 2017 Mika Heiskanen <mika.heiskanen@fmi.fi> - 17.1.4-1.fmi
- Changed to use renamed SmartMet base libraries

* Wed Nov 30 2016 Mika Heiskanen <mika.heiskanen@fmi.fi> - 16.11.30-1.fmi
- Using test database in test configuration
- No installation for configuration

* Tue Nov 15 2016 Mika Heiskanen <mika.heiskanen@fmi.fi> - 16.11.15-1.fmi
- New Copernicus grib parameters for marine forecasts

* Tue Nov  1 2016 Mika Heiskanen <mika.heiskanen@fmi.fi> - 16.11.1-1.fmi
- Namespace and directory name changed

* Tue Sep  6 2016 Mika Heiskanen <mika.heiskanen@fmi.fi> - 16.9.6-1.fmi
- New exception handler

* Tue Aug 30 2016 Mika Heiskanen <mika.heiskanen@fmi.fi> - 16.8.30-1.fmi
- Base class API change

* Mon Aug 15 2016 Markku Koskela <markku.koskela@fmi.fi> - 16.8.15-1.fmi
- The init(),shutdown() and requestHandler() methods are now protected methods
- The requestHandler() method is called from the callRequestHandler() method

* Wed Jun 29 2016 Mika Heiskanen <mika.heiskanen@fmi.fi> - 16.6.29-1.fmi
- QEngine API changed

* Tue Jun 14 2016 Mika Heiskanen <mika.heiskanen@fmi.fi> - 16.6.14-1.fmi
- Full recompile

* Thu Jun  2 2016 Mika Heiskanen <mika.heiskanen@fmi.fi> - 16.6.2-1.fmi
- Full recompile

* Wed Jun  1 2016 Mika Heiskanen <mika.heiskanen@fmi.fi> - 16.6.1-1.fmi
- Added graceful shutdown

* Mon May 16 2016 Mika Heiskanen <mika.heiskanen@fmi.fi> - 16.5.16-1.fmi
- Use TimeZones instead of TimeZonefactory

* Wed May  4 2016 Tuomo Lauri <tuomo.lauri@fmi.fi> - 16.5.4-1.fmi
- Fixed issue in landscaping functionality
- Switched to use the latest TimeSeriesGenerator API which handles climatological data too

* Tue Apr 19 2016 Tuomo Lauri <tuomo.lauri@fmi.fi> - 16.4.19-1.fmi
- Fixed segfault issue when requesting cropped data beyond data limits

* Mon Apr 18 2016 Mika Heiskanen <mika.heiskanen@fmi.fi> - 16.4.18-2.fmi
- Fixed to use recommended jsoncpp methods, isConvertibleTo behaviour has changed

* Mon Apr 18 2016 Tuomo Lauri <tuomo.lauri@fmi.fi> - 16.4.18-1.fmi
- Fixed area cropping bug

* Thu Mar 10 2016 Tuomo Lauri <tuomo.lauri@fmi.fi> - 16.3.10-1.fmi
- Added Ice Speed and Ice Direction grib-mappings

* Wed Mar  9 2016 Tuomo Lauri <tuomo.lauri@fmi.fi> - 16.3.9-1.fmi
- Added snowdepth parameter mapping

* Fri Mar  4 2016 Tuomo Lauri <tuomo.lauri@fmi.fi> - 16.3.4-2.fmi
- Added landscaping, but disabled it due to performance issues

* Fri Feb 26 2016 Tuomo Lauri <tuomo.lauri@fmi.fi> - 16.2.26-2.fmi
- Fixed grid id for sea water salinity.

* Fri Feb 26 2016 Tuomo Lauri <tuomo.lauri@fmi.fi> - 16.2.26-1.fmi
- Fix broken grib configurations of radiation parameters.
- No longer setting productDefinitionTemplateNumber to grib1 files


* Tue Feb  2 2016 Tuomo Lauri <tuomo.lauri@fmi.fi> - 16.2.2-1.fmi
- Sum positive instead of substract negative offset values.
- Renamed multiplier configuration parameter.

* Thu Jan 28 2016 Pertti Kinnia <pertti.kinnia@fmi.fi> - 16.1.28-1.fmi
- Silently ignore duplicate data parameter names

* Mon Jan 25 2016 Pertti Kinnia <pertti.kinnia@fmi.fi> - 16.1.25-2.fmi
- Added netcdf mapping for WeatherSymbol3

* Mon Jan 25 2016 Mika Heiskanen <mika.heiskanen@fmi.fi> - 16.1.25-1.fmi
- New Harmonie precpitation parameters

* Sat Jan 23 2016 Mika Heiskanen <mika.heiskanen@fmi.fi> - 16.1.23-1.fmi
- Fmi::TimeZoneFactory API changed

* Mon Jan 18 2016 Mika Heiskanen <mika.heiskanen@fmi.fi> - 16.1.18-1.fmi
- newbase API changed, full recompile

* Tue Jan  5 2016 Santeri Oksman <santeri.oksman@fmi.fi> - 16.1.5-1.fmi
- MeanIceThickness values of HELMI model are rescaled

* Thu Dec  3 2015 Tuomo Lauri <tuomo.lauri@fmi.fi> - 15.12.3-1.fmi
- Fixed bug related to unavailable parameters

* Wed Nov 18 2015 Tuomo Lauri <tuomo.lauri@fmi.fi> - 15.11.18-1.fmi
- SmartMetPlugin now receives a const HTTP Request

* Wed Nov 11 2015 Tuomo Lauri <tuomo.lauri@fmi.fi> - 15.11.12-1.fmi
- Added a number of TestLab parameters

* Mon Nov  9 2015 Mika Heiskanen <mika.heiskanen@fmi.fi> - 15.11.9-1.fmi
- Using fast case conversion without locale locks when possible

* Tue Nov  3 2015 Mika Heiskanen <mika.heiskanen@fmi.fi> - 15.11.3-1.fmi
- Removed calls to deprecated Cast.h string conversion functions

* Wed Oct 28 2015 Mika Heiskanen <mika.heiskanen@fmi.fi> - 15.10.28-1.fmi
- Fixed Visibility and CloudBase parameters

* Mon Oct 26 2015 Mika Heiskanen <mika.heiskanen@fmi.fi> - 15.10.26-1.fmi
- Added proper debuginfo packaging

* Wed Oct 21 2015 Tuomo Lauri <tuomo.lauri@fmi.fi> - 15.10.21-1.fmi
- Added visibility grib mapping

* Tue Oct 20 2015 Tuomo Lauri <tuomo.lauri@fmi.fi> - 15.10.20-1.fmi
- New 'tablesversion' query parameter

* Mon Oct 12 2015 Tuomo Lauri <tuomo.lauri@fmi.fi> - 15.10.12-1.fmi
- Moved from cropping to sampling in data generation
- Default HTTP status code for errors is not 400, Bad Request

* Mon Oct  5 2015 Tuomo Lauri <tuomo.lauri@fmi.fi> - 15.10.5-1.fmi
- Reverted master table changes due to problems with YLE production

* Tue Sep 29 2015 Tuomo Lauri <tuomo.lauri@fmi.fi> - 15.9.29-2.fmi
- Added pollen parameters

* Tue Sep 29 2015 Tuomo Lauri <tuomo.lauri@fmi.fi> - 15.9.29-1.fmi
- Added cloudbase-parameter

* Thu Aug 27 2015 Mika Heiskanen <mika.heiskanen@fmi.fi> - 15.8.27-1.fmi
- TimeSeriesGenerator API changed

* Wed Aug 26 2015 Mika Heiskanen <mika.heiskanen@fmi.fi> - 15.8.26-1.fmi
- Recompiled with latest newbase with faster parameter changing

* Mon Aug 24 2015 Mika Heiskanen <mika.heiskanen@fmi.fi> - 15.8.24-1.fmi
- Recompiled due to Convenience.h API changes

* Tue Aug 18 2015 Mika Heiskanen <mika.heiskanen@fmi.fi> - 15.8.18-1.fmi
- Use time formatters from macgyver to avoid global locks from sstreams

* Mon Aug 17 2015 Mika Heiskanen <mika.heiskanen@fmi.fi> - 15.8.17-1.fmi
- Use -fno-omit-frame-pointer to improve perf use

* Fri Aug 14 2015 Mika Heiskanen <mika.heiskanen@fmi.fi> - 15.8.14-1.fmi
- Recompiled due to string formatting changes

* Fri Jun 26 2015 Mika Heiskanen <mika.heiskanen@fmi.fi> - 15.6.26-1.fmi
- Recompiled due to API changes

* Tue Jun 23 2015 Mika Heiskanen <mika.heiskanen@fmi.fi> - 15.6.23-1.fmi
- Location API changed

* Mon May 25 2015 Tuomo Lauri <tuomo.lauri@fmi.fi> - 15.5.25-1.fmi
- Rebuilt against new spine

* Wed Apr 29 2015 Mika Heiskanen <mika.heiskanen@fmi.fi> - 15.4.29-1.fmi
- Added 24h min-max temperature mappings to grib conf
- Removed optionengine dependency

* Thu Apr 23 2015 Santeri Oksman <santeri.oksman@fmi.fi> - 15.4.23-1.fmi
- Added Icing parameter mapping <Tuomo Lauri>
- Added conversion for WeatherSymbol3 <Tuomo Lauri>

* Thu Apr 16 2015 Tuomo Lauri <tuomo.lauri@fmi.fi> - 15.4.16-1.fmi
- Fixed param '500032' in configuration

* Wed Apr 15 2015 Mika Heiskanen <mika.heiskanen@fmi.fi> - 15.4.15-1.fmi
- newbase NFmiQueryData::LatLonCache API changed

* Thu Apr  9 2015 Mika Heiskanen <mika.heiskanen@fmi.fi> - 15.4.9-1.fmi
- newbase API changed

* Wed Apr  8 2015 Mika Heiskanen <mika.heiskanen@fmi.fi> - 15.4.8-1.fmi
- Dynamic linking of smartmet libraries into use

* Tue Feb 24 2015 Mika Heiskanen <mika.heiskanen@fmi.fi> - 15.2.24-1.fmi
- Recompiled due to changes in newbase linkage

* Fri Jan 16 2015 Mika Heiskanen <mika.heiskanen@fmi.fi> - 15.1.16-1.fmi
- Generate actual origintime to the filename instead of querystring option

* Wed Jan 14 2015 Mika Heiskanen <mika.heiskanen@fmi.fi> - 15.1.14-1.fmi
- Added support for origintime=latest|newest|oldest

* Thu Dec 18 2014 Mika Heiskanen <mika.heiskanen@fmi.fi> - 14.12.18-1.fmi
- Recompiled due to spine API changes

* Fri Dec 12 2014 Tuomo Lauri <tuomo.lauri@fmi.fi> - 14.12.12-1.fmi
- Added PrecipitationForm2 and PrecipitationForm3 GRIB mappings

* Wed Dec  3 2014 Mika Heiskanen <mika.heiskanen@fmi.fi> - 14.12.3-3.fmi
- Fixed pressure multipliers to be 0.01

* Wed Dec  3 2014 Mika Heiskanen <mika.heiskanen@fmi.fi> - 14.12.3-2.fmi
- Renamed the configuration files to have a json suffix

* Wed Dec  3 2014 Mika Heiskanen <mika.heiskanen@fmi.fi> - 14.12.3-1.fmi
- Do not set template number 0, it is now allowed
- Do not use 0 as a multiplier for pressure

* Tue Dec  2 2014 Pertti Kinnia <pertti.kinnia@fmi.fi> - 14.12.2-1.fmi
- BRAINSTORM-394; New grib parameter configuration field "templatenumber"
- BRAINSTORM-400; Using json -formatted parameter configuration

* Mon Dec  1 2014 Tuomo Lauri <tuomo.lauri@fmi.fi> - 14.12.1-1.fmi
- Fixed non-native timestep handling

* Tue Nov 25 2014 Tuomo Lauri <tuomo.lauri@fmi.fi> - 14.11.24-1.fmi
- Now using (mostly) standard time parsing with OptionEngine

* Thu Nov 13 2014 Mika Heiskanen <mika.heiskanen@fmi.fi> - 14.11.13-1.fmi
- Added HourlyMaximumGust conversion

* Tue Nov 11 2014 Mika Heiskanen <mika.heiskanen@fmi.fi> - 14.11.11-1.fmi
- Added PrecipitationConv and PrecipitationLarge conversions

* Wed Sep 17 2014 Tuomo Lauri <tuomo.lauri@fmi.fi> - 14.9.17-1.fmi
- Fixed GRIB-parameter mapping confusion

* Fri Sep  5 2014 Tuomo Lauri <tuomo.lauri@fmi.fi> - 14.9.8-1.fmi
- Packaged Pertti's GRIB-table changes

* Tue Aug 26 2014 Mikko Visa <mikko.visa@fmi.fi> - 14.8.26-1.fmi
- INSPIRE-556: bounding box fix
- added test framework script for netcdf metadata testing and first netcdf test to catch INSPIRE-565
- workaround for NEWBASE-8; using nextTime() status instead of calling isTimeUsable()
- INSPIRE-565: Empty frame in NetCDF file

* Mon Jun 16 2014 Santeri Oksman <santeri.oksman@fmi.fi> - 14.6.16-1.fmi
- Added FogIntensity grib configuration

* Tue Jun 10 2014 Santeri Oksman <santeri.oksman@fmi.fi> - 14.6.10-1.fmi
- Added grib conversions for new cloudiness and precipitation parameters

* Wed May 14 2014 Mika Heiskanen <mika.heiskanen@fmi.fi> - 14.5.14-1.fmi
- Use shared macgyver and locus libraries

* Tue May 13 2014 Tuomo Lauri <tuomo.lauri@fmi.fi> - 14.5.13-1.fmi
- Added vertical wind speed parameter to grib.conf

* Thu May  8 2014 Mika Heiskanen <mika.heiskanen@fmi.fi> - 14.5.8-1.fmi
- Updated grib_api to version 1.12

* Tue May  6 2014 Mika Heiskanen <mika.heiskanen@fmi.fi> - 14.5.7-1.fmi
- Hotfix to timeparser

* Tue May  6 2014 Mika Heiskanen <mika.heiskanen@fmi.fi> - 14.5.6-1.fmi
- qengine API changed

* Mon Apr 28 2014 Mika Heiskanen <mika.heiskanen@fmi.fi> - 14.4.28-1.fmi
- Full recompile due to large changes in spine etc APIs

* Wed Apr  9 2014 Tuomo Lauri <tuomo.lauri@fmi.fi> - 14.4.9-1.fmi
- Grib.conf parameter additions

* Tue Apr  1 2014 Tuomo Lauri <tuomo.lauri@fmi.fi> - 14.4.1-1.fmi
- Added parameters PrecipitationRate and PrecipitationRateSolid to grib.conf

* Thu Mar 20 2014 Mikko Visa <mikko.visa@fmi.fi> - 14.3.20-1.fmi
- Open Data 2014-04-01 release
- Harmonie latlon grid distortion, INSPIRE-479

* Thu Mar 13 2014 Tuomo Lauri <tuomo.lauri@fmi.fi> - 14.3.13-1.fmi
- Added geometric height parameter to grib conf

* Wed Mar 12 2014 Tuomo Lauri <tuomo.lauri@fmi.fi> - 14.3.12-1.fmi
- Interim fix for rotated lat lon area problems

* Thu Feb 27 2014 Tuomo Lauri <tuomo.lauri@fmi.fi> - 14.2.27-1.fmi
- Wind component projection issues fixed

* Mon Feb 17 2014 Tuomo Lauri <tuomo.lauri@fmi.fi> - 14.2.17-1.fmi
- New release
- edited data cut half in stereographic projection, BRAINSTORM-323

* Mon Feb 3 2014 Mikko Visa <mikko.visa@fmi.fi> - 14.2.3-2.fmi
- Producers list commented out in open data configuration.
- Configured standard_name and long_name atributes into netcdf configuration.
- NetCDF standard_name and long_name atribute implementation.
- Fixed NetCDF time bounds of cell method.
- Starttime and endtime is not anymore ignored when origintime is defined when netcdf format is selected.
- Included netcdf.conf into rpm
- Added NedCDF CF name for the parameter 434.
- Using VelocityPotential parameter insted of VerticalVelocityMMS.
- Fixed parameters in Netcdf and Grib configuration.
- Fixed RadiationGlobal and RadiationLW cfnames and then commented those out in netcdf configuration.
- Added missing cfnames for radiation parameters.
- Fixed scale factors of cloud parameters and changed cfname of vertical velocity parameter in netcdf configuration.
- Netcdf config file path unconmmented into opendata configuration file.

* Thu Dec 12 2013 Tuomo Lauri <tuomo.lauri@fmi.fi> - 13.12.12-1.fmi
- LevelType and levelValue added for the parameter of HELMI model in grib configuration.
- Plugin no longer waits for QEngine in the constructor (it blocked the entire server startup)
- Fixed scale factor of Ice thickness parameter in grib and netcdf config.
- Fixed scale factor of Ice thickness parameter in grib config.
- Fixed unsigned types to signed after newbase changes.

* Mon Nov 25 2013 Mika Heiskanen <mika.heiskanen@fmi.fi> - 13.11.25-1.fmi
- Updated Locus library with signed geoids

* Thu Nov 14 2013 Mika Heiskanen <mika.heiskanen@fmi.fi> - 13.11.14-1.fmi
- Update to NetCDF streaming

* Thu Nov  7 2013 Mika Heiskanen <mika.heiskanen@fmi.fi> - 13.11.7-1.fmi
- Restore sub index status when switching between parameters

* Tue Nov  5 2013 Mika Heiskanen <mika.heiskanen@fmi.fi> - 13.11.5-1.fmi
- Added PseudoAdiabaticPotentialTemperature parameter
- Changed grib id of Vertical velocity and fixed the scale

* Wed Oct  9 2013 Tuomo Lauri <tuomo.lauri@fmi.fi> - 13.10.9-1.fmi
- Now conforming with the new Reactor initialization API
- Accepting both 'model' and 'producer' as model identifiers
- Setting stream status for querydata output and on error returns too
- Using NFmiMultiQueryInfo instead of ModelTimeList

* Tue Sep 24 2013 Tuomo Lauri <tuomo.lauri@fmi.fi> - 13.9.24-1.fmi
- Hotfix to properly set the stream status

* Mon Sep 23 2013 Tuomo Lauri    <tuomo.lauri@fmi.fi>    - 13.9.23-1.fmi
- Bugfixes

* Wed Sep 18 2013 Tuomo Lauri    <tuomo.lauri@fmi.fi>    - 13.9.18-1.fmi
- Recompiled to conform with the new Spine

* Fri Sep 6  2013 Tuomo Lauri    <tuomo.lauri@fmi.fi>    - 13.9.6-1.fmi
- Recompiled due Spine changes

* Wed Sep  4 2013 Tuomo Lauri    <tuomo.lauri@fmi.fi>    - 13.9.4-1.fmi
- Timestep fix

* Wed Aug 28 2013 Tuomo Lauri    <tuomo.lauri@fmi.fi>    - 13.8.28-1.fmi
- Partial netcdf-support

* Thu Aug 22 2013 Tuomo Lauri    <tuomo.lauri@fmi.fi>    - 13.8.22-1.fmi
- Updated grib_api dependency
- Added separate open data configuration

* Tue Jul 23 2013 Mika Heiskanen <mika.heiskanen@fmi.fi> - 13.7.23-1.fmi
- Recompiled due to thread safety fixes in newbase & macgyver

* Wed Jul  3 2013 mheiskan <mika.heiskanen@fmi.fi> - 13.7.3-1.fmi
- Update to boost 1.54

* Thu Jun 20 2013 lauri   <tuomo.lauri@fmi.fi>    -  13.6.20-2.fmi
- Fixed wind direction param id

* Thu Jun 20 2013 lauri   <tuomo.lauri@fmi.fi>    -  13.6.20-1.fmi
- Added parameters to grib.conf

* Mon Jun 17 2013 lauri   <tuomo.lauri@fmi.fi>    -  13.6.17-1.fmi
- grib scanning directions determined from grid's origo instead of dx/dy sign

* Mon Jun  3 2013 lauri    <tuomo.lauri@fmi.fi>    - 13.6.3-1.fmi
- Built against the new Spine

* Wed  May 29 2013 tervo    <roope.tervo@fmi.fi>   - 13.5.29-1.fmi
- Fixed bounding box logic
- Added new parameters to grib configuration

* Wed  May 22 2013 tervo    <roope.tervo@fmi.fi>   - 13.5.22-1.fmi
- New rpm

* Fri May 17 2013 kinnia    <pertti.kinnia@fmi.fi>   - 13.5.17-1.fmi
- Fixed wind u/v rotation in cachedProjGridValues()

* Sat May 11 2013 tervo    <roope.tervo@fmi.fi>   - 13.5.11-1.fmi
- Fixed winds

* Tue May 07 2013 oksman <santeri.oksman@fmi.fi> - 13.5.7-1.fmi
- Rebuild to get master and develop to the same stage.

* Thu May 02 2013 tervo    <roope.tervo@fmi.fi>   - 13.5.2-1.fmi
- Fixed wave parameters

* Tue Apr 30 2013 tervo    <roope.tervo@fmi.fi>   - 13.4.30-1.fmi
- Fixed handling of long time steps

* Mon Apr 29 2013 tervo    <roope.tervo@fmi.fi>   - 13.4.29-1.fmi
- Changed hirlam and monthly data default model name. Added TemperatureMonthlyMean and MonthlyPrecipitationRate in grib config.

* Wed Apr 24 2013 laurí    <tuomo.lauri@fmi.fi>   - 13.4.24-1.fmi
- Changed module name from 'DLS' to 'DLSplugin' to conform with the configuration name

* Mon Apr 22 2013 mheiskan <mika.heiskanen@fi.fi> - 13.4.22-1.fmi
- Brainstorm API changed

* Mon Apr 15 2013 lauri <tuomo.lauri@fmi.fi>    - 13.4.15-1.fmi
- Improved projection transformation performance

* Fri Apr 12 2013 lauri <tuomo.lauri@fmi.fi>    - 13.4.12-1.fmi
- Rebuild due to changes in Spine

* Tue Apr 9 2013 oksman <santeri.oksman@fmi.fi> - 13.4.9-1.fmi
- New beta release.

* Wed Mar 27 2013 lauri <tuomo.lauri@fmi.fi>	 - 13.3.27-1.fmi
- Built new version for open data.

* Thu Mar 14 2013 oksman <santeri.oksman@fmi.fi> - 13.3.14-1.fmi
- No data restrictions in plugin configuration file. Changed configuration file location to like in other plugins.
- New build from develop branch.

* Thu Feb 28 2013 lauri    <tuomo.lauri@fmi.fi>    - 13.2.28-1.fmi
- Bugfixes

* Wed Feb 27 2013 oksman   <santeri.oksman@fmi.fi> - 13.2.27.1-fmi
- Fixed grib conf path
- Fixes in projection and time parameter handling

* Fri Feb 22 2013 lauri    <tuomo.lauri@fmi.fi>    - 13.2.22.1-fmi
- Added origintime-parameter and fixed rotated lat-lon handling

* Thu Feb 14 2013 lauri    <tuomo.lauri@fmi.fi>    - 13.2.14-1.fmi

* Uses now streaming response

* Wed Feb  6 2013 lauri    <tuomo.lauri@fmi.fi>    - 13.2.6-1.fmi
- Initial release

%define DIRNAME download
%define SPECNAME smartmet-plugin-%{DIRNAME}
Summary: SmartMet Download Plugin
Name: %{SPECNAME}
Version: 18.2.9
Release: 1%{?dist}.fmi
License: MIT
Group: SmartMet/Plugins
URL: https://github.com/fmidev/smartmet-plugin-download
Source0: %{name}.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)
BuildRequires: gcc-c++
BuildRequires: make
BuildRequires: boost-devel
BuildRequires: gdal-devel >= 1.11.4
BuildRequires: eccodes-devel
BuildRequires: jsoncpp-devel >= 0.10.5
BuildRequires: libconfig >= 1.4.9
BuildRequires: smartmet-library-spine-devel >= 18.2.9
BuildRequires: smartmet-library-macgyver-devel >= 18.2.9
BuildRequires: smartmet-library-newbase-devel >= 18.2.8
BuildRequires: netcdf-devel
BuildRequires: smartmet-engine-querydata-devel >= 18.2.9
BuildRequires: smartmet-engine-geonames-devel
BuildRequires: netcdf-cxx-devel
BuildRequires: bzip2-devel
Requires: gdal >= 1.11.4
Requires: eccodes
Requires: jsoncpp >= 0.10.5
Requires: smartmet-library-macgyver >= 18.2.9
Requires: smartmet-library-spine >= 18.2.9
Requires: smartmet-library-newbase >= 18.2.8
Requires: smartmet-engine-querydata >= 18.2.9
Requires: smartmet-server >= 17.11.10
%if 0%{rhel} >= 7
Requires: boost-date-time
Requires: boost-iostreams
Requires: boost-system
Requires: boost-thread
BuildRequires: netcdf-cxx-devel
Requires: netcdf-cxx
%endif
Provides: %{SPECNAME}
Obsoletes: smartmet-brainstorm-dlsplugin < 16.11.1
Obsoletes: smartmet-brainstorm-dlsplugin-debuginfo < 16.11.1

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
%{_datadir}/smartmet/plugins/%{DIRNAME}.so

%changelog
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

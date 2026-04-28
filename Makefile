SUBNAME = download
SPEC = smartmet-plugin-$(SUBNAME)
INCDIR = smartmet/plugins/$(SUBNAME)

REQUIRES = gdal jsoncpp configpp

include $(shell echo $${PREFIX-/usr})/share/smartmet/devel/makefile.inc

ECCODES_LIBS = -leccodes
ifneq ($(words $(foreach ext, so a, $(wildcard $(libdir)/libeccodes_memfs.$(ext)))),0)
	ECCODES_LIBS += -leccodes_memfs
endif

FLAGS += -Wno-vla -Wno-variadic-macros -Wno-deprecated-declarations -Wno-unknown-pragmas

# Compiler options

DEFINES = -DUNIX -D_REENTRANT

CORBA_INCLUDE = -isystem /usr/include/smartmet/grid-content/contentServer/corba/stubs \
                -isystem /usr/include/smartmet/grid-content/dataServer/corba/stubs \
                -isystem /usr/include/smartmet/grid-content/queryServer/corba/stubs
CORBA_LIBS = -lomniORB4 -lomnithread

INCLUDES += $(CORBA_INCLUDE)

LIBS += $(PREFIX_LDFLAGS) \
	-lsmartmet-grid-content \
	-lsmartmet-grid-files \
	-lsmartmet-timeseries \
	-lsmartmet-spine \
	-lsmartmet-newbase \
	-lsmartmet-macgyver \
	 $(REQUIRED_LIBS) \
	 $(CORBA_LIBS) \
	-lboost_thread \
	-lboost_iostreams \
	-lbz2 -lz \
	$(ECCODES_LIBS) \
	-ljasper \
	-lnetcdf_c++4

# What to install

LIBFILE = $(SUBNAME).so

# Compilation directories

vpath %.cpp $(SUBNAME)
vpath %.h $(SUBNAME)

# The files to be compiled

SRCS = $(wildcard $(SUBNAME)/*.cpp)
HDRS = $(wildcard $(SUBNAME)/*.h)
OBJS = $(patsubst %.cpp, obj/%.o, $(notdir $(SRCS)))

DOWNLOAD_SRCS = $(wildcard $(SUBNAME)/download/*.cpp)
DOWNLOAD_OBJS = $(patsubst $(SUBNAME)/download/%.cpp, obj/download/%.o, $(DOWNLOAD_SRCS))

COVERAGES_SRCS = $(wildcard $(SUBNAME)/coverages/*.cpp)
COVERAGES_OBJS = $(patsubst $(SUBNAME)/coverages/%.cpp, obj/coverages/%.o, $(COVERAGES_SRCS))

OBJS += $(DOWNLOAD_OBJS) $(COVERAGES_OBJS)

INCLUDES := -I$(SUBNAME) $(INCLUDES)

.PHONY: test test-qd test-coverages test-grid rpm

# The rules

all: objdir $(LIBFILE)
debug: all
release: all
profile: all

$(LIBFILE): $(OBJS)
	$(CXX) $(LDFLAGS) -shared -rdynamic -o $(LIBFILE) $(OBJS) $(LIBS)
#@echo Checking $(LIBFILE) for unresolved references
#@if ldd -r $(LIBFILE) 2>&1 | c++filt | grep ^undefined\ symbol | grep -v SmartMet::Engine:: ; \
#	then rm -v $(LIBFILE); \
#	exit 1; \
#fi

clean:
	rm -f $(LIBFILE) *~ $(SUBNAME)/*~
	rm -rf obj
	$(MAKE) -C test $@

format:
	clang-format -i -style=file $(SUBNAME)/*.h $(SUBNAME)/*.cpp $(SUBNAME)/download/*.h $(SUBNAME)/download/*.cpp $(SUBNAME)/coverages/*.h $(SUBNAME)/coverages/*.cpp

install:
	@mkdir -p $(plugindir)
	$(INSTALL_PROG) $(LIBFILE) $(plugindir)/$(LIBFILE)

test test-qd test-coverages test-grid:
	$(MAKE) -C test $@

objdir:
	@mkdir -p $(objdir) $(objdir)/download $(objdir)/coverages

# Forcibly lower RPM_BUILD_NCPUs in CircleCI cloud(but not on local builds)
RPMBUILD=$(shell test "$$CIRCLE_BUILD_NUM" && echo RPM_BUILD_NCPUS=2 rpmbuild || echo rpmbuild)

rpm: clean $(SPEC).spec
	rm -f $(SPEC).tar.gz # Clean a possible leftover from previous attempt
	tar -czvf $(SPEC).tar.gz --exclude test --exclude-vcs --transform "s,^,$(SPEC)/," *
	$(RPMBUILD) -tb $(SPEC).tar.gz
	rm -f $(SPEC).tar.gz

.SUFFIXES: $(SUFFIXES) .cpp

obj/%.o: %.cpp
	$(CXX) $(CFLAGS) $(INCLUDES) -c -MD -MF $(patsubst obj/%.o, obj/%.d, $@) -MT $@ -o $@ $<

obj/download/%.o: $(SUBNAME)/download/%.cpp
	$(CXX) $(CFLAGS) $(INCLUDES) -c -MD -MF obj/download/$*.d -MT $@ -o $@ $<

obj/coverages/%.o: $(SUBNAME)/coverages/%.cpp
	$(CXX) $(CFLAGS) $(INCLUDES) -c -MD -MF obj/coverages/$*.d -MT $@ -o $@ $<

-include $(wildcard obj/*.d)
-include $(wildcard obj/download/*.d)
-include $(wildcard obj/coverages/*.d)

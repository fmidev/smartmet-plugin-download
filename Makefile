SUBNAME = download
SPEC = smartmet-plugin-$(SUBNAME)
INCDIR = smartmet/plugins/$(SUBNAME)

# Installation directories

processor := $(shell uname -p)

ifeq ($(origin PREFIX), undefined)
  PREFIX = /usr
else
  PREFIX = $(PREFIX)
endif

ifeq ($(processor), x86_64)
  libdir = $(PREFIX)/lib64
else
  libdir = $(PREFIX)/lib
endif

bindir = $(PREFIX)/bin
includedir = $(PREFIX)/include
datadir = $(PREFIX)/share
plugindir = $(datadir)/smartmet/plugins
objdir = obj

# Compiler options

DEFINES = -DUNIX -D_REENTRANT

ifeq ($(CXX), clang++)

# TODO: Try to shorten the list of disabled checks
 FLAGS = \
	-std=c++11 -fPIC -MD \
	-Weverything \
	-Wno-c++98-compat \
	-Wno-float-equal \
	-Wno-padded \
	-Wno-missing-prototypes \
	-Wno-exit-time-destructors \
	-Wno-global-constructors \
	-Wno-shorten-64-to-32 \
	-Wno-sign-conversion \
	-Wno-vla -Wno-vla-extension

 INCLUDES = \
	-isystem $(includedir) \
	-isystem $(includedir)/smartmet \
	-isystem $(includedir)/mysql \
	-isystem $(includedir)/gdal

else

 FLAGS = -std=c++11 -fPIC -MD -Wall -W -Wno-unused-parameter -fno-omit-frame-pointer -Wno-unknown-pragmas -fdiagnostics-color=always

 FLAGS_DEBUG = \
	-Wcast-align \
	-Wcast-qual \
	-Winline \
	-Wno-multichar \
	-Wno-pmf-conversions \
	-Woverloaded-virtual  \
	-Wpointer-arith \
	-Wredundant-decls \
	-Wwrite-strings \
	-Wno-deprecated

 FLAGS_RELEASE = -Wuninitialized

  INCLUDES = \
	-I$(includedir) \
	-I$(includedir)/smartmet \
	-I$(includedir)/mysql \
	-I$(includedir)/gdal \
	-I$(includedir)/jsoncpp
endif

# Compile options in detault, debug and profile modes

CFLAGS_RELEASE = $(DEFINES) $(FLAGS) $(FLAGS_RELEASE) -DNDEBUG -O2 -g
CFLAGS_DEBUG   = $(DEFINES) $(FLAGS) $(FLAGS_DEBUG)   -Werror  -O0 -g

ifneq (,$(findstring debug,$(MAKECMDGOALS)))
  override CFLAGS += $(CFLAGS_DEBUG)
else
  override CFLAGS += $(CFLAGS_RELEASE)
endif

LIBS = -L$(libdir) \
	-lsmartmet-spine \
	-lsmartmet-newbase \
	-lsmartmet-macgyver \
	-lboost_date_time \
	-lboost_thread \
	-lboost_iostreams \
	-lboost_system \
	-lbz2 -lz \
	-lgrib_api \
	-lgdal \
	-lnetcdf_c++ \
	`pkg-config --libs jsoncpp`

# rpm variables

rpmsourcedir = /tmp/$(shell whoami)/rpmbuild

rpmerr = "There's no spec file ($(SPEC).spec). RPM wasn't created. Please make a spec file or copy and rename it into $(SPEC).spec"

# What to install

LIBFILE = $(SUBNAME).so

# How to install

INSTALL_PROG = install -p -m 775
INSTALL_DATA = install -p -m 664

# Compilation directories

vpath %.cpp source
vpath %.h include

# The files to be compiled

SRCS = $(wildcard source/*.cpp)
HDRS = $(wildcard include/*.h)
OBJS = $(patsubst %.cpp, obj/%.o, $(notdir $(SRCS)))

INCLUDES := -Iinclude $(INCLUDES)

.PHONY: test rpm

# The rules

all: configtest objdir $(LIBFILE)
debug: all
release: all
profile: all

configtest:
	@if [ -x "$$(command -v cfgvalidate)" ]; then cfgvalidate -v test/cnf/download.conf; fi

$(LIBFILE): $(OBJS)
	$(CXX) $(CFLAGS) -shared -rdynamic -o $(LIBFILE) $(OBJS) $(LIBS)

clean:
	rm -f $(LIBFILE) *~ source/*~ include/*~
	rm -rf obj

format:
	clang-format -i -style=file include/*.h source/*.cpp test/*.cpp

install:
	@mkdir -p $(plugindir)
	$(INSTALL_PROG) $(LIBFILE) $(plugindir)/$(LIBFILE)

test:
	cd test && make test

objdir:
	@mkdir -p $(objdir)

rpm: clean
	@if [ -e $(SPEC).spec ]; \
	then \
	  mkdir -p $(rpmsourcedir) ; \
	  tar -czvf $(rpmsourcedir)/$(SPEC).tar.gz --transform "s,^,plugins/$(SPEC)/," * ; \
	  rpmbuild -ta $(rpmsourcedir)/$(SPEC).tar.gz ; \
	  rm -f $(rpmsourcedir)/$(SPEC).tar.gz ; \
	else \
	  echo $(rpmerr); \
	fi;

.SUFFIXES: $(SUFFIXES) .cpp

obj/%.o: %.cpp
	$(CXX) $(CFLAGS) $(INCLUDES) -c -o $@ $<

ifneq ($(wildcard obj/*.d),)
-include $(wildcard obj/*.d)
endif

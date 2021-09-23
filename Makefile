SUBNAME = download
SPEC = smartmet-plugin-$(SUBNAME)
INCDIR = smartmet/plugins/$(SUBNAME)

REQUIRES = gdal jsoncpp configpp

include $(shell echo $${PREFIX-/usr})/share/smartmet/devel/makefile.inc

FLAGS += -Wno-vla -Wno-variadic-macros -Wno-deprecated-declarations -Wno-unknown-pragmas

# Compiler options

DEFINES = -DUNIX -D_REENTRANT

LIBS += -L$(libdir) \
	-lsmartmet-spine \
	-lsmartmet-newbase \
	-lsmartmet-macgyver \
	 $(REQUIRED_LIBS) \
	-lboost_date_time \
	-lboost_thread \
	-lboost_iostreams \
	-lboost_system \
	-lbz2 -lz \
	-leccodes -leccodes_memfs \
	-lnetcdf_c++

# What to install

LIBFILE = $(SUBNAME).so

# Compilation directories

vpath %.cpp $(SUBNAME)
vpath %.h $(SUBNAME)

# The files to be compiled

SRCS = $(wildcard $(SUBNAME)/*.cpp)
HDRS = $(wildcard $(SUBNAME)/*.h)
OBJS = $(patsubst %.cpp, obj/%.o, $(notdir $(SRCS)))

INCLUDES := -I$(SUBNAME) $(INCLUDES)

.PHONY: test rpm

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
	clang-format -i -style=file $(SUBNAME)/*.h $(SUBNAME)/*.cpp

install:
	@mkdir -p $(plugindir)
	$(INSTALL_PROG) $(LIBFILE) $(plugindir)/$(LIBFILE)

test:
	$(MAKE) -C test $@

objdir:
	@mkdir -p $(objdir)

# Forcibly lower RPM_BUILD_NCPUs in CircleCI cloud(but not on local builds)
RPMBUILD=$(shell test "$$CIRCLE_BUILD_NUM" && echo RPM_BUILD_NCPUS=2 rpmbuild || echo rpmbuild)

rpm: clean $(SPEC).spec
	rm -f $(SPEC).tar.gz # Clean a possible leftover from previous attempt
	tar -czvf $(SPEC).tar.gz --exclude test --exclude-vcs --transform "s,^,$(SPEC)/," *
	$(RPMBUILD) -tb $(SPEC).tar.gz
	rm -f $(SPEC).tar.gz

.SUFFIXES: $(SUFFIXES) .cpp

obj/%.o: %.cpp
	$(CXX) $(CFLAGS) $(INCLUDES) -c -o $@ $<

-include $(wildcard obj/*.d)


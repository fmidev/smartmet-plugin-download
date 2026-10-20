PROG = PluginTest

REQUIRES = gdal configpp

include $(shell echo $${PREFIX-/usr})/share/smartmet/devel/makefile.inc

CFLAGS = -DUNIX -O0 -g $(FLAGS) -Wno-unknown-pragmas

INCLUDES += \
	-I ../download

LIBS += $(REQUIRED_LIBS) \
	-lsmartmet-spine  \
	-lsmartmet-macgyver \
	-lsmartmet-newbase \
	-lboost_thread \
	-lboost_filesystem \
	-lboost_iostreams \
	-lconfig++ \
	-lbz2 -lz \
	-leccodes \
	-lpthread

OBS =	../obj/Config.o \
		../obj/DataStreamer.o \
		../obj/Datum.o \
		../obj/GribStreamer.o \
		../obj/GribTools.o \
		../obj/NetCdfStreamer.o \
		../obj/Plugin.o \
		../obj/QueryDataStreamer.o \
		../obj/Query.o

all: $(PROG)
clean:
	rm -f $(PROG) *~

TEST_DB_DIR := $(shell pwd)/tmp-geonames-db

# Starts the Redis test database listening only on a Unix socket below /tmp
GRID_TEST_REDIS ?= smartmet-grid-test-redis

TEST_PREPARE_TARGETS := cnf/geonames.conf start-redis-db
TEST_FINISH_TARGETS := .dummy stop-redis-db
ifdef CI
  GEONAMES_HOST_EDIT := sed -e 's|"smartmet-test"|"$(TEST_DB_DIR)"|g'
  TEST_PREPARE_TARGETS += start-geonames-db
  TEST_FINISH_TARGETS += stop-geonames-db
  TEST_TARGETS := test-qd test-coverages
else
  ifdef LOCAL_TESTS_ONLY
    TEST_TARGETS := test-qd test-coverages
    GEONAMES_HOST_EDIT := cat
    META_CONF_EDIT := cat
  else
    GEONAMES_HOST_EDIT := cat
    META_CONF_EDIT := cat
    TEST_TARGETS := test-qd test-coverages test-grid
  endif
endif

TESTER_PARAM := --handler=/download --reactor-config=cnf/reactor.conf --num-threads 8
TESTER_PARAM_COV := --handler=/coverages --reactor-config=cnf/reactor.conf --input-dir=input-coverages --num-threads 8

# Each test target stops the databases when it finishes, so run the targets
# in separate make invocations to get a fresh prepare/finish cycle for each
test:
	@ok=true; \
	for t in $(TEST_TARGETS); do $(MAKE) $$t || ok=false; done; \
	$$ok

test-qd: $(TEST_PREPARE_TARGETS)
	@rm -rf failures tmp
	@mkdir -p failures tmp
	@echo ""
	@echo "*******************************************************************"
	@echo "*** Testing legacy /download interface, querydata source        ***"
	@echo "*** (requests: test/input, failures: test/failures)             ***"
	@echo "*******************************************************************"
	@echo ""
	ok=true; $(TEST_RUNNER) smartmet-plugin-test $(TESTER_PARAM) || ok=false; $(MAKE) $(TEST_FINISH_TARGETS); $$ok

test-coverages: $(TEST_PREPARE_TARGETS)
	@rm -rf failures tmp
	@mkdir -p failures tmp
	@echo ""
	@echo "*******************************************************************"
	@echo "*** Testing OGC API Coverages interface (/coverages)            ***"
	@echo "*** (requests: test/input-coverages, failures: test/failures)   ***"
	@echo "*******************************************************************"
	@echo ""
	ok=true; $(TEST_RUNNER) smartmet-plugin-test $(TESTER_PARAM_COV) || ok=false; $(MAKE) $(TEST_FINISH_TARGETS); \
	$(MAKE) validate-openapi && $$ok

# The generated OpenAPI definition is validated against the OpenAPI 3.1 schema in schemas/,
# and every successful /coverages test request must be described by it: an operation matches
# the path, the values are in the enums and all the parameters are declared.
validate-openapi:
	@if command -v python3 >/dev/null; then \
	   python3 validate-openapi.py output/coverages_api.get:input-coverages::output; \
	 else \
	   echo "python3 not available, skipping OpenAPI validation"; \
	 fi

test-grid: $(TEST_PREPARE_TARGETS)
	ok=true
	if $(MAKE) -C grid test; then ok=true; else ok=false; fi; \
	$(MAKE) $(TEST_FINISH_TARGETS); \
	$$ok

cnf/geonames.conf: cnf/geonames.conf.in .dummy
	$(GEONAMES_HOST_EDIT) $< >$@

geonames-database:
	@-$(MAKE) stop-geonames-db
	rm -rf tmp-geonames-db
	if ! /usr/share/smartmet/test/db/create-local-db.sh $(TEST_DB_DIR) >tmp-geonames-db.log 2>&1 ; then \
	    cat tmp-geonames-db.log; \
	    false; \
	fi

start-geonames-db: geonames-database
	/usr/share/smartmet/test/db/test-db-ctl.sh $(TEST_DB_DIR) start -w

stop-geonames-db:
	-/usr/share/smartmet/test/db/test-db-ctl.sh $(TEST_DB_DIR) stop -w

start-redis-db:
	$(GRID_TEST_REDIS) start

stop-redis-db:
	$(GRID_TEST_REDIS) stop

.dummy:
	true

.PHONY: geonames.conf validate-openapi

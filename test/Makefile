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
	-lboost_system \
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

TEST_PREPARE_TARGETS := cnf/geonames.conf start-redis-db
TEST_FINISH_TARGETS := .dummy stop-redis-db
ifdef CI
  GEONAMES_HOST_EDIT := sed -e 's|"smartmet-test"|"$(TEST_DB_DIR)"|g'
  TEST_PREPARE_TARGETS += start-geonames-db
  TEST_FINISH_TARGETS += stop-geonames-db
  TEST_TARGETS := test-qd
else
  ifdef LOCAL_TESTS_ONLY
    TEST_TARGETS := test-qd
    GEONAMES_HOST_EDIT := cat
    META_CONF_EDIT := cat
  else
    GEONAMES_HOST_EDIT := cat
    META_CONF_EDIT := cat
    TEST_TARGETS := test-qd test-grid
  endif
endif

TESTER_PARAM := --handler=/download --reactor-config=cnf/reactor.conf

test: $(TEST_TARGETS)

test-qd: $(TEST_PREPARE_TARGETS)
	@rm -rf failures tmp
	@mkdir -p failures tmp
	ok=true; smartmet-plugin-test $(TESTER_PARAM) || ok=false; $(MAKE) $(TEST_FINISH_TARGETS); $$ok

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
	rm -rf cnf/grid
	cp -pr $(datadir)/smartmet/test/grid cnf/
	@echo "*** Creating the Redis configuration file."
	smartmet-grid-test-config-creator cnf/environment.conf cnf/grid/redis/redis-template.conf cnf/grid/redis/redis.conf
	@echo "*** Starting the Redis database."
	@redis-server cnf/grid/redis/redis.conf

stop-redis-db:
	@if [ -f cnf/grid/redis/redis-server.pid ] ; then \
		cat cnf/grid/redis/redis-server.pid | xargs kill -9; \
		rm -f cnf/grid/redis/redis-server.pid; \
	fi

.dummy:
	true

.PHONY: geonames.conf

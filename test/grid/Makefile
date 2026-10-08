include $(shell echo $${PREFIX-/usr})/share/smartmet/devel/makefile.inc

TEST_TIMEOUT ?= 300

# Additional grid test data from the smartmet-qdtools-test-data package; a latlon
# grid scanned from north to south, for testing that the data is not vertically
# flipped when fetching data using the original geometry

QDTOOLS_GRIB := $(datadir)/smartmet/test/data/qdtools/input/grib/LC-0TO1_ground_0_ll_3600_1801_0_000.grib

# A global thunderstorm probability grid scanned from north to south, for testing the
# fast native data fetching (direct copy) of a global grid. Reuses geometry 5204.

ECGMTA_GRIB := $(datadir)/smartmet/test/data/grib/ecgmta/ecgmta_pot_prcnt.grib
REDIS_PORT = $(shell awk -F= '/^REDIS_CONTENT_SERVER_PORT/{gsub(/[ \t]/,"",$$2); print $$2}' cnf/environment.conf)

all:

# Stop the Redis server afterwards, otherwise the next run would silently use the
# old server (and its old data) since the new one cannot bind to the same port

test test-grid:	start-redis-db
	if $(MAKE) test-impl; then ok=true; else ok=false; fi; $(MAKE) stop-redis-db; $$ok

clean:
	rm -rf failures
	rm -rf cnf/grid

test-impl:
	mkdir -p tmp
	@echo ""
	@echo "*******************************************************************"
	@echo "*** Testing legacy /download interface, grid-engine source      ***"
	@echo "*** (requests: test/grid/input, failures: test/grid/failures)   ***"
	@echo "*** NOTE: test names are the same as in the querydata and       ***"
	@echo "*** coverages suites; these are separate grid-source tests      ***"
	@echo "*******************************************************************"
	@echo ""
	$(TEST_RUNNER) smartmet-plugin-test \
		--handler /download \
		--reactor-config cnf/reactor.conf \
		--timeout $(TEST_TIMEOUT)

start-redis-db:
	rm -rf cnf/grid
	cp -pr $(datadir)/smartmet/test/grid cnf/
	@echo "*** Creating the Redis configuration file."
	smartmet-grid-test-config-creator cnf/environment.conf cnf/grid/redis/redis-template.conf cnf/grid/redis/redis.conf
	@echo "*** Starting the Redis database."
	@redis-server cnf/grid/redis/redis.conf
	$(MAKE) register-qdtools-data
	$(MAKE) register-ecgmta-data

# Newer smartmet-qdtools-test-data packages ship the grib xz compressed. The grid
# engine cannot read compressed files, so serve the test data from a local directory
# which links to the installed data and holds an uncompressed copy of the grib.

LOCAL_DATA_DIR := $(CURDIR)/tmp/data

register-qdtools-data:
	@test -s $(QDTOOLS_GRIB) || test -s $(QDTOOLS_GRIB).xz || { \
	    echo "$(QDTOOLS_GRIB) not found; install smartmet-qdtools-test-data"; false; }
	rm -rf $(LOCAL_DATA_DIR)
	mkdir -p $(LOCAL_DATA_DIR)/qdtools/input/grib
	for f in $(datadir)/smartmet/test/data/*; do \
	    test "$$(basename $$f)" = qdtools || ln -s $$f $(LOCAL_DATA_DIR)/; \
	done
	if test -s $(QDTOOLS_GRIB); then \
	    ln -s $(QDTOOLS_GRIB) $(LOCAL_DATA_DIR)/qdtools/input/grib/; \
	else \
	    xz -dc $(QDTOOLS_GRIB).xz > $(LOCAL_DATA_DIR)/qdtools/input/grib/$(notdir $(QDTOOLS_GRIB)); \
	fi
	sed -i 's|directory = "$(datadir)/smartmet/test/data"|directory = "$(LOCAL_DATA_DIR)"|' cnf/grid/engine/grid-engine.conf
	@echo "*** Registering qdtools test data into the Redis database."
	cat extra/fmi_geometries_qdtools.csv >> cnf/grid/library/test/fmi_geometries.csv
	echo "qdtools_ll:5204" >> cnf/grid/engine/producers_test.csv
	echo "qdtools_ll;LC-0TO1;2;LC-0TO1;5204;1;1;00000;1;1;1;0;E;;;;" >> cnf/grid/engine/mapping_fmi_test.csv
	@redis-cli -p $(REDIS_PORT) zadd test.producers 0 "12;qdtools_ll;qdtools_ll;;0;0" > /dev/null
	@redis-cli -p $(REDIS_PORT) zadd test.generations 0 "13;0;12;qdtools_ll:20190606T180000;;20190606T180000;1;0;0" > /dev/null
	@redis-cli -p $(REDIS_PORT) zadd test.files 0 "20;2;/qdtools/input/grib/LC-0TO1_ground_0_ll_3600_1801_0_000.grib;12;13;0;1;0;;" > /dev/null
	@redis-cli -p $(REDIS_PORT) zadd test.content 0 "20;0;2;0;6483708;12;13;0;20190606T180000;143;LC-0TO1;0;0;0;0;0;1;1;1;0;0to1;;1;-1;0;0;0;5204;;" > /dev/null
	@redis-cli -p $(REDIS_PORT) set test.producerCounter 12 > /dev/null
	@redis-cli -p $(REDIS_PORT) set test.generationCounter 13 > /dev/null
	@redis-cli -p $(REDIS_PORT) set test.fileCounter 20 > /dev/null

# Register the global thunderstorm probability grid (reuses geometry 5204).
register-ecgmta-data:
	@test -s $(ECGMTA_GRIB) || { \
	    echo "$(ECGMTA_GRIB) not found; update smartmet-test-data"; false; }
	@echo "*** Registering ecgmta test data into the Redis database."
	echo "ecgmta:5204" >> cnf/grid/engine/producers_test.csv
	echo "ecgmta;POT-PRCNT;2;POT-PRCNT;5204;1;1;00000;1;1;1;0;E;;;;" >> cnf/grid/engine/mapping_fmi_test.csv
	@redis-cli -p $(REDIS_PORT) zadd test.producers 0 "13;ecgmta;ecgmta;;0;0" > /dev/null
	@redis-cli -p $(REDIS_PORT) zadd test.generations 0 "14;0;13;ecgmta:20260615T120000;;20260615T120000;1;0;0" > /dev/null
	@redis-cli -p $(REDIS_PORT) zadd test.files 0 "21;2;/grib/ecgmta/ecgmta_pot_prcnt.grib;13;14;0;1;0;;" > /dev/null
	@redis-cli -p $(REDIS_PORT) zadd test.content 0 "21;0;2;0;5673333;13;14;0;20260616T000000;701;POT-PRCNT;0;0;0;0;0;1;1;1;0;%;;1;-1;0;0;0;5204;;" > /dev/null
	@redis-cli -p $(REDIS_PORT) set test.producerCounter 13 > /dev/null
	@redis-cli -p $(REDIS_PORT) set test.generationCounter 14 > /dev/null
	@redis-cli -p $(REDIS_PORT) set test.fileCounter 21 > /dev/null

stop-redis-db:
	@if [ -f cnf/grid/redis/redis-server.pid ] ; then \
		cat cnf/grid/redis/redis-server.pid | xargs kill -9; \
		rm -f cnf/grid/redis/redis-server.pid; \
	fi

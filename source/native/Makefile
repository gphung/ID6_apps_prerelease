CC ?= cc
PYTHON ?= python3
CFLAGS ?= -O2 -std=c99 -Wall -Wextra -Werror
CORE = src/gauge_core.c src/n55_metrics.c
CORE_LINK = $(CORE) src/diag_link.c
HEADERS = src/gauge_core.h src/n55_metrics.h src/drive_frame.h src/diagnostic_lease.h src/diag_link.h
# Every host worker carries the HSFZ-then-MOST link, with a fake DSI selected by ID6_FAKE_MOST.
MOST = src/most/most_link.c src/most/dsi_client.c src/most/most_proto.c src/most/dsi_bind.c
MOST_HEADERS = src/most/most_link.h src/most/dsi_client.h src/most/most_proto.h src/most/dsi_abi.h
FAKE_DSI = tests/fake_dsi.c tests/fake_dme.c tests/fake_dsi.h tests/fake_dme.h
WORKER = src/collector.c $(CORE_LINK) $(MOST) tests/fake_most_env.c $(FAKE_DSI) $(HEADERS) $(MOST_HEADERS)
HOST_LINK = $(CC) $(CFLAGS) -pthread -DMOST_HOST -Isrc -Itests
.PHONY: assets test

assets:
	$(PYTHON) tools/make_bars.py
	$(PYTHON) tools/make_drive_apps.py

build:
	mkdir -p build

build/boostdeck-collector: $(WORKER) | build
	$(HOST_LINK) $(filter %.c,$^) -lm -o $@

build/shift-deck: $(WORKER) | build
	$(HOST_LINK) -DSHIFT_DECK_ONLY $(filter %.c,$^) -lm -o $@

build/mhd-maps: $(WORKER) | build
	$(HOST_LINK) -DMHD_MAPS_ONLY $(filter %.c,$^) -lm -o $@

build/engine-codes: $(WORKER) | build
	$(HOST_LINK) -DENGINE_CODES_ONLY $(filter %.c,$^) -lm -o $@

build/bootmod3: $(WORKER) src/controls.c src/bm3_profiles.h | build
	$(HOST_LINK) -DBM3_ONLY $(filter-out src/controls.c,$(filter %.c,$^)) -lm -o $@

build/exhaust-flap: $(WORKER) | build
	$(HOST_LINK) -DEXHAUST_FLAP_ONLY $(filter %.c,$^) -lm -o $@

build/test-core: tests/test_core.c $(CORE) $(HEADERS) | build
	$(CC) $(CFLAGS) -Isrc $(filter %.c,$^) -lm -o $@

build/test-most: tests/test_most.c $(MOST) $(FAKE_DSI) $(MOST_HEADERS) | build
	$(CC) $(CFLAGS) -pthread -DMOST_HOST -Isrc -Itests $(filter %.c,$^) -o $@

build/test-diag-link: tests/test_diag_link.c $(CORE_LINK) $(MOST) tests/fake_most_env.c $(FAKE_DSI) $(HEADERS) $(MOST_HEADERS) | build
	$(HOST_LINK) $(filter %.c,$^) -lm -o $@

test: build/boostdeck-collector build/shift-deck build/mhd-maps build/engine-codes build/test-core build/test-most build/test-diag-link build/bootmod3 build/exhaust-flap build/test-dme-lookup build/test-diagnostic-gateway build/test-fault-text build/test-n55-metrics build/n55-dump
	./build/test-core
	./build/test-n55-metrics
	./build/test-dme-lookup
	./build/test-fault-text
	./build/test-diagnostic-gateway
	./build/test-most
	./build/test-diag-link
	$(PYTHON) -B tests/test_drive_apps.py
	$(PYTHON) -B tests/test_oneshot_most.py
	$(PYTHON) -B tests/test_transport.py
	$(PYTHON) -B tests/test_n55_tiles.py

build/test-dme-lookup: tests/test_dme_lookup.c src/dme_lookup.h src/dme_descriptions.h src/dme_descriptions_n55.h | build
	$(CC) $(CFLAGS) -Isrc tests/test_dme_lookup.c -o $@

build/test-diagnostic-gateway: tests/test_diagnostic_gateway.cpp src/diagnostic_gateway.h | build
	$(CXX) -O2 -Wall -Wextra -Werror -Isrc tests/test_diagnostic_gateway.cpp -o $@

build/test-fault-text: tests/test_fault_text.c src/fault_text.h | build
	$(CC) $(CFLAGS) -Isrc tests/test_fault_text.c -o $@

build/test-n55-metrics: tests/test_n55_metrics.c src/n55_metrics.c src/n55_metrics.h | build
	$(CC) $(CFLAGS) -Isrc $(filter %.c,$^) -lm -o $@

build/n55-dump: tests/n55_dump.c src/n55_metrics.c src/n55_metrics.h | build
	$(CC) $(CFLAGS) -Isrc $(filter %.c,$^) -lm -o $@

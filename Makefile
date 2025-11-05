# nlmon - Enhanced Network Link Monitor
# Build system with modular compilation and optional features

EXEC   := nlmon
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
LIBDIR ?= $(PREFIX)/lib/nlmon
CONFDIR ?= /etc/nlmon
DATADIR ?= /var/lib/nlmon

# Feature flags - set to 1 to enable, 0 to disable
ENABLE_CONFIG    ?= 1
ENABLE_STORAGE   ?= 1
ENABLE_WEB       ?= 1
ENABLE_PLUGINS   ?= 1
ENABLE_EXPORT    ?= 1

# libnl-tiny integration
LIBNL_DIR := libnl
LIBNL_LIB := $(LIBNL_DIR)/libnl-tiny.a
LIBNL_SRCS := $(LIBNL_DIR)/attr.c $(LIBNL_DIR)/cache.c $(LIBNL_DIR)/cache_mngt.c \
              $(LIBNL_DIR)/error.c $(LIBNL_DIR)/genl.c $(LIBNL_DIR)/genl_ctrl.c \
              $(LIBNL_DIR)/genl_family.c $(LIBNL_DIR)/genl_mngt.c $(LIBNL_DIR)/handlers.c \
              $(LIBNL_DIR)/msg.c $(LIBNL_DIR)/nl.c $(LIBNL_DIR)/object.c \
              $(LIBNL_DIR)/socket.c $(LIBNL_DIR)/unl.c
LIBNL_OBJS := $(LIBNL_SRCS:.c=.o)
LIBNL_INCLUDES := -I$(LIBNL_DIR)/include

# Core source files
CORE_SRCS := nlmon.c
CORE_OBJS := $(CORE_SRCS:.c=.o)

# Additional source files (will be populated as modules are implemented)
CONFIG_SRCS   := src/config/config.c src/config/yaml_parser.c src/config/hot_reload.c
CORE_ENGINE_SRCS := src/core/ring_buffer.c src/core/thread_pool.c src/core/rate_limiter.c src/core/event_processor.c
MEMORY_MGMT_SRCS := src/core/object_pool.c src/core/event_pool.c src/core/filter_pool.c src/core/resource_tracker.c src/core/signal_handler.c src/core/memory_tracker.c src/core/performance_profiler.c
NETLINK_SRCS := src/core/netlink_multi_protocol.c src/core/nlmon_netlink.c src/core/nlmon_nl_route.c src/core/nlmon_nl_genl.c src/core/nlmon_nl_diag.c src/core/nlmon_nl_netfilter.c src/core/nlmon_nl_event.c src/core/nlmon_nl_error.c src/core/nlmon_netlink_compat.c src/core/nlmon_netlink_api.c src/core/namespace_tracker.c src/core/interface_detector.c src/core/qca_vendor.c src/core/qca_wmi.c src/core/wmi_log_reader.c src/core/wmi_event_bridge.c src/core/wmi_error.c
FILTER_SRCS := src/core/filter_parser.c src/core/filter_compiler.c src/core/filter_eval.c src/core/filter_manager.c
CORRELATION_SRCS := src/core/time_window.c src/core/correlation_engine.c src/core/pattern_detector.c src/core/anomaly_detector.c
SECURITY_SRCS := src/core/security_detector.c src/web/access_control.c
INTEGRATION_SRCS := src/core/event_hooks.c
ALERT_SRCS := src/core/alert_manager.c
STORAGE_SRCS  := src/storage/storage_buffer.c src/storage/storage_db.c src/storage/audit_log.c src/storage/retention_policy.c src/storage/storage_layer.c
EXPORT_SRCS   := src/export/pcap_export.c src/export/json_export.c src/export/prometheus_exporter.c src/export/syslog_forwarder.c src/export/log_rotation.c src/export/export_layer.c
PLUGIN_SRCS   := src/plugins/plugin_loader.c src/plugins/plugin_lifecycle.c src/plugins/plugin_router.c
WEB_SRCS      := src/web/web_server.c src/web/web_api.c src/web/websocket_server.c src/web/web_auth.c src/web/web_dashboard.c
CLI_SRCS      := src/cli/cli_interface.c src/cli/cli_integration.c
CLI_OBJS      := $(CLI_SRCS:.c=.o)

# Collect all sources and objects
ALL_SRCS := $(CORE_SRCS) $(CORE_ENGINE_SRCS) $(MEMORY_MGMT_SRCS) $(NETLINK_SRCS) $(FILTER_SRCS) $(CORRELATION_SRCS) $(SECURITY_SRCS) $(INTEGRATION_SRCS) $(ALERT_SRCS)
ALL_OBJS := $(CORE_OBJS) $(CORE_ENGINE_SRCS:.c=.o) $(MEMORY_MGMT_SRCS:.c=.o) $(NETLINK_SRCS:.c=.o) $(FILTER_SRCS:.c=.o) $(CORRELATION_SRCS:.c=.o) $(SECURITY_SRCS:.c=.o) $(INTEGRATION_SRCS:.c=.o) $(ALERT_SRCS:.c=.o)

# Core dependencies (always required)
CORE_LIBS := libnl-route-3.0 libnl-3.0
LDLIBS := $(shell pkg-config --libs $(CORE_LIBS))
LDLIBS += -lev -lncursesw -lpthread -lm -lcurl
# Prioritize libnl-tiny includes over system libnl - must come BEFORE pkg-config
CFLAGS := $(LIBNL_INCLUDES) -Iinclude
CFLAGS += $(shell pkg-config --cflags $(CORE_LIBS))
CFLAGS += -g -Og -W -Wall -Wextra -Wno-unused-parameter

# Check for optional dependencies and set feature flags
ifeq ($(ENABLE_CONFIG),1)
    ifneq ($(shell pkg-config --exists yaml-0.1 && echo yes),yes)
        $(warning libyaml not found, disabling configuration management)
        ENABLE_CONFIG := 0
    else
        CFLAGS += -DENABLE_CONFIG=1
        LDLIBS += $(shell pkg-config --libs yaml-0.1)
        CFLAGS += $(shell pkg-config --cflags yaml-0.1)
        ALL_SRCS += $(CONFIG_SRCS)
        ALL_OBJS += $(CONFIG_SRCS:.c=.o)
    endif
endif

ifeq ($(ENABLE_STORAGE),1)
    ifneq ($(shell pkg-config --exists sqlite3 && echo yes),yes)
        $(warning sqlite3 not found, disabling database storage)
        ENABLE_STORAGE := 0
    else
        CFLAGS += -DENABLE_STORAGE=1
        LDLIBS += $(shell pkg-config --libs sqlite3)
        LDLIBS += -lssl -lcrypto
        CFLAGS += $(shell pkg-config --cflags sqlite3)
        ALL_SRCS += $(STORAGE_SRCS)
        ALL_OBJS += $(STORAGE_SRCS:.c=.o)
    endif
endif

ifeq ($(ENABLE_WEB),1)
    ifneq ($(shell pkg-config --exists libmicrohttpd && echo yes),yes)
        $(warning libmicrohttpd not found, disabling web dashboard)
        ENABLE_WEB := 0
    else
        CFLAGS += -DENABLE_WEB=1
        LDLIBS += $(shell pkg-config --libs libmicrohttpd)
        CFLAGS += $(shell pkg-config --cflags libmicrohttpd)
        ALL_SRCS += $(WEB_SRCS)
        ALL_OBJS += $(WEB_SRCS:.c=.o)
    endif
endif

ifeq ($(ENABLE_PLUGINS),1)
    CFLAGS += -DENABLE_PLUGINS=1
    LDLIBS += -ldl
    ALL_SRCS += $(PLUGIN_SRCS)
    ALL_OBJS += $(PLUGIN_SRCS:.c=.o)
endif

ifeq ($(ENABLE_EXPORT),1)
    CFLAGS += -DENABLE_EXPORT=1
    ALL_SRCS += $(EXPORT_SRCS)
    ALL_OBJS += $(EXPORT_SRCS:.c=.o)
endif

# Build targets
.PHONY: all clean distclean install uninstall check-deps core plugins web help tools
.PHONY: unit-tests run-unit-tests integration-tests benchmarks test-all
.PHONY: wmi-tests run-wmi-tests libnl-tiny

all: check-deps $(LIBNL_LIB) $(EXEC)

# libnl-tiny static library
$(LIBNL_LIB): $(LIBNL_OBJS)
	@echo "  AR      $@"
	@ar rcs $@ $^

$(LIBNL_DIR)/%.o: $(LIBNL_DIR)/%.c
	@echo "  CC      $@"
	@$(CC) -g -Og -W -Wall -Wextra -Wno-unused-parameter -I$(LIBNL_DIR)/include -c -o $@ $<

libnl-tiny: $(LIBNL_LIB)
	@echo "libnl-tiny library built"

tools: audit_verify

audit_verify: audit_verify.c src/storage/audit_log.o
	@echo "  CC      $@"
	@$(CC) $(CFLAGS) -o $@ $^ -lssl -lcrypto

# Test programs
test_security: test_security.c src/core/security_detector.o src/web/access_control.o src/storage/audit_log.o src/core/event_processor.o src/core/ring_buffer.o src/core/thread_pool.o src/core/rate_limiter.o src/core/object_pool.o
	@echo "  CC      $@"
	@$(CC) $(CFLAGS) -o $@ $^ -lssl -lcrypto -lpthread

test_alert_system: test_alert_system.c src/core/alert_manager.o $(FILTER_SRCS:.c=.o) $(CORE_ENGINE_SRCS:.c=.o) $(MEMORY_MGMT_SRCS:.c=.o)
	@echo "  CC      $@"
	@$(CC) $(CFLAGS) -o $@ $^ -lpthread -lcurl

# WMI test programs
test_wmi_parser: test_wmi_parser.c src/core/qca_wmi.o src/core/wmi_error.o
	@echo "  CC      $@"
	@$(CC) $(CFLAGS) -o $@ $^ -lpthread

test_wmi_timestamps: test_wmi_timestamps.c src/core/qca_wmi.o src/core/wmi_error.o
	@echo "  CC      $@"
	@$(CC) $(CFLAGS) -o $@ $^ -lpthread

test_wmi_log_reader: test_wmi_log_reader.c src/core/wmi_log_reader.o src/core/qca_wmi.o src/core/wmi_error.o
	@echo "  CC      $@"
	@$(CC) $(CFLAGS) -o $@ $^ -lpthread

test_wmi_event_bridge: test_wmi_event_bridge.c src/core/wmi_event_bridge.o src/core/wmi_log_reader.o src/core/qca_wmi.o src/core/wmi_error.o src/core/event_processor.o src/core/ring_buffer.o src/core/thread_pool.o src/core/rate_limiter.o src/core/object_pool.o
	@echo "  CC      $@"
	@$(CC) $(CFLAGS) -o $@ $^ -lpthread

test_wmi_error_handling: test_wmi_error_handling.c src/core/wmi_error.o src/core/qca_wmi.o src/core/wmi_log_reader.o
	@echo "  CC      $@"
	@$(CC) $(CFLAGS) -o $@ $^ -lpthread

test_wmi_cli: test_wmi_cli.c
	@echo "  CC      $@"
	@$(CC) $(CFLAGS) -o $@ $^ -lpthread

# Unit test targets
UNIT_TEST_SRCS := tests/unit/test_ring_buffer.c tests/unit/test_filter.c tests/unit/test_object_pool.c
UNIT_TEST_BINS := $(UNIT_TEST_SRCS:tests/unit/%.c=test_unit_%)

test_unit_ring_buffer: tests/unit/test_ring_buffer.c src/core/ring_buffer.o
	@echo "  CC      $@"
	@$(CC) $(CFLAGS) -Itests/unit -o $@ $^ -lpthread

test_unit_filter: tests/unit/test_filter.c $(FILTER_SRCS:.c=.o) src/core/event_processor.o src/core/ring_buffer.o src/core/thread_pool.o src/core/rate_limiter.o src/core/object_pool.o
	@echo "  CC      $@"
	@$(CC) $(CFLAGS) -Itests/unit -o $@ $^ -lpthread

test_unit_object_pool: tests/unit/test_object_pool.c src/core/object_pool.o
	@echo "  CC      $@"
	@$(CC) $(CFLAGS) -Itests/unit -o $@ $^ -lpthread

unit-tests: $(UNIT_TEST_BINS)
	@echo "Unit tests built successfully"

run-unit-tests: unit-tests
	@./tests/run_unit_tests.sh

# Integration test targets
INTEGRATION_TEST_SRCS := tests/integration/test_e2e_processing.c tests/integration/test_plugin_loading.c tests/integration/test_config_loading.c tests/integration/test_wmi_integration.c
INTEGRATION_TEST_BINS := $(INTEGRATION_TEST_SRCS:tests/integration/%.c=test_integration_%)

tests/integration/netlink_simulator.o: tests/integration/netlink_simulator.c
	@echo "  CC      $@"
	@$(CC) $(CFLAGS) -c -o $@ $<

test_integration_e2e_processing: tests/integration/test_e2e_processing.c tests/integration/netlink_simulator.o $(CORE_ENGINE_SRCS:.c=.o) $(MEMORY_MGMT_SRCS:.c=.o) $(FILTER_SRCS:.c=.o) $(STORAGE_SRCS:.c=.o)
	@echo "  CC      $@"
	@$(CC) $(CFLAGS) -Itests/unit -Itests/integration -o $@ $^ $(LDLIBS)

test_integration_plugin_loading: tests/integration/test_plugin_loading.c
	@echo "  CC      $@"
	@$(CC) $(CFLAGS) -Itests/unit -o $@ $^ -ldl

test_integration_config_loading: tests/integration/test_config_loading.c $(CONFIG_SRCS:.c=.o)
	@echo "  CC      $@"
	@$(CC) $(CFLAGS) -Itests/unit -o $@ $^ $(shell pkg-config --libs yaml-0.1 2>/dev/null || echo "")

test_integration_wmi_integration: tests/integration/test_wmi_integration.c src/core/wmi_log_reader.o src/core/wmi_event_bridge.o src/core/qca_wmi.o src/core/wmi_error.o src/core/event_processor.o src/core/ring_buffer.o src/core/thread_pool.o src/core/rate_limiter.o src/core/object_pool.o
	@echo "  CC      $@"
	@$(CC) $(CFLAGS) -Itests/unit -o $@ $^ -lpthread

integration-tests: $(INTEGRATION_TEST_BINS)
	@echo "Integration tests built successfully"

# Benchmark targets
BENCHMARK_SRCS := tests/benchmarks/bench_event_processing.c tests/benchmarks/bench_filter_evaluation.c tests/benchmarks/bench_memory_usage.c tests/benchmarks/bench_wmi_parsing.c
BENCHMARK_BINS := $(BENCHMARK_SRCS:tests/benchmarks/%.c=bench_%)

bench_event_processing: tests/benchmarks/bench_event_processing.c $(CORE_ENGINE_SRCS:.c=.o) $(MEMORY_MGMT_SRCS:.c=.o)
	@echo "  CC      $@"
	@$(CC) $(CFLAGS) -Itests/benchmarks -o $@ $^ -lpthread

bench_filter_evaluation: tests/benchmarks/bench_filter_evaluation.c $(FILTER_SRCS:.c=.o) $(CORE_ENGINE_SRCS:.c=.o) $(MEMORY_MGMT_SRCS:.c=.o)
	@echo "  CC      $@"
	@$(CC) $(CFLAGS) -Itests/benchmarks -o $@ $^ -lpthread

bench_memory_usage: tests/benchmarks/bench_memory_usage.c $(MEMORY_MGMT_SRCS:.c=.o) $(CORE_ENGINE_SRCS:.c=.o)
	@echo "  CC      $@"
	@$(CC) $(CFLAGS) -Itests/benchmarks -o $@ $^ -lpthread

bench_wmi_parsing: tests/benchmarks/bench_wmi_parsing.c src/core/qca_wmi.o src/core/wmi_error.o src/core/wmi_log_reader.o src/core/wmi_event_bridge.o src/core/event_processor.o src/core/ring_buffer.o src/core/thread_pool.o src/core/rate_limiter.o src/core/object_pool.o
	@echo "  CC      $@"
	@$(CC) $(CFLAGS) -Itests/benchmarks -o $@ $^ -lpthread

benchmarks: $(BENCHMARK_BINS)
	@echo "Benchmarks built successfully"

run-benchmarks: benchmarks
	@echo "Running benchmarks..."
	@for bench in $(BENCHMARK_BINS); do \
		if [ -f $$bench ]; then \
			./$$bench; \
		fi; \
	done

# Memory testing targets
test_stability: tests/memory/test_stability.c $(CORE_ENGINE_SRCS:.c=.o) $(MEMORY_MGMT_SRCS:.c=.o)
	@echo "  CC      $@"
	@$(CC) $(CFLAGS) -o $@ $^ -lpthread

memory-tests: test_stability
	@echo "Memory tests built successfully"

run-valgrind: unit-tests
	@./tests/memory/valgrind_test.sh

run-stability: test_stability
	@echo "Running stability test (60 seconds)..."
	@./test_stability 60

profile-memory: test_stability
	@./tests/memory/profile_memory.sh test_stability 30

# WMI test suite
WMI_TEST_BINS := test_wmi_parser test_wmi_timestamps test_wmi_log_reader test_wmi_event_bridge test_wmi_error_handling test_wmi_cli

wmi-tests: $(WMI_TEST_BINS)
	@echo "WMI tests built successfully"

run-wmi-tests: wmi-tests
	@echo "Running WMI tests..."
	@for test in $(WMI_TEST_BINS); do \
		if [ -f $$test ]; then \
			echo "Running $$test..."; \
			./$$test || exit 1; \
		fi; \
	done
	@echo "All WMI tests passed!"

# libnl-tiny integration test
test_libnl_integration: test_libnl_integration.c $(LIBNL_LIB)
	@echo "  CC      $@"
	@$(CC) $(CFLAGS) -o $@ $< $(LIBNL_LIB)

# Netlink comprehensive test suite
NL_UNIT_TEST_SRCS := tests/unit/test_nl_message_parsing.c tests/unit/test_nl_event_translation.c
NL_UNIT_TEST_BINS := $(NL_UNIT_TEST_SRCS:tests/unit/%.c=test_unit_%)

NL_INTEGRATION_TEST_SRCS := tests/integration/test_nl_integration.c tests/integration/test_nl_e2e.c
NL_INTEGRATION_TEST_BINS := $(NL_INTEGRATION_TEST_SRCS:tests/integration/%.c=test_integration_%)

NL_BENCHMARK_SRCS := tests/benchmarks/bench_nl_performance.c
NL_BENCHMARK_BINS := $(NL_BENCHMARK_SRCS:tests/benchmarks/%.c=%)

# Unit test targets for netlink
test_unit_nl_message_parsing: tests/unit/test_nl_message_parsing.c $(NETLINK_SRCS:.c=.o) $(LIBNL_LIB)
	@echo "  CC      $@"
	@$(CC) $(CFLAGS) -Itests/unit -o $@ $< $(NETLINK_SRCS:.c=.o) $(LIBNL_LIB) $(LDLIBS)

test_unit_nl_event_translation: tests/unit/test_nl_event_translation.c $(NETLINK_SRCS:.c=.o) $(LIBNL_LIB)
	@echo "  CC      $@"
	@$(CC) $(CFLAGS) -Itests/unit -o $@ $< $(NETLINK_SRCS:.c=.o) $(LIBNL_LIB) $(LDLIBS)

# Integration test targets for netlink
test_integration_nl_integration: tests/integration/test_nl_integration.c $(NETLINK_SRCS:.c=.o) $(CORE_ENGINE_SRCS:.c=.o) $(MEMORY_MGMT_SRCS:.c=.o) $(LIBNL_LIB)
	@echo "  CC      $@"
	@$(CC) $(CFLAGS) -Itests/unit -Itests/integration -o $@ $< $(NETLINK_SRCS:.c=.o) $(CORE_ENGINE_SRCS:.c=.o) $(MEMORY_MGMT_SRCS:.c=.o) $(LIBNL_LIB) $(LDLIBS)

test_integration_nl_e2e: tests/integration/test_nl_e2e.c $(NETLINK_SRCS:.c=.o) $(CORE_ENGINE_SRCS:.c=.o) $(MEMORY_MGMT_SRCS:.c=.o) $(FILTER_SRCS:.c=.o) $(LIBNL_LIB)
	@echo "  CC      $@"
	@$(CC) $(CFLAGS) -Itests/unit -Itests/integration -o $@ $< $(NETLINK_SRCS:.c=.o) $(CORE_ENGINE_SRCS:.c=.o) $(MEMORY_MGMT_SRCS:.c=.o) $(FILTER_SRCS:.c=.o) $(LIBNL_LIB) $(LDLIBS)

# Benchmark target for netlink
bench_nl_performance: tests/benchmarks/bench_nl_performance.c $(NETLINK_SRCS:.c=.o) $(CORE_ENGINE_SRCS:.c=.o) $(MEMORY_MGMT_SRCS:.c=.o) $(LIBNL_LIB)
	@echo "  CC      $@"
	@$(CC) $(CFLAGS) -Itests/benchmarks -o $@ $< $(NETLINK_SRCS:.c=.o) $(CORE_ENGINE_SRCS:.c=.o) $(MEMORY_MGMT_SRCS:.c=.o) $(LIBNL_LIB) $(LDLIBS)

# Build all netlink tests
nl-unit-tests: $(NL_UNIT_TEST_BINS)
	@echo "Netlink unit tests built successfully"

nl-integration-tests: $(NL_INTEGRATION_TEST_BINS)
	@echo "Netlink integration tests built successfully"

nl-benchmarks: $(NL_BENCHMARK_BINS)
	@echo "Netlink benchmarks built successfully"

nl-tests: nl-unit-tests nl-integration-tests nl-benchmarks
	@echo "All netlink tests built successfully"

# Run netlink tests
run-nl-unit-tests: nl-unit-tests
	@echo "Running netlink unit tests..."
	@for test in $(NL_UNIT_TEST_BINS); do \
		if [ -f $$test ]; then \
			echo "Running $$test..."; \
			./$$test || exit 1; \
		fi; \
	done
	@echo "All netlink unit tests passed!"

run-nl-integration-tests: nl-integration-tests
	@echo "Running netlink integration tests..."
	@for test in $(NL_INTEGRATION_TEST_BINS); do \
		if [ -f $$test ]; then \
			echo "Running $$test..."; \
			./$$test || exit 1; \
		fi; \
	done
	@echo "All netlink integration tests passed!"

run-nl-benchmarks: nl-benchmarks
	@echo "Running netlink benchmarks..."
	@for bench in $(NL_BENCHMARK_BINS); do \
		if [ -f $$bench ]; then \
			./$$bench; \
		fi; \
	done

run-nl-tests: run-nl-unit-tests run-nl-integration-tests run-nl-benchmarks
	@echo ""
	@echo "All netlink tests completed successfully!"

# Combined test target
test-all: run-unit-tests integration-tests run-benchmarks run-wmi-tests run-nl-tests
	@echo ""
	@echo "All tests completed successfully!"

$(EXEC): $(ALL_OBJS) $(LIBNL_LIB)
	@echo "  LD      $@"
	@$(CC) -o $@ $(ALL_OBJS) $(LIBNL_LIB) $(LDLIBS)

%.o: %.c
	@echo "  CC      $@"
	@$(CC) $(CFLAGS) -c -o $@ $<

# Modular build targets
core: $(CORE_OBJS)
	@echo "Core module built"

plugins: $(PLUGIN_SRCS:.c=.o)
	@echo "Plugin system built"

web: $(WEB_SRCS:.c=.o)
	@echo "Web dashboard built"

# Dependency checking
check-deps:
	@echo "Checking dependencies..."
	@pkg-config --exists $(CORE_LIBS) || (echo "ERROR: Core dependencies not found (libnl-3.0, libnl-route-3.0)" && exit 1)
	@echo "Core dependencies: OK"
	@if [ "$(ENABLE_CONFIG)" = "1" ]; then \
		pkg-config --exists yaml-0.1 && echo "  libyaml: OK" || echo "  libyaml: NOT FOUND (optional)"; \
	fi
	@if [ "$(ENABLE_STORAGE)" = "1" ]; then \
		pkg-config --exists sqlite3 && echo "  sqlite3: OK" || echo "  sqlite3: NOT FOUND (optional)"; \
	fi
	@if [ "$(ENABLE_WEB)" = "1" ]; then \
		pkg-config --exists libmicrohttpd && echo "  libmicrohttpd: OK" || echo "  libmicrohttpd: NOT FOUND (optional)"; \
	fi
	@echo "Feature flags:"
	@echo "  CONFIG:  $(ENABLE_CONFIG)"
	@echo "  STORAGE: $(ENABLE_STORAGE)"
	@echo "  WEB:     $(ENABLE_WEB)"
	@echo "  PLUGINS: $(ENABLE_PLUGINS)"
	@echo "  EXPORT:  $(ENABLE_EXPORT)"

# Installation
install: $(EXEC)
	@echo "Installing nlmon..."
	install -d $(DESTDIR)$(BINDIR)
	install -d $(DESTDIR)$(CONFDIR)
	install -d $(DESTDIR)$(DATADIR)
	install -m 0755 $(EXEC) $(DESTDIR)$(BINDIR)/
	@if [ "$(ENABLE_PLUGINS)" = "1" ]; then \
		install -d $(DESTDIR)$(LIBDIR)/plugins; \
		echo "Plugin directory created at $(DESTDIR)$(LIBDIR)/plugins"; \
	fi
	@echo "Installation complete"
	@echo "Binary: $(DESTDIR)$(BINDIR)/$(EXEC)"
	@echo "Config: $(DESTDIR)$(CONFDIR)/"
	@echo "Data:   $(DESTDIR)$(DATADIR)/"

uninstall:
	@echo "Uninstalling nlmon..."
	$(RM) $(DESTDIR)$(BINDIR)/$(EXEC)
	$(RM) -r $(DESTDIR)$(LIBDIR)
	@echo "Note: Config and data directories preserved"
	@echo "  $(DESTDIR)$(CONFDIR)/"
	@echo "  $(DESTDIR)$(DATADIR)/"

# Cleanup
clean:
	@echo "Cleaning build artifacts..."
	$(RM) $(EXEC) $(ALL_OBJS)
	$(RM) src/core/*.o src/config/*.o src/storage/*.o
	$(RM) src/export/*.o src/plugins/*.o src/web/*.o src/cli/*.o
	$(RM) $(UNIT_TEST_BINS) $(INTEGRATION_TEST_BINS) $(BENCHMARK_BINS) $(WMI_TEST_BINS)
	$(RM) test_stability test_security test_alert_system audit_verify test_libnl_integration
	$(RM) tests/integration/*.o
	$(RM) $(LIBNL_OBJS) $(LIBNL_LIB)

distclean: clean
	@echo "Cleaning all generated files..."
	$(RM) *.o *~ *.bak
	$(RM) src/*/*.o src/*/*~ src/*/*.bak
	$(RM) $(LIBNL_DIR)/*.o

# Test targets
test_cli: test_cli.c $(CLI_OBJS)
	@echo "Building CLI test..."
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_web_dashboard: test_web_dashboard.c $(WEB_SRCS:.c=.o) $(STORAGE_SRCS:.c=.o) $(FILTER_SRCS:.c=.o) $(CORE_ENGINE_SRCS:.c=.o) $(MEMORY_MGMT_SRCS:.c=.o) $(CONFIG_SRCS:.c=.o)
	@echo "Building web dashboard test..."
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

# Help target
help:
	@echo "nlmon Enhanced Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all          - Build nlmon with enabled features (default)"
	@echo "  core         - Build only core module"
	@echo "  plugins      - Build plugin system"
	@echo "  web          - Build web dashboard"
	@echo "  check-deps   - Check for required and optional dependencies"
	@echo "  install      - Install nlmon to PREFIX (default: /usr/local)"
	@echo "  uninstall    - Remove installed files"
	@echo "  clean        - Remove build artifacts"
	@echo "  distclean    - Remove all generated files"
	@echo "  help         - Show this help message"
	@echo ""
	@echo "Test Targets:"
	@echo "  unit-tests       - Build unit tests"
	@echo "  run-unit-tests   - Build and run unit tests"
	@echo "  integration-tests - Build integration tests"
	@echo "  benchmarks       - Build benchmark tests"
	@echo "  run-benchmarks   - Build and run benchmarks"
	@echo "  wmi-tests        - Build WMI test suite"
	@echo "  run-wmi-tests    - Build and run WMI tests"
	@echo "  memory-tests     - Build memory/stability tests"
	@echo "  run-valgrind     - Run unit tests under valgrind"
	@echo "  run-stability    - Run stability test (60 seconds)"
	@echo "  test-all         - Run all test suites"
	@echo ""
	@echo "Feature Flags (set to 0 to disable):"
	@echo "  ENABLE_CONFIG=1    - Configuration management (requires libyaml)"
	@echo "  ENABLE_STORAGE=1   - Database storage (requires sqlite3)"
	@echo "  ENABLE_WEB=1       - Web dashboard (requires libmicrohttpd)"
	@echo "  ENABLE_PLUGINS=1   - Plugin system"
	@echo "  ENABLE_EXPORT=1    - Export modules"
	@echo ""
	@echo "Installation Paths:"
	@echo "  PREFIX=$(PREFIX)"
	@echo "  BINDIR=$(BINDIR)"
	@echo "  LIBDIR=$(LIBDIR)"
	@echo "  CONFDIR=$(CONFDIR)"
	@echo "  DATADIR=$(DATADIR)"
	@echo ""
	@echo "Examples:"
	@echo "  make                              - Build with all available features"
	@echo "  make ENABLE_WEB=0                 - Build without web dashboard"
	@echo "  make PREFIX=/opt/nlmon install    - Install to /opt/nlmon"
	@echo "  make check-deps                   - Check what dependencies are available"
	@echo "  make run-wmi-tests                - Build and run WMI test suite"
	@echo "  make test-all                     - Run all tests including WMI tests"

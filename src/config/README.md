# nlmon Configuration Management System

This directory contains the implementation of nlmon's configuration management system, which provides YAML-based configuration with hot-reload capability.

## Features

- **YAML Configuration**: Human-readable configuration format with support for all nlmon features
- **Environment Variable Substitution**: Use `${VAR_NAME}` syntax to reference environment variables
- **Hot-Reload**: Automatic configuration reload when the config file changes (using inotify)
- **Thread-Safe Access**: Configuration can be safely accessed from multiple threads
- **Validation**: Comprehensive validation of all configuration values with detailed error messages
- **Default Values**: Sensible defaults for all configuration options

## Files

- `config.c` - Core configuration data structures and accessor functions
- `yaml_parser.c` - YAML file parsing and environment variable expansion
- `hot_reload.c` - Configuration file watching and atomic reload mechanism

## Configuration Structure

The configuration is organized into the following sections:

### Core Configuration
- `buffer_size` - Event buffer size (supports KB, MB, GB suffixes)
- `max_events` - Maximum events in memory buffer
- `rate_limit` - Events per second limit
- `worker_threads` - Number of worker threads

### Monitoring Configuration
- `protocols` - Netlink protocol families to monitor
- `interfaces` - Interface include/exclude patterns
- `message_types` - Specific message types to monitor
- `namespaces` - Network namespace support

### Output Configuration
- `console` - Console output settings
- `pcap` - PCAP file output with rotation
- `database` - SQLite database storage

### CLI Configuration
- `enabled` - Enable ncurses CLI mode
- `refresh_rate` - Screen refresh rate
- `max_history` - Maximum events in history

### Web Configuration
- `enabled` - Enable web dashboard
- `port` - HTTP port
- `tls` - TLS/HTTPS settings

### Metrics Configuration
- `enabled` - Enable Prometheus metrics
- `port` - Metrics HTTP port
- `path` - Metrics endpoint path

### Plugins Configuration
- `directory` - Plugin directory path
- `enabled` - List of enabled plugins

### Alerts Configuration
- Alert rules with conditions and actions

### Integration Configuration
- `kubernetes` - Kubernetes integration settings
- `docker` - Docker integration settings
- `syslog` - Syslog forwarding settings
- `snmp` - SNMP trap settings

## Usage

### Basic Usage

```c
#include "nlmon_config.h"

struct nlmon_config_ctx ctx;
int ret;

/* Initialize configuration context */
ret = nlmon_config_ctx_init(&ctx, "/etc/nlmon/config.yaml");
if (ret != NLMON_CONFIG_OK) {
    fprintf(stderr, "Config error: %s\n", nlmon_config_error_string(ret));
    return 1;
}

/* Get configuration values (thread-safe) */
struct nlmon_core_config core;
nlmon_config_get_core(&ctx, &core);

printf("Buffer size: %zu\n", core.buffer_size);
printf("Max events: %d\n", core.max_events);

/* Cleanup */
nlmon_config_ctx_free(&ctx);
```

### Hot-Reload Usage

```c
/* Initialize file watching */
ret = nlmon_config_watch_init(&ctx);
if (ret == NLMON_CONFIG_OK) {
    /* Get file descriptor for select/poll */
    int watch_fd = nlmon_config_watch_get_fd(&ctx);
    
    /* In event loop */
    if (nlmon_config_watch_check(&ctx)) {
        /* Configuration file changed */
        ret = nlmon_config_ctx_reload(&ctx);
        if (ret == NLMON_CONFIG_OK) {
            printf("Configuration reloaded\n");
        }
    }
}
```

### Environment Variable Overrides

Environment variables with the `NLMON_` prefix override configuration values:

```bash
export NLMON_BUFFER_SIZE=512000
export NLMON_MAX_EVENTS=20000
export NLMON_WEB_ENABLED=true
export NLMON_WEB_PORT=9090
```

## Configuration File Format

See `nlmon.yaml.example` in the project root for a complete example configuration file.

Example:

```yaml
nlmon:
  core:
    buffer_size: 320KB
    max_events: 10000
    rate_limit: 1000
    worker_threads: 4
  
  monitoring:
    protocols:
      - 0  # NETLINK_ROUTE
    interfaces:
      include:
        - "eth*"
        - "veth*"
      exclude:
        - "lo"
  
  output:
    console:
      enabled: true
      format: "text"
    pcap:
      enabled: false
      file: "/var/log/nlmon/capture.pcap"
      rotate_size: 100MB
```

## Environment Variable Substitution

Configuration values can reference environment variables:

```yaml
nlmon:
  output:
    database:
      path: "${HOME}/.nlmon/events.db"
  
  integration:
    syslog:
      server: "${SYSLOG_SERVER}:514"
```

## Validation

All configuration values are validated when loaded. Invalid values will cause the configuration load to fail with a detailed error message:

- Buffer size: 1KB - 100MB
- Max events: 100 - 1,000,000
- Rate limit: 0 - 100,000
- Worker threads: 1 - 64
- Ports: 1 - 65535
- Retention days: 1 - 3650

## Thread Safety

The configuration system uses read-write locks to ensure thread-safe access:

- Multiple threads can read configuration simultaneously
- Configuration updates are atomic
- Old configuration remains valid until all readers are done

## Hot-Reload Mechanism

The hot-reload system uses inotify to watch for configuration file changes:

1. File modification detected via inotify
2. New configuration loaded and validated
3. If valid, atomic swap with old configuration
4. Old configuration freed after grace period

The reload is non-disruptive - the application continues running with the old configuration until the new one is validated and ready.

## Testing

A test program is provided to verify the configuration system:

```bash
gcc -o test_config test_config.c src/config/*.c -Iinclude $(pkg-config --cflags --libs yaml-0.1) -lpthread
./test_config nlmon.yaml.example
```

## Error Handling

All functions return error codes from `enum nlmon_config_error`:

- `NLMON_CONFIG_OK` - Success
- `NLMON_CONFIG_ERR_NOMEM` - Out of memory
- `NLMON_CONFIG_ERR_FILE_NOT_FOUND` - Config file not found
- `NLMON_CONFIG_ERR_PARSE_ERROR` - YAML parsing failed
- `NLMON_CONFIG_ERR_INVALID_VALUE` - Invalid parameter
- `NLMON_CONFIG_ERR_VALIDATION` - Validation failed

Use `nlmon_config_error_string()` to get human-readable error messages.

## Dependencies

- libyaml (yaml-0.1) - YAML parsing
- pthread - Thread synchronization
- inotify - File watching (Linux kernel feature)

## Future Enhancements

- Configuration schema validation
- Configuration migration between versions
- Remote configuration management
- Configuration change callbacks
- Per-section reload (avoid full reload)

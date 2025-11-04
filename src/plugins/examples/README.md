# nlmon Example Plugins

This directory contains example plugins demonstrating the nlmon plugin API. These plugins serve as templates and learning resources for developing custom nlmon plugins.

## Available Example Plugins

### 1. Event Logger Plugin (`event_logger_plugin.c`)

A simple plugin that logs all network events to a file with timestamps.

**Features:**
- Logs events in text or JSON format
- Configurable log file path
- Automatic flushing at intervals
- Statistics command
- Configuration hot-reload support

**Configuration:**
```yaml
plugins:
  enabled:
    - event_logger
    
event_logger:
  log_path: /var/log/nlmon/events.log
  log_format: text  # or json
  flush_interval: 10
```

**Commands:**
- `logger_stats` - Show event logger statistics

**Use Cases:**
- Persistent event logging
- Audit trail creation
- Event archival

### 2. CSV Exporter Plugin (`csv_exporter_plugin.c`)

Exports network events to CSV format for analysis in spreadsheet applications.

**Features:**
- CSV format export
- Configurable field selection
- Automatic file rotation
- Custom rotation size
- Manual rotation command

**Configuration:**
```yaml
plugins:
  enabled:
    - csv_exporter
    
csv_exporter:
  output_path: /var/log/nlmon/events.csv
  fields: timestamp,interface,event_type
  rotate_size: 100  # MB
  include_header: true
```

**Commands:**
- `csv_stats` - Show CSV exporter statistics
- `csv_rotate` - Manually rotate CSV file

**Use Cases:**
- Data analysis in Excel/LibreOffice
- Integration with BI tools
- Historical trend analysis

### 3. CLI Command Plugin (`cli_command_plugin.c`)

Adds utility CLI commands for system information and diagnostics.

**Features:**
- System information display
- Network statistics
- Memory usage information
- Uptime tracking

**Commands:**
- `sysinfo` - Display system information (hostname, OS, kernel, etc.)
- `netstat` - Show network interface statistics
- `meminfo` - Display memory usage
- `uptime` - Show nlmon uptime and statistics

**Use Cases:**
- Quick system diagnostics
- Troubleshooting assistance
- System monitoring integration

## Building the Example Plugins

### Prerequisites

- GCC compiler
- nlmon headers installed
- Make

### Build All Plugins

```bash
cd src/plugins/examples
make
```

This will create:
- `event_logger.so`
- `csv_exporter.so`
- `cli_commands.so`

### Build Individual Plugin

```bash
make event_logger.so
make csv_exporter.so
make cli_commands.so
```

### Clean Build Artifacts

```bash
make clean
```

## Installing the Example Plugins

### System-wide Installation

```bash
sudo make install
```

This installs plugins to `/usr/lib/nlmon/plugins/`.

### Custom Installation Directory

```bash
sudo make install PLUGIN_DIR=/custom/path/to/plugins
```

### Manual Installation

```bash
sudo cp event_logger.so /usr/lib/nlmon/plugins/
sudo cp csv_exporter.so /usr/lib/nlmon/plugins/
sudo cp cli_commands.so /usr/lib/nlmon/plugins/
```

## Using the Example Plugins

### Enable in Configuration

Edit `/etc/nlmon/nlmon.yaml`:

```yaml
plugins:
  directory: /usr/lib/nlmon/plugins
  enabled:
    - event_logger
    - csv_exporter
    - cli_commands
```

### Verify Plugin Loading

Start nlmon and check the logs:

```bash
sudo nlmon
```

You should see messages like:
```
Loaded plugin: event_logger v1.0.0 - Logs all network events to a file
Loaded plugin: csv_exporter v1.0.0 - Exports network events to CSV format
Loaded plugin: cli_commands v1.0.0 - Adds utility CLI commands
```

### Test Plugin Commands

In the nlmon CLI, press `:` to enter command mode and try:

```
:logger_stats
:csv_stats
:sysinfo
:netstat
:meminfo
:uptime
```

## Customizing the Example Plugins

### Modify Event Logger

To change the log format:

1. Edit `event_logger_plugin.c`
2. Modify `log_event_text()` or `log_event_json()` functions
3. Rebuild: `make event_logger.so`
4. Reinstall: `sudo make install`

### Modify CSV Exporter

To add custom fields:

1. Edit `csv_exporter_plugin.c`
2. Update `csv_exporter_on_event()` to include desired fields
3. Update default fields in configuration
4. Rebuild and reinstall

### Add New Commands

To add commands to CLI plugin:

1. Edit `cli_command_plugin.c`
2. Create new command handler function
3. Register command in `cli_command_init()`
4. Add dispatch in `cli_command_on_command()`
5. Rebuild and reinstall

## Plugin Development Tips

### 1. Start with an Example

Copy one of the example plugins as a template:

```bash
cp event_logger_plugin.c my_plugin.c
```

### 2. Update Plugin Metadata

```c
static nlmon_plugin_t my_plugin = {
    .name = "my_plugin",
    .version = "1.0.0",
    .description = "My custom plugin",
    // ...
};
```

### 3. Implement Required Callbacks

At minimum, implement `init()` and `cleanup()`:

```c
static int my_plugin_init(nlmon_plugin_context_t *ctx) {
    // Initialize plugin
    return 0;
}

static void my_plugin_cleanup(void) {
    // Cleanup resources
}
```

### 4. Add Event Processing (Optional)

```c
static int my_plugin_on_event(struct nlmon_event *event) {
    // Process event
    return 0;
}
```

### 5. Register Commands (Optional)

```c
static int my_plugin_init(nlmon_plugin_context_t *ctx) {
    ctx->register_command("mycommand", my_handler, "My command help");
    return 0;
}
```

### 6. Build and Test

```bash
gcc -shared -fPIC -o my_plugin.so my_plugin.c -I../../../include
sudo cp my_plugin.so /usr/lib/nlmon/plugins/
```

### 7. Debug with Logging

Use the context logging API:

```c
ctx->log(NLMON_LOG_DEBUG, "Debug message: %d", value);
ctx->log(NLMON_LOG_INFO, "Info message");
ctx->log(NLMON_LOG_WARN, "Warning message");
ctx->log(NLMON_LOG_ERROR, "Error message");
```

## Common Patterns

### Maintaining Plugin State

```c
static struct {
    FILE *file;
    uint64_t counter;
    nlmon_plugin_context_t *ctx;
} plugin_state;

static int my_init(nlmon_plugin_context_t *ctx) {
    memset(&plugin_state, 0, sizeof(plugin_state));
    plugin_state.ctx = ctx;
    return 0;
}
```

### Reading Configuration

```c
const char *value = ctx->get_config("my_plugin.setting");
if (value) {
    // Use configuration value
}
```

### Handling Config Reload

```c
static int my_on_config_reload(nlmon_plugin_context_t *ctx) {
    const char *new_value = ctx->get_config("my_plugin.setting");
    // Update plugin behavior
    return 0;
}
```

### Event Filtering

```c
static int my_event_filter(struct nlmon_event *event) {
    // Return 1 to process, 0 to skip
    return event->message_type == RTM_NEWLINK;
}

static nlmon_plugin_t my_plugin = {
    // ...
    .event_filter = my_event_filter,
};
```

### Error Handling

```c
static int my_plugin_on_event(struct nlmon_event *event) {
    if (!event) {
        ctx->log(NLMON_LOG_ERROR, "Null event received");
        return -1;
    }
    
    // Process event
    
    return 0;  // Success
}
```

## Troubleshooting

### Plugin Not Loading

**Problem:** Plugin doesn't appear in nlmon logs

**Solutions:**
- Check plugin is in correct directory
- Verify file permissions (should be readable)
- Check API version matches
- Review nlmon logs for error messages

### Plugin Crashes

**Problem:** nlmon crashes when plugin loads

**Solutions:**
- Check for NULL pointer dereferences
- Verify buffer sizes
- Use valgrind to detect memory errors
- Add defensive checks in callbacks

### Commands Not Working

**Problem:** Plugin commands don't execute

**Solutions:**
- Verify command registration in `init()`
- Check command name spelling
- Ensure `on_command()` callback is implemented
- Check command dispatch logic

### Configuration Not Loading

**Problem:** Plugin doesn't read configuration

**Solutions:**
- Verify configuration key names
- Check YAML syntax
- Ensure plugin is enabled in config
- Use logging to debug config values

## Additional Resources

- [Plugin API Documentation](../PLUGIN_API.md)
- [Plugin System README](../README.md)
- [nlmon Configuration Guide](../../../docs/configuration.md)
- [Plugin Development Guide](../../../docs/PLUGIN_DEVELOPMENT_GUIDE.md)

## Contributing

To contribute new example plugins:

1. Create plugin following the patterns above
2. Add comprehensive comments
3. Include configuration examples
4. Update this README
5. Add to Makefile
6. Test thoroughly
7. Submit pull request

## License

These example plugins are provided under the same license as nlmon.

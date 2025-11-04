# nlmon Plugin API Documentation

## Overview

The nlmon plugin system allows extending functionality without modifying core code. Plugins are dynamically loaded shared libraries that can:

- Process network events
- Generate custom events
- Register CLI commands
- Integrate with external systems
- Export data in custom formats

## Plugin API Version

Current API version: **1**

Plugins must specify the API version they were built against. The plugin manager will verify compatibility at load time.

## Creating a Plugin

### Basic Plugin Structure

```c
#include "plugin_api.h"

/* Plugin initialization */
static int my_plugin_init(nlmon_plugin_context_t *ctx) {
    ctx->log(NLMON_LOG_INFO, "My plugin initialized");
    return 0;
}

/* Plugin cleanup */
static void my_plugin_cleanup(void) {
    /* Cleanup resources */
}

/* Event handler */
static int my_plugin_on_event(struct nlmon_event *event) {
    /* Process event */
    return 0;
}

/* Plugin descriptor */
static nlmon_plugin_t my_plugin = {
    .name = "my_plugin",
    .version = "1.0.0",
    .description = "Example plugin",
    .api_version = NLMON_PLUGIN_API_VERSION,
    .callbacks = {
        .init = my_plugin_init,
        .cleanup = my_plugin_cleanup,
        .on_event = my_plugin_on_event,
    },
    .flags = NLMON_PLUGIN_FLAG_NONE,
    .dependencies = NULL
};

/* Plugin registration */
NLMON_PLUGIN_DEFINE(my_plugin);
```

### Building a Plugin

```bash
gcc -shared -fPIC -o my_plugin.so my_plugin.c -I/usr/include/nlmon
```

### Installing a Plugin

Copy the `.so` file to the plugin directory (default: `/usr/lib/nlmon/plugins/`):

```bash
sudo cp my_plugin.so /usr/lib/nlmon/plugins/
```

## Plugin Lifecycle

1. **Discovery**: Plugin manager scans plugin directory for `.so` files
2. **Load**: Plugin is loaded with `dlopen()`
3. **Register**: Plugin's `nlmon_plugin_register()` function is called
4. **Verify**: API version and dependencies are checked
5. **Initialize**: Plugin's `init()` callback is invoked
6. **Active**: Plugin processes events and handles commands
7. **Cleanup**: Plugin's `cleanup()` callback is invoked
8. **Unload**: Plugin is unloaded with `dlclose()`

## Plugin Context

The `nlmon_plugin_context_t` structure provides the plugin API:

### Logging

```c
ctx->log(NLMON_LOG_INFO, "Message: %s", msg);
```

Log levels:
- `NLMON_LOG_DEBUG`: Debug information
- `NLMON_LOG_INFO`: Informational messages
- `NLMON_LOG_WARN`: Warning messages
- `NLMON_LOG_ERROR`: Error messages

### Registering Commands

```c
static int my_command_handler(const char *args, char *response, size_t resp_len) {
    snprintf(response, resp_len, "Command executed with args: %s", args);
    return 0;
}

static int my_plugin_init(nlmon_plugin_context_t *ctx) {
    ctx->register_command("mycommand", my_command_handler, "Execute my command");
    return 0;
}
```

### Emitting Events

```c
struct nlmon_event *event = /* create event */;
ctx->emit_event(event);
```

### Getting Configuration

```c
const char *value = ctx->get_config("my_plugin.setting");
```

### Storing Plugin Data

```c
struct my_plugin_data *data = malloc(sizeof(*data));
ctx->plugin_data = data;

/* Later, retrieve it */
struct my_plugin_data *data = ctx->plugin_data;
```

## Callbacks

### init

Called once when the plugin is initialized.

```c
int (*init)(nlmon_plugin_context_t *ctx);
```

**Returns**: 0 on success, -1 on error

### cleanup

Called once when the plugin is being unloaded.

```c
void (*cleanup)(void);
```

### on_event

Called for each network event (if plugin subscribes to events).

```c
int (*on_event)(struct nlmon_event *event);
```

**Returns**: 
- 0: Event processed successfully
- -1: Error processing event
- 1: Event should be filtered (not passed to other plugins)

### on_command

Called when a plugin-registered command is invoked.

```c
int (*on_command)(const char *cmd, const char *args, char *response, size_t resp_len);
```

**Parameters**:
- `cmd`: Command name
- `args`: Command arguments
- `response`: Buffer for response message
- `resp_len`: Size of response buffer

**Returns**: 0 on success, -1 on error

### on_config_reload

Called when configuration is reloaded.

```c
int (*on_config_reload)(nlmon_plugin_context_t *ctx);
```

**Returns**: 0 on success, -1 on error

## Event Filtering

Plugins can filter events by providing an event filter function:

```c
static int my_event_filter(struct nlmon_event *event) {
    /* Return 1 to process event, 0 to skip */
    return event->message_type == RTM_NEWLINK;
}

static nlmon_plugin_t my_plugin = {
    /* ... */
    .event_filter = my_event_filter,
};
```

## Plugin Flags

### NLMON_PLUGIN_FLAG_NONE

Default behavior - plugin receives filtered events.

### NLMON_PLUGIN_FLAG_PROCESS_ALL

Plugin receives all events, regardless of filter.

### NLMON_PLUGIN_FLAG_ASYNC

Plugin event processing is asynchronous (events queued).

### NLMON_PLUGIN_FLAG_CRITICAL

Plugin is critical - if it fails to load or initialize, nlmon will not start.

## Dependencies

Plugins can declare dependencies on other plugins:

```c
static const char *deps[] = {
    "base_plugin",
    "helper_plugin",
    NULL
};

static nlmon_plugin_t my_plugin = {
    /* ... */
    .dependencies = deps,
};
```

Dependencies are loaded and initialized before dependent plugins.

## Error Handling

### Plugin Errors

If a plugin's `init()` returns an error:
- Plugin is marked as failed
- Plugin is not activated
- nlmon continues (unless plugin has `NLMON_PLUGIN_FLAG_CRITICAL`)

### Event Processing Errors

If `on_event()` returns an error:
- Error is logged
- Error counter is incremented
- Event processing continues

### Timeout Protection

Event processing has timeout protection:
- Default timeout: 1 second
- If plugin exceeds timeout, it's disabled
- Error is logged

## Best Practices

1. **Keep init() fast**: Avoid long-running operations in init()
2. **Handle errors gracefully**: Always check return values
3. **Clean up resources**: Free all allocated memory in cleanup()
4. **Use logging**: Log important events and errors
5. **Validate input**: Check event data before processing
6. **Be thread-safe**: Use proper synchronization if needed
7. **Document commands**: Provide clear help text for commands
8. **Version your plugin**: Use semantic versioning
9. **Test thoroughly**: Test with various event types and edge cases
10. **Handle config reload**: Update settings in on_config_reload()

## Example Plugins

### Event Logger Plugin

```c
#include "plugin_api.h"
#include <stdio.h>

static FILE *logfile = NULL;

static int logger_init(nlmon_plugin_context_t *ctx) {
    const char *path = ctx->get_config("logger.path");
    if (!path) path = "/var/log/nlmon/events.log";
    
    logfile = fopen(path, "a");
    if (!logfile) {
        ctx->log(NLMON_LOG_ERROR, "Failed to open log file: %s", path);
        return -1;
    }
    
    ctx->log(NLMON_LOG_INFO, "Event logger initialized: %s", path);
    return 0;
}

static void logger_cleanup(void) {
    if (logfile) {
        fclose(logfile);
        logfile = NULL;
    }
}

static int logger_on_event(struct nlmon_event *event) {
    if (!logfile) return -1;
    
    fprintf(logfile, "[%lu] %s: %s\n",
            event->timestamp,
            event->interface,
            /* event details */);
    fflush(logfile);
    
    return 0;
}

static nlmon_plugin_t logger_plugin = {
    .name = "event_logger",
    .version = "1.0.0",
    .description = "Logs events to file",
    .api_version = NLMON_PLUGIN_API_VERSION,
    .callbacks = {
        .init = logger_init,
        .cleanup = logger_cleanup,
        .on_event = logger_on_event,
    },
    .flags = NLMON_PLUGIN_FLAG_PROCESS_ALL,
};

NLMON_PLUGIN_DEFINE(logger_plugin);
```

### Custom Command Plugin

```c
#include "plugin_api.h"
#include <string.h>

static nlmon_plugin_context_t *g_ctx = NULL;

static int stats_command(const char *args, char *response, size_t resp_len) {
    snprintf(response, resp_len, "Plugin statistics: ...");
    return 0;
}

static int cmd_plugin_init(nlmon_plugin_context_t *ctx) {
    g_ctx = ctx;
    ctx->register_command("stats", stats_command, "Show plugin statistics");
    return 0;
}

static nlmon_plugin_t cmd_plugin = {
    .name = "command_plugin",
    .version = "1.0.0",
    .description = "Adds custom commands",
    .api_version = NLMON_PLUGIN_API_VERSION,
    .callbacks = {
        .init = cmd_plugin_init,
    },
};

NLMON_PLUGIN_DEFINE(cmd_plugin);
```

## Troubleshooting

### Plugin Not Loading

- Check plugin is in correct directory
- Verify plugin has correct permissions (readable)
- Check API version compatibility
- Review nlmon logs for error messages

### Plugin Crashes

- Use valgrind to detect memory errors
- Check for NULL pointer dereferences
- Verify buffer sizes
- Test with various event types

### Performance Issues

- Profile plugin with gprof or perf
- Minimize work in on_event()
- Use event filtering to reduce load
- Consider async processing flag

## API Reference

See `plugin_api.h` for complete API reference.

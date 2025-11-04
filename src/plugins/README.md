# nlmon Plugin System

## Overview

The nlmon plugin system provides a flexible, extensible architecture for adding custom functionality without modifying the core application. Plugins are dynamically loaded shared libraries that can process events, register commands, and integrate with external systems.

## Architecture

### Components

1. **Plugin API** (`include/plugin_api.h`)
   - Defines the plugin interface and data structures
   - Provides callback definitions for plugin lifecycle
   - Declares the plugin context for accessing nlmon functionality

2. **Plugin Manager** (`include/plugin_manager.h`)
   - Manages plugin discovery, loading, and lifecycle
   - Handles plugin dependencies and initialization order
   - Routes events to registered plugins

3. **Plugin Loader** (`src/plugins/plugin_loader.c`)
   - Discovers plugins in the plugin directory
   - Loads plugins using `dlopen()`
   - Verifies API version compatibility
   - Resolves plugin symbols

4. **Plugin Lifecycle** (`src/plugins/plugin_lifecycle.c`)
   - Manages plugin initialization and cleanup
   - Handles plugin enable/disable functionality
   - Supports plugin reload capability
   - Manages plugin dependencies

5. **Plugin Router** (`src/plugins/plugin_router.c`)
   - Routes events to all registered plugins
   - Applies event filters
   - Provides timeout protection
   - Handles plugin errors and automatic disable

## Plugin Lifecycle

```
┌─────────────┐
│  Discovery  │  Scan plugin directory for .so files
└──────┬──────┘
       │
       ▼
┌─────────────┐
│    Load     │  dlopen() and resolve symbols
└──────┬──────┘
       │
       ▼
┌─────────────┐
│  Register   │  Call nlmon_plugin_register()
└──────┬──────┘
       │
       ▼
┌─────────────┐
│   Verify    │  Check API version and dependencies
└──────┬──────┘
       │
       ▼
┌─────────────┐
│ Initialize  │  Call init() callback
└──────┬──────┘
       │
       ▼
┌─────────────┐
│   Active    │  Process events and handle commands
└──────┬──────┘
       │
       ▼
┌─────────────┐
│  Cleanup    │  Call cleanup() callback
└──────┬──────┘
       │
       ▼
┌─────────────┐
│   Unload    │  dlclose()
└─────────────┘
```

## Creating a Plugin

### Minimal Plugin Example

```c
#include "plugin_api.h"

static int my_plugin_init(nlmon_plugin_context_t *ctx) {
    ctx->log(NLMON_LOG_INFO, "My plugin initialized");
    return 0;
}

static void my_plugin_cleanup(void) {
    /* Cleanup resources */
}

static nlmon_plugin_t my_plugin = {
    .name = "my_plugin",
    .version = "1.0.0",
    .description = "My custom plugin",
    .api_version = NLMON_PLUGIN_API_VERSION,
    .callbacks = {
        .init = my_plugin_init,
        .cleanup = my_plugin_cleanup,
    },
};

NLMON_PLUGIN_DEFINE(my_plugin);
```

### Building a Plugin

```bash
gcc -shared -fPIC -o my_plugin.so my_plugin.c -I/path/to/nlmon/include
```

### Installing a Plugin

```bash
sudo cp my_plugin.so /usr/lib/nlmon/plugins/
```

## Plugin Capabilities

### Event Processing

Plugins can process network events:

```c
static int my_plugin_on_event(struct nlmon_event *event) {
    /* Process event */
    return 0;  /* 0=success, -1=error, 1=filter event */
}

static nlmon_plugin_t my_plugin = {
    /* ... */
    .callbacks = {
        .on_event = my_plugin_on_event,
    },
};
```

### Event Filtering

Plugins can filter which events they receive:

```c
static int my_event_filter(struct nlmon_event *event) {
    /* Return 1 to process, 0 to skip */
    return event->message_type == RTM_NEWLINK;
}

static nlmon_plugin_t my_plugin = {
    /* ... */
    .event_filter = my_event_filter,
};
```

### Custom Commands

Plugins can register CLI commands:

```c
static int my_command(const char *args, char *response, size_t resp_len) {
    snprintf(response, resp_len, "Command executed");
    return 0;
}

static int my_plugin_init(nlmon_plugin_context_t *ctx) {
    ctx->register_command("mycommand", my_command, "Execute my command");
    return 0;
}
```

### Configuration Access

Plugins can read configuration values:

```c
static int my_plugin_init(nlmon_plugin_context_t *ctx) {
    const char *value = ctx->get_config("my_plugin.setting");
    if (value) {
        /* Use configuration value */
    }
    return 0;
}
```

### Event Generation

Plugins can generate new events:

```c
static void generate_custom_event(nlmon_plugin_context_t *ctx) {
    struct nlmon_event *event = /* create event */;
    ctx->emit_event(event);
}
```

## Plugin Flags

### NLMON_PLUGIN_FLAG_NONE (0x00)
Default behavior - plugin receives filtered events.

### NLMON_PLUGIN_FLAG_PROCESS_ALL (0x01)
Plugin receives all events, regardless of filter.

### NLMON_PLUGIN_FLAG_ASYNC (0x02)
Plugin event processing is asynchronous (events queued).

### NLMON_PLUGIN_FLAG_CRITICAL (0x04)
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

The plugin manager ensures dependencies are loaded and initialized before dependent plugins.

## Error Handling

### Initialization Errors

If a plugin's `init()` returns an error:
- Plugin is marked as failed
- Plugin is not activated
- nlmon continues (unless plugin has `NLMON_PLUGIN_FLAG_CRITICAL`)

### Event Processing Errors

If `on_event()` returns an error:
- Error is logged
- Error counter is incremented
- If error count exceeds threshold (10), plugin is automatically disabled

### Timeout Protection

Event processing has timeout protection (default: 1 second):
- If plugin exceeds timeout, it's disabled
- Error is logged with plugin name

## Plugin Manager API

### Creating Plugin Manager

```c
plugin_manager_t *mgr = plugin_manager_create("/usr/lib/nlmon/plugins");
```

### Discovering Plugins

```c
int count = plugin_manager_discover(mgr);
```

### Loading a Plugin

```c
plugin_handle_t *handle = plugin_manager_load(mgr, "my_plugin");
```

### Initializing Plugins

```c
nlmon_plugin_context_t ctx = { /* ... */ };
int initialized = plugin_manager_init_all(mgr, &ctx);
```

### Routing Events

```c
struct nlmon_event event = { /* ... */ };
int processed = plugin_manager_route_event(mgr, &event);
```

### Invoking Commands

```c
char response[256];
int ret = plugin_manager_invoke_command(mgr, "my_plugin", "mycommand", 
                                        "args", response, sizeof(response));
```

### Listing Plugins

```c
plugin_info_t *list;
size_t count;
plugin_manager_list(mgr, &list, &count);
/* Use list */
free(list);
```

### Cleanup

```c
plugin_manager_cleanup_all(mgr);
plugin_manager_destroy(mgr);
```

## Testing

A test suite is provided in `test_plugin_system.c`:

```bash
# Build test
gcc -o test_plugin_system test_plugin_system.c \
    src/plugins/plugin_loader.c \
    src/plugins/plugin_lifecycle.c \
    src/plugins/plugin_router.c \
    -Iinclude -ldl

# Run test
./test_plugin_system
```

## Example Plugins

### Event Logger Plugin

See `src/plugins/example_plugin.c` for a complete example that:
- Logs all events to a file
- Provides statistics command
- Handles configuration reload
- Demonstrates all plugin features

### Building Example Plugin

```bash
gcc -shared -fPIC -o example_plugin.so src/plugins/example_plugin.c -Iinclude
sudo cp example_plugin.so /usr/lib/nlmon/plugins/
```

## Best Practices

1. **Keep init() fast** - Avoid long-running operations
2. **Handle errors gracefully** - Always check return values
3. **Clean up resources** - Free all allocated memory in cleanup()
4. **Use logging** - Log important events and errors
5. **Validate input** - Check event data before processing
6. **Be thread-safe** - Use proper synchronization if needed
7. **Document commands** - Provide clear help text
8. **Version your plugin** - Use semantic versioning
9. **Test thoroughly** - Test with various event types
10. **Handle config reload** - Update settings in on_config_reload()

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

See `PLUGIN_API.md` for complete API documentation.

## Future Enhancements

- Plugin hot-reload without restart
- Plugin sandboxing for security
- Plugin marketplace/repository
- Plugin configuration UI
- Plugin performance profiling
- Plugin dependency version constraints

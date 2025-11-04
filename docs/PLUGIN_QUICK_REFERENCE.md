# nlmon Plugin API Quick Reference

## Plugin Structure

```c
#include "plugin_api.h"

static nlmon_plugin_t my_plugin = {
    .name = "my_plugin",
    .version = "1.0.0",
    .description = "Description",
    .api_version = NLMON_PLUGIN_API_VERSION,
    .callbacks = { /* ... */ },
    .event_filter = NULL,
    .flags = NLMON_PLUGIN_FLAG_NONE,
    .dependencies = NULL,
};

NLMON_PLUGIN_DEFINE(my_plugin);
```

## Callbacks

### init()
```c
static int my_init(nlmon_plugin_context_t *ctx) {
    // Initialize plugin
    return 0;  // 0=success, -1=error
}
```

### cleanup()
```c
static void my_cleanup(void) {
    // Cleanup resources
}
```

### on_event()
```c
static int my_on_event(struct nlmon_event *event) {
    // Process event
    return 0;  // 0=success, -1=error, 1=filter
}
```

### on_command()
```c
static int my_on_command(const char *cmd, const char *args,
                         char *response, size_t resp_len) {
    snprintf(response, resp_len, "Result");
    return 0;  // 0=success, -1=error
}
```

### on_config_reload()
```c
static int my_on_config_reload(nlmon_plugin_context_t *ctx) {
    // Reload configuration
    return 0;  // 0=success, -1=error
}
```

## Context API

### Logging
```c
ctx->log(NLMON_LOG_DEBUG, "Debug: %d", value);
ctx->log(NLMON_LOG_INFO, "Info: %s", msg);
ctx->log(NLMON_LOG_WARN, "Warning: %s", msg);
ctx->log(NLMON_LOG_ERROR, "Error: %s", error);
```

### Register Command
```c
ctx->register_command("mycommand", handler, "Help text");
```

### Get Configuration
```c
const char *value = ctx->get_config("my_plugin.setting");
```

### Emit Event
```c
ctx->emit_event(event);
```

### Store Plugin Data
```c
ctx->plugin_data = my_state;
struct my_state *state = ctx->plugin_data;
```

## Plugin Flags

```c
NLMON_PLUGIN_FLAG_NONE          // Default
NLMON_PLUGIN_FLAG_PROCESS_ALL   // Receive all events
NLMON_PLUGIN_FLAG_ASYNC         // Async processing
NLMON_PLUGIN_FLAG_CRITICAL      // Failure stops nlmon
```

## Event Filter

```c
static int my_filter(struct nlmon_event *event) {
    return event->message_type == RTM_NEWLINK;  // 1=process, 0=skip
}

static nlmon_plugin_t my_plugin = {
    .event_filter = my_filter,
};
```

## Dependencies

```c
static const char *deps[] = {"base_plugin", NULL};

static nlmon_plugin_t my_plugin = {
    .dependencies = deps,
};
```

## Build Commands

```bash
# Basic build
gcc -shared -fPIC -o plugin.so plugin.c -I/usr/include/nlmon

# With debug
gcc -g -shared -fPIC -o plugin.so plugin.c -I/usr/include/nlmon

# With optimization
gcc -O2 -shared -fPIC -o plugin.so plugin.c -I/usr/include/nlmon

# With library
gcc -shared -fPIC -o plugin.so plugin.c -I/usr/include/nlmon -lcurl
```

## Installation

```bash
# Install plugin
sudo cp plugin.so /usr/lib/nlmon/plugins/

# Enable in config
echo "plugins:
  enabled:
    - my_plugin" | sudo tee -a /etc/nlmon/nlmon.yaml
```

## Configuration

```yaml
plugins:
  enabled:
    - my_plugin

my_plugin:
  setting1: value1
  setting2: value2
```

## Common Patterns

### Plugin State
```c
static struct {
    nlmon_plugin_context_t *ctx;
    uint64_t counter;
} state;

static int my_init(nlmon_plugin_context_t *ctx) {
    memset(&state, 0, sizeof(state));
    state.ctx = ctx;
    return 0;
}
```

### Error Handling
```c
if (!event) {
    ctx->log(NLMON_LOG_ERROR, "Null event");
    return -1;
}
```

### Resource Cleanup
```c
static FILE *fp = NULL;

static int my_init(nlmon_plugin_context_t *ctx) {
    fp = fopen("/tmp/log", "a");
    return fp ? 0 : -1;
}

static void my_cleanup(void) {
    if (fp) {
        fclose(fp);
        fp = NULL;
    }
}
```

## Debugging

```bash
# Check symbols
nm -D plugin.so | grep nlmon_plugin_register

# Check dependencies
ldd plugin.so

# Run with valgrind
sudo valgrind --leak-check=full nlmon

# Run with gdb
sudo gdb nlmon
(gdb) run
```

## Testing

```bash
# Start nlmon
sudo nlmon

# Check logs
tail -f /var/log/nlmon/nlmon.log

# Test command (in CLI)
:mycommand
```

## Common Issues

### Plugin not loading
- Check file in `/usr/lib/nlmon/plugins/`
- Verify permissions: `chmod 644 plugin.so`
- Check API version matches
- Review nlmon logs

### Plugin crashes
- Add NULL checks
- Validate buffer sizes
- Use valgrind
- Check return values

### Performance issues
- Minimize work in `on_event()`
- Use event filtering
- Consider async flag
- Profile with gprof/perf

## Resources

- [Full Development Guide](PLUGIN_DEVELOPMENT_GUIDE.md)
- [API Documentation](../src/plugins/PLUGIN_API.md)
- [Example Plugins](../src/plugins/examples/)
- [Plugin Template](../src/plugins/examples/plugin_template.c)

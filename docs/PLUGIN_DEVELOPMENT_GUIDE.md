# nlmon Plugin Development Guide

## Table of Contents

1. [Introduction](#introduction)
2. [Getting Started](#getting-started)
3. [Plugin Architecture](#plugin-architecture)
4. [Creating Your First Plugin](#creating-your-first-plugin)
5. [Plugin API Reference](#plugin-api-reference)
6. [Advanced Topics](#advanced-topics)
7. [Best Practices](#best-practices)
8. [Testing and Debugging](#testing-and-debugging)
9. [Deployment](#deployment)
10. [Examples and Templates](#examples-and-templates)

## Introduction

### What is a Plugin?

An nlmon plugin is a dynamically loaded shared library that extends nlmon's functionality without modifying the core application. Plugins can:

- **Process Events**: React to network events in real-time
- **Export Data**: Send events to external systems or files
- **Add Commands**: Register custom CLI commands
- **Integrate Systems**: Connect nlmon with external services
- **Transform Data**: Enrich or filter events

### Why Create a Plugin?

- **Extensibility**: Add custom functionality without forking nlmon
- **Modularity**: Keep custom code separate from core
- **Reusability**: Share plugins across deployments
- **Maintainability**: Update plugins independently
- **Performance**: Plugins run in the same process for efficiency

### Prerequisites

Before developing plugins, you should have:

- C programming experience
- Understanding of shared libraries
- Familiarity with Linux system programming
- Basic knowledge of nlmon architecture
- GCC compiler and development tools

## Getting Started

### Development Environment Setup

#### Install Dependencies

```bash
# Ubuntu/Debian
sudo apt-get install build-essential gcc make

# RHEL/CentOS
sudo yum groupinstall "Development Tools"
```

#### Install nlmon Headers

```bash
# If nlmon is installed from package
sudo apt-get install nlmon-dev

# Or copy headers manually
sudo cp include/plugin_api.h /usr/include/nlmon/
```

#### Create Plugin Directory

```bash
mkdir -p ~/nlmon-plugins/my_plugin
cd ~/nlmon-plugins/my_plugin
```

### Quick Start: Hello World Plugin

Create `hello_plugin.c`:

```c
#include "plugin_api.h"
#include <stdio.h>

static int hello_init(nlmon_plugin_context_t *ctx) {
    ctx->log(NLMON_LOG_INFO, "Hello from my plugin!");
    return 0;
}

static void hello_cleanup(void) {
    printf("Goodbye from my plugin!\n");
}

static nlmon_plugin_t hello_plugin = {
    .name = "hello",
    .version = "1.0.0",
    .description = "Hello World plugin",
    .api_version = NLMON_PLUGIN_API_VERSION,
    .callbacks = {
        .init = hello_init,
        .cleanup = hello_cleanup,
    },
};

NLMON_PLUGIN_DEFINE(hello_plugin);
```

Build and test:

```bash
gcc -shared -fPIC -o hello.so hello_plugin.c -I/usr/include/nlmon
sudo cp hello.so /usr/lib/nlmon/plugins/
```

Enable in `/etc/nlmon/nlmon.yaml`:

```yaml
plugins:
  enabled:
    - hello
```

Run nlmon and verify the plugin loads.

## Plugin Architecture

### Plugin Lifecycle

```
┌─────────────────┐
│   Discovery     │  Plugin manager scans directory
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│   Load (.so)    │  dlopen() loads shared library
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│   Register      │  Call nlmon_plugin_register()
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│   Verify        │  Check API version & dependencies
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│   Initialize    │  Call init() callback
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│   Active        │  Process events, handle commands
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│   Cleanup       │  Call cleanup() callback
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│   Unload        │  dlclose() unloads library
└─────────────────┘
```

### Plugin Components

#### 1. Plugin Descriptor

The main structure defining your plugin:

```c
typedef struct nlmon_plugin {
    char name[64];
    char version[32];
    char description[256];
    int api_version;
    nlmon_plugin_callbacks_t callbacks;
    nlmon_event_filter_t event_filter;
    uint32_t flags;
    const char **dependencies;
} nlmon_plugin_t;
```

#### 2. Plugin Context

Provides API for interacting with nlmon:

```c
typedef struct nlmon_plugin_context {
    const void *config;
    void (*log)(nlmon_log_level_t level, const char *fmt, ...);
    int (*register_command)(const char *name, nlmon_command_handler_t handler, const char *help);
    int (*emit_event)(struct nlmon_event *event);
    const char *(*get_config)(const char *key);
    void *plugin_data;
} nlmon_plugin_context_t;
```

#### 3. Callbacks

Functions called by nlmon at specific points:

- `init()` - Plugin initialization
- `cleanup()` - Plugin cleanup
- `on_event()` - Event processing
- `on_command()` - Command handling
- `on_config_reload()` - Configuration reload

## Creating Your First Plugin

### Step 1: Define Plugin Metadata

```c
#include "plugin_api.h"

static nlmon_plugin_t my_plugin = {
    .name = "my_plugin",
    .version = "1.0.0",
    .description = "My custom nlmon plugin",
    .api_version = NLMON_PLUGIN_API_VERSION,
    .flags = NLMON_PLUGIN_FLAG_NONE,
    .dependencies = NULL,
};

NLMON_PLUGIN_DEFINE(my_plugin);
```

### Step 2: Implement Initialization

```c
static nlmon_plugin_context_t *g_ctx = NULL;

static int my_plugin_init(nlmon_plugin_context_t *ctx) {
    g_ctx = ctx;
    
    ctx->log(NLMON_LOG_INFO, "My plugin initialized");
    
    // Read configuration
    const char *setting = ctx->get_config("my_plugin.setting");
    if (setting) {
        ctx->log(NLMON_LOG_INFO, "Setting: %s", setting);
    }
    
    return 0;  // Success
}

static nlmon_plugin_t my_plugin = {
    // ...
    .callbacks = {
        .init = my_plugin_init,
    },
};
```

### Step 3: Implement Cleanup

```c
static void my_plugin_cleanup(void) {
    if (g_ctx) {
        g_ctx->log(NLMON_LOG_INFO, "My plugin cleaned up");
    }
    
    // Free resources
    // Close files
    // Disconnect from services
}

static nlmon_plugin_t my_plugin = {
    // ...
    .callbacks = {
        .init = my_plugin_init,
        .cleanup = my_plugin_cleanup,
    },
};
```

### Step 4: Add Event Processing

```c
static uint64_t event_count = 0;

static int my_plugin_on_event(struct nlmon_event *event) {
    if (!event) {
        return -1;
    }
    
    event_count++;
    
    // Process event
    g_ctx->log(NLMON_LOG_DEBUG, "Processing event #%lu", event_count);
    
    return 0;  // Success
}

static nlmon_plugin_t my_plugin = {
    // ...
    .callbacks = {
        .init = my_plugin_init,
        .cleanup = my_plugin_cleanup,
        .on_event = my_plugin_on_event,
    },
};
```

### Step 5: Register Commands

```c
static int cmd_stats(const char *args, char *response, size_t resp_len) {
    snprintf(response, resp_len, 
             "My Plugin Statistics:\n"
             "  Events processed: %lu\n",
             event_count);
    return 0;
}

static int my_plugin_init(nlmon_plugin_context_t *ctx) {
    g_ctx = ctx;
    
    // Register command
    ctx->register_command("mystats", cmd_stats, "Show my plugin statistics");
    
    ctx->log(NLMON_LOG_INFO, "My plugin initialized");
    return 0;
}
```

### Step 6: Build and Install

```bash
# Build
gcc -shared -fPIC -o my_plugin.so my_plugin.c -I/usr/include/nlmon

# Install
sudo cp my_plugin.so /usr/lib/nlmon/plugins/

# Enable in config
echo "plugins:
  enabled:
    - my_plugin" | sudo tee -a /etc/nlmon/nlmon.yaml
```

## Plugin API Reference

### Context Functions

#### Logging

```c
void (*log)(nlmon_log_level_t level, const char *fmt, ...);
```

Log levels:
- `NLMON_LOG_DEBUG` - Debug information
- `NLMON_LOG_INFO` - Informational messages
- `NLMON_LOG_WARN` - Warnings
- `NLMON_LOG_ERROR` - Errors

Example:
```c
ctx->log(NLMON_LOG_INFO, "Processing event: %s", event->interface);
ctx->log(NLMON_LOG_ERROR, "Failed to open file: %s", strerror(errno));
```

#### Command Registration

```c
int (*register_command)(const char *name, nlmon_command_handler_t handler, const char *help);
```

Example:
```c
static int my_command(const char *args, char *response, size_t resp_len) {
    snprintf(response, resp_len, "Command executed");
    return 0;
}

ctx->register_command("mycommand", my_command, "Execute my command");
```

#### Configuration Access

```c
const char *(*get_config)(const char *key);
```

Example:
```c
const char *host = ctx->get_config("my_plugin.server_host");
const char *port = ctx->get_config("my_plugin.server_port");
```

#### Event Emission

```c
int (*emit_event)(struct nlmon_event *event);
```

Example:
```c
struct nlmon_event *custom_event = create_custom_event();
ctx->emit_event(custom_event);
```

#### Plugin Data Storage

```c
void *plugin_data;
```

Example:
```c
struct my_plugin_state {
    FILE *logfile;
    uint64_t counter;
};

static int my_init(nlmon_plugin_context_t *ctx) {
    struct my_plugin_state *state = malloc(sizeof(*state));
    ctx->plugin_data = state;
    return 0;
}

static int my_on_event(struct nlmon_event *event) {
    struct my_plugin_state *state = g_ctx->plugin_data;
    state->counter++;
    return 0;
}
```

### Callback Functions

#### init()

```c
int (*init)(nlmon_plugin_context_t *ctx);
```

Called once when plugin is loaded. Return 0 for success, -1 for error.

**Responsibilities:**
- Initialize plugin state
- Allocate resources
- Read configuration
- Register commands
- Set up connections

#### cleanup()

```c
void (*cleanup)(void);
```

Called once when plugin is unloaded.

**Responsibilities:**
- Free allocated memory
- Close files
- Disconnect from services
- Save state if needed

#### on_event()

```c
int (*on_event)(struct nlmon_event *event);
```

Called for each network event (if plugin subscribes).

**Return values:**
- `0` - Success
- `-1` - Error (logged, error counter incremented)
- `1` - Filter event (don't pass to other plugins)

#### on_command()

```c
int (*on_command)(const char *cmd, const char *args, char *response, size_t resp_len);
```

Called when plugin command is invoked.

**Parameters:**
- `cmd` - Command name
- `args` - Command arguments
- `response` - Buffer for response
- `resp_len` - Response buffer size

**Return:** 0 for success, -1 for error

#### on_config_reload()

```c
int (*on_config_reload)(nlmon_plugin_context_t *ctx);
```

Called when configuration is reloaded.

**Responsibilities:**
- Re-read configuration
- Update plugin behavior
- Reconnect if needed

### Plugin Flags

```c
#define NLMON_PLUGIN_FLAG_NONE          0x00
#define NLMON_PLUGIN_FLAG_PROCESS_ALL   0x01
#define NLMON_PLUGIN_FLAG_ASYNC         0x02
#define NLMON_PLUGIN_FLAG_CRITICAL      0x04
```

- **NONE**: Default behavior
- **PROCESS_ALL**: Receive all events (ignore filter)
- **ASYNC**: Asynchronous event processing
- **CRITICAL**: Plugin failure stops nlmon

### Event Filtering

```c
typedef int (*nlmon_event_filter_t)(struct nlmon_event *event);
```

Return 1 to process event, 0 to skip.

Example:
```c
static int my_filter(struct nlmon_event *event) {
    // Only process link events
    return event->message_type == RTM_NEWLINK;
}

static nlmon_plugin_t my_plugin = {
    // ...
    .event_filter = my_filter,
};
```

## Advanced Topics

### Managing Plugin State

Use a static structure to maintain state:

```c
static struct {
    FILE *logfile;
    uint64_t event_count;
    char config_value[256];
    nlmon_plugin_context_t *ctx;
} plugin_state;

static int my_init(nlmon_plugin_context_t *ctx) {
    memset(&plugin_state, 0, sizeof(plugin_state));
    plugin_state.ctx = ctx;
    return 0;
}
```

### Thread Safety

If your plugin uses threads:

```c
#include <pthread.h>

static pthread_mutex_t state_mutex = PTHREAD_MUTEX_INITIALIZER;

static int my_on_event(struct nlmon_event *event) {
    pthread_mutex_lock(&state_mutex);
    // Access shared state
    pthread_mutex_unlock(&state_mutex);
    return 0;
}
```

### Asynchronous Processing

For long-running operations:

```c
static nlmon_plugin_t my_plugin = {
    // ...
    .flags = NLMON_PLUGIN_FLAG_ASYNC,
};
```

Events are queued and processed asynchronously.

### Plugin Dependencies

Declare dependencies on other plugins:

```c
static const char *deps[] = {
    "base_plugin",
    "helper_plugin",
    NULL
};

static nlmon_plugin_t my_plugin = {
    // ...
    .dependencies = deps,
};
```

Dependencies are loaded first.

### External Library Integration

Link with external libraries:

```c
// my_plugin.c
#include "plugin_api.h"
#include <curl/curl.h>

static int my_init(nlmon_plugin_context_t *ctx) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    return 0;
}

static void my_cleanup(void) {
    curl_global_cleanup();
}
```

Build with library:
```bash
gcc -shared -fPIC -o my_plugin.so my_plugin.c -lcurl
```

### Configuration Schema

Define configuration in YAML:

```yaml
my_plugin:
  enabled: true
  server:
    host: localhost
    port: 8080
    timeout: 30
  options:
    retry_count: 3
    buffer_size: 4096
```

Access in plugin:
```c
const char *host = ctx->get_config("my_plugin.server.host");
const char *port = ctx->get_config("my_plugin.server.port");
```

## Best Practices

### 1. Error Handling

Always check return values:

```c
static int my_init(nlmon_plugin_context_t *ctx) {
    FILE *fp = fopen("/path/to/file", "r");
    if (!fp) {
        ctx->log(NLMON_LOG_ERROR, "Failed to open file: %s", strerror(errno));
        return -1;
    }
    
    // Use file
    fclose(fp);
    return 0;
}
```

### 2. Resource Management

Clean up all resources:

```c
static FILE *logfile = NULL;

static int my_init(nlmon_plugin_context_t *ctx) {
    logfile = fopen("/tmp/plugin.log", "a");
    if (!logfile) {
        return -1;
    }
    return 0;
}

static void my_cleanup(void) {
    if (logfile) {
        fclose(logfile);
        logfile = NULL;
    }
}
```

### 3. Defensive Programming

Validate inputs:

```c
static int my_on_event(struct nlmon_event *event) {
    if (!event) {
        g_ctx->log(NLMON_LOG_ERROR, "Null event received");
        return -1;
    }
    
    if (!event->interface[0]) {
        g_ctx->log(NLMON_LOG_WARN, "Event has no interface");
        return 0;
    }
    
    // Process event
    return 0;
}
```

### 4. Performance Optimization

Minimize work in on_event():

```c
// BAD: Expensive operation in event handler
static int my_on_event(struct nlmon_event *event) {
    expensive_database_query();  // Blocks event processing
    return 0;
}

// GOOD: Queue for async processing
static int my_on_event(struct nlmon_event *event) {
    queue_event_for_processing(event);  // Fast
    return 0;
}
```

### 5. Logging Best Practices

Use appropriate log levels:

```c
ctx->log(NLMON_LOG_DEBUG, "Processing event %lu", event->sequence);
ctx->log(NLMON_LOG_INFO, "Plugin initialized successfully");
ctx->log(NLMON_LOG_WARN, "Configuration value missing, using default");
ctx->log(NLMON_LOG_ERROR, "Failed to connect to server: %s", error);
```

### 6. Configuration Validation

Validate configuration values:

```c
static int my_init(nlmon_plugin_context_t *ctx) {
    const char *port_str = ctx->get_config("my_plugin.port");
    if (!port_str) {
        ctx->log(NLMON_LOG_ERROR, "Missing required config: my_plugin.port");
        return -1;
    }
    
    int port = atoi(port_str);
    if (port < 1 || port > 65535) {
        ctx->log(NLMON_LOG_ERROR, "Invalid port: %d", port);
        return -1;
    }
    
    return 0;
}
```

### 7. Memory Safety

Avoid buffer overflows:

```c
// BAD
char buf[64];
strcpy(buf, user_input);  // Unsafe

// GOOD
char buf[64];
strncpy(buf, user_input, sizeof(buf) - 1);
buf[sizeof(buf) - 1] = '\0';

// BETTER
snprintf(buf, sizeof(buf), "%s", user_input);
```

### 8. Version Compatibility

Check API version:

```c
static nlmon_plugin_t my_plugin = {
    .api_version = NLMON_PLUGIN_API_VERSION,
    // ...
};
```

### 9. Documentation

Document your plugin:

```c
/*
 * My Plugin
 * 
 * Description: Does something useful
 * 
 * Configuration:
 *   my_plugin.setting1: Description
 *   my_plugin.setting2: Description
 * 
 * Commands:
 *   mycommand - Description
 * 
 * Dependencies:
 *   - other_plugin
 */
```

### 10. Testing

Test thoroughly:

- Test with various event types
- Test error conditions
- Test configuration reload
- Test with missing configuration
- Test resource cleanup
- Test under load

## Testing and Debugging

### Unit Testing

Create test harness:

```c
// test_my_plugin.c
#include "plugin_api.h"
#include <assert.h>

extern nlmon_plugin_t *nlmon_plugin_register(void);

int main() {
    nlmon_plugin_t *plugin = nlmon_plugin_register();
    assert(plugin != NULL);
    assert(strcmp(plugin->name, "my_plugin") == 0);
    
    // Test initialization
    nlmon_plugin_context_t ctx = { /* ... */ };
    assert(plugin->callbacks.init(&ctx) == 0);
    
    // Test event processing
    struct nlmon_event event = { /* ... */ };
    assert(plugin->callbacks.on_event(&event) == 0);
    
    // Test cleanup
    plugin->callbacks.cleanup();
    
    printf("All tests passed!\n");
    return 0;
}
```

### Debugging with GDB

```bash
# Build with debug symbols
gcc -g -shared -fPIC -o my_plugin.so my_plugin.c

# Run nlmon under gdb
sudo gdb nlmon
(gdb) run
# Trigger plugin code
(gdb) bt  # Backtrace on crash
```

### Memory Debugging with Valgrind

```bash
# Build with debug symbols
gcc -g -shared -fPIC -o my_plugin.so my_plugin.c

# Run under valgrind
sudo valgrind --leak-check=full nlmon
```

### Logging for Debugging

Add verbose logging:

```c
#ifdef DEBUG
#define DEBUG_LOG(ctx, fmt, ...) \
    ctx->log(NLMON_LOG_DEBUG, "[%s:%d] " fmt, __FILE__, __LINE__, ##__VA_ARGS__)
#else
#define DEBUG_LOG(ctx, fmt, ...)
#endif

static int my_on_event(struct nlmon_event *event) {
    DEBUG_LOG(g_ctx, "Event received: type=%d", event->message_type);
    // ...
}
```

Build with debug:
```bash
gcc -DDEBUG -g -shared -fPIC -o my_plugin.so my_plugin.c
```

### Common Issues

#### Plugin Not Loading

**Symptoms:** Plugin doesn't appear in logs

**Causes:**
- Wrong directory
- File permissions
- API version mismatch
- Missing dependencies

**Solutions:**
```bash
# Check file exists
ls -l /usr/lib/nlmon/plugins/my_plugin.so

# Check permissions
sudo chmod 644 /usr/lib/nlmon/plugins/my_plugin.so

# Check symbols
nm -D my_plugin.so | grep nlmon_plugin_register

# Check dependencies
ldd my_plugin.so
```

#### Plugin Crashes

**Symptoms:** nlmon crashes or plugin disabled

**Causes:**
- NULL pointer dereference
- Buffer overflow
- Memory corruption
- Unhandled exception

**Solutions:**
- Use valgrind
- Add NULL checks
- Validate buffer sizes
- Use defensive programming

#### Performance Issues

**Symptoms:** High CPU usage, slow event processing

**Causes:**
- Expensive operations in on_event()
- Blocking I/O
- Memory leaks
- Inefficient algorithms

**Solutions:**
- Profile with gprof/perf
- Use async processing
- Optimize hot paths
- Add event filtering

## Deployment

### Packaging

Create a package structure:

```
my-plugin/
├── my_plugin.so
├── README.md
├── LICENSE
├── config.yaml.example
└── install.sh
```

### Installation Script

```bash
#!/bin/bash
# install.sh

PLUGIN_DIR=/usr/lib/nlmon/plugins
CONFIG_DIR=/etc/nlmon/plugins.d

# Install plugin
sudo cp my_plugin.so $PLUGIN_DIR/
sudo chmod 644 $PLUGIN_DIR/my_plugin.so

# Install config
sudo mkdir -p $CONFIG_DIR
sudo cp config.yaml.example $CONFIG_DIR/my_plugin.yaml

echo "Plugin installed successfully"
echo "Edit $CONFIG_DIR/my_plugin.yaml to configure"
```

### Distribution

#### Source Distribution

```bash
tar czf my-plugin-1.0.0.tar.gz my-plugin/
```

#### Binary Distribution

```bash
# Build for distribution
gcc -O2 -shared -fPIC -o my_plugin.so my_plugin.c

# Create package
tar czf my-plugin-1.0.0-linux-x86_64.tar.gz \
    my_plugin.so README.md LICENSE install.sh
```

#### Debian Package

Create `debian/control`:

```
Package: nlmon-plugin-myplugin
Version: 1.0.0
Architecture: amd64
Depends: nlmon (>= 2.0.0)
Description: My nlmon plugin
```

Build:
```bash
dpkg-deb --build my-plugin
```

## Examples and Templates

### Plugin Template

```c
/*
 * Plugin Template
 * 
 * Copy this template to create a new plugin.
 */

#include "plugin_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Plugin state */
static struct {
    nlmon_plugin_context_t *ctx;
    uint64_t event_count;
    /* Add your state variables here */
} state;

/* Initialize plugin */
static int template_init(nlmon_plugin_context_t *ctx) {
    memset(&state, 0, sizeof(state));
    state.ctx = ctx;
    
    ctx->log(NLMON_LOG_INFO, "Template plugin initialized");
    
    /* TODO: Initialize your plugin */
    
    return 0;
}

/* Cleanup plugin */
static void template_cleanup(void) {
    if (state.ctx) {
        state.ctx->log(NLMON_LOG_INFO, "Template plugin cleaned up");
    }
    
    /* TODO: Cleanup your resources */
}

/* Process event */
static int template_on_event(struct nlmon_event *event) {
    if (!event) return -1;
    
    state.event_count++;
    
    /* TODO: Process event */
    
    return 0;
}

/* Handle command */
static int template_on_command(const char *cmd, const char *args,
                               char *response, size_t resp_len) {
    if (!cmd || !response) return -1;
    
    /* TODO: Handle commands */
    
    snprintf(response, resp_len, "Unknown command: %s", cmd);
    return -1;
}

/* Handle config reload */
static int template_on_config_reload(nlmon_plugin_context_t *ctx) {
    ctx->log(NLMON_LOG_INFO, "Template plugin: config reloaded");
    
    /* TODO: Reload configuration */
    
    return 0;
}

/* Plugin descriptor */
static nlmon_plugin_t template_plugin = {
    .name = "template",
    .version = "1.0.0",
    .description = "Plugin template",
    .api_version = NLMON_PLUGIN_API_VERSION,
    .callbacks = {
        .init = template_init,
        .cleanup = template_cleanup,
        .on_event = template_on_event,
        .on_command = template_on_command,
        .on_config_reload = template_on_config_reload,
    },
    .event_filter = NULL,
    .flags = NLMON_PLUGIN_FLAG_NONE,
    .dependencies = NULL,
};

/* Plugin registration */
NLMON_PLUGIN_DEFINE(template_plugin);
```

### Makefile Template

```makefile
# Plugin Makefile Template

CC = gcc
CFLAGS = -Wall -Wextra -fPIC -O2
INCLUDES = -I/usr/include/nlmon
LDFLAGS = -shared

PLUGIN_NAME = my_plugin
PLUGIN_SO = $(PLUGIN_NAME).so
PLUGIN_SRC = $(PLUGIN_NAME).c

INSTALL_DIR = /usr/lib/nlmon/plugins

.PHONY: all clean install uninstall

all: $(PLUGIN_SO)

$(PLUGIN_SO): $(PLUGIN_SRC)
	$(CC) $(CFLAGS) $(INCLUDES) $(LDFLAGS) -o $@ $<

clean:
	rm -f $(PLUGIN_SO)

install: $(PLUGIN_SO)
	sudo install -m 644 $(PLUGIN_SO) $(INSTALL_DIR)/

uninstall:
	sudo rm -f $(INSTALL_DIR)/$(PLUGIN_SO)

test: $(PLUGIN_SO)
	@echo "Testing plugin..."
	nm -D $(PLUGIN_SO) | grep nlmon_plugin_register
	@echo "Plugin symbols OK"
```

## Additional Resources

- [Plugin API Documentation](../src/plugins/PLUGIN_API.md)
- [Plugin System README](../src/plugins/README.md)
- [Example Plugins](../src/plugins/examples/)
- [nlmon Configuration Guide](configuration.md)

## Getting Help

- GitHub Issues: https://github.com/nlmon/nlmon/issues
- Documentation: https://nlmon.readthedocs.io
- Mailing List: nlmon-dev@lists.example.com

## Contributing

We welcome plugin contributions! Please:

1. Follow this development guide
2. Include comprehensive documentation
3. Add example configuration
4. Test thoroughly
5. Submit pull request

## License

This guide is part of the nlmon project and is licensed under the same terms.

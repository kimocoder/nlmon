# nlmon Architecture Documentation

## Table of Contents

1. [Overview](#overview)
2. [System Architecture](#system-architecture)
3. [Core Components](#core-components)
4. [Data Flow](#data-flow)
5. [Threading Model](#threading-model)
6. [Memory Management](#memory-management)
7. [Plugin System](#plugin-system)
8. [Storage Architecture](#storage-architecture)
9. [Export Layer](#export-layer)
10. [Web Interface](#web-interface)
11. [Security Architecture](#security-architecture)
12. [Performance Considerations](#performance-considerations)
13. [Extension Points](#extension-points)

## Overview

nlmon (Network Link Monitor) is a high-performance network event monitoring system for Linux that captures and analyzes kernel netlink events in real-time. The architecture is designed for:

- **High Throughput**: Process 10,000+ events per second
- **Low Latency**: Minimal overhead on event processing
- **Extensibility**: Plugin-based architecture for custom functionality
- **Reliability**: Robust error handling and resource management
- **Scalability**: Efficient memory and CPU usage

### Design Principles

1. **Modularity**: Clear separation of concerns with well-defined interfaces
2. **Performance**: Lock-free data structures and zero-copy where possible
3. **Extensibility**: Plugin system for custom functionality
4. **Reliability**: Comprehensive error handling and resource cleanup
5. **Maintainability**: Clean code structure with extensive documentation

## System Architecture

### High-Level Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                         Linux Kernel                             │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐          │
│  │   Network    │  │   Routing    │  │   Neighbor   │          │
│  │   Stack      │  │   Subsystem  │  │   Discovery  │          │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘          │
│         │                  │                  │                   │
│         └──────────────────┴──────────────────┘                  │
│                            │                                      │
│                    ┌───────▼────────┐                           │
│                    │ Netlink Socket │                           │
│                    └───────┬────────┘                           │
└────────────────────────────┼──────────────────────────────────┘
                             │
                    ┌────────▼────────┐
                    │  Netlink        │
                    │  Listener       │
                    └────────┬────────┘
                             │
┌─────────────────────────────────────────────────────────────────┐
│                         nlmon Core                               │
│                                                                   │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐          │
│  │   Event      │  │   Filter     │  │  Correlation │          │
│  │  Processor   │  │   Manager    │  │   Engine     │          │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘          │
│         │                  │                  │                   │
│         └──────────────────┴──────────────────┘                  │
│                            │                                      │
│                    ┌───────▼────────┐                           │
│                    │  Event Manager │                           │
│                    └───────┬────────┘                           │
└────────────────────────────┼──────────────────────────────────┘
                             │
        ┌────────────────────┼────────────────────┐
        │                    │                    │
┌───────▼────────┐  ┌────────▼────────┐  ┌───────▼────────┐
│  Storage       │  │   Export        │  │   Interface    │
│  Layer         │  │   Layer         │  │   Layer        │
│                │  │                 │  │                │
│ • Memory Buf   │  │ • PCAP          │  │ • CLI          │
│ • SQLite DB    │  │ • JSON          │  │ • Web UI       │
│ • Audit Log    │  │ • Prometheus    │  │ • REST API     │
└────────────────┘  └─────────────────┘  └────────────────┘
```

### Layer Architecture

nlmon follows a layered architecture with clear boundaries:

**Layer 1: Kernel Interface**
- Netlink socket management
- Protocol family support (ROUTE, GENERIC, SOCK_DIAG)
- Raw message reception

**Layer 2: Core Processing**
- Event parsing and normalization
- Filter evaluation
- Rate limiting
- Event correlation
- Plugin invocation

**Layer 3: Storage & Export**
- Memory buffering
- Database persistence
- Export format conversion
- Metrics collection

**Layer 4: User Interface**
- CLI (ncurses-based)
- Web dashboard
- REST API
- WebSocket streaming

## Core Components

### 1. Netlink Listener

**Location**: `src/core/netlink_multi_protocol.c`

**Purpose**: Capture netlink messages from kernel

**Key Features**:
- Multi-protocol support (ROUTE, GENERIC, SOCK_DIAG)
- Non-blocking I/O with epoll
- Message buffering and parsing
- Namespace detection

**Data Structures**:
```c
struct netlink_listener {
    int sock_fd;
    int protocol;
    struct sockaddr_nl addr;
    uint32_t groups;
    struct epoll_event *events;
};
```

**Message Flow**:
1. Kernel generates netlink message
2. Message arrives at netlink socket
3. epoll notifies listener
4. Message read and parsed
5. Event created and queued

### 2. Event Processor

**Location**: `src/core/event_processor.c`

**Purpose**: Parse and normalize netlink messages into events

**Key Features**:
- Message type dispatch
- Attribute parsing
- Event normalization
- Interface name resolution

**Event Structure**:
```c
struct nlmon_event {
    uint64_t timestamp;
    uint64_t sequence;
    uint16_t message_type;
    char interface[IFNAMSIZ];
    char namespace[256];
    union {
        struct link_event link;
        struct addr_event addr;
        struct route_event route;
        struct neigh_event neigh;
    } data;
    char *correlation_id;
};
```

### 3. Ring Buffer

**Location**: `src/core/ring_buffer.c`

**Purpose**: Lock-free event queue for high throughput

**Key Features**:
- Lock-free single-producer, single-consumer
- Power-of-2 size for efficient modulo
- Memory barriers for correctness
- Overflow detection

**Implementation**:
```c
struct ring_buffer {
    void **buffer;
    size_t size;
    atomic_size_t head;
    atomic_size_t tail;
    atomic_uint_fast64_t overflow_count;
};
```

**Operations**:
- `ring_buffer_push()`: Add event (producer)
- `ring_buffer_pop()`: Remove event (consumer)
- `ring_buffer_size()`: Current occupancy

### 4. Thread Pool

**Location**: `src/core/thread_pool.c`

**Purpose**: Parallel event processing

**Key Features**:
- Configurable worker count
- Work queue with priority
- Graceful shutdown
- Load balancing

**Architecture**:
```
┌─────────────┐
│   Master    │
│   Thread    │
└──────┬──────┘
       │
       ├──────┐
       │      │
┌──────▼──┐ ┌─▼────────┐
│ Worker  │ │ Worker   │
│ Thread  │ │ Thread   │
│    1    │ │    N     │
└─────────┘ └──────────┘
```

### 5. Filter Manager

**Location**: `src/core/filter_manager.c`

**Purpose**: Evaluate filter expressions on events

**Components**:
- **Parser** (`filter_parser.c`): Parse filter expressions
- **Compiler** (`filter_compiler.c`): Compile to bytecode
- **Evaluator** (`filter_eval.c`): Execute bytecode

**Filter Language**:
```
interface == "eth0"
interface =~ "veth.*"
message_type IN [16, 17, 18]
(interface =~ "eth.*" AND state == "UP") OR priority == "high"
```

**Bytecode Instructions**:
```c
enum filter_opcode {
    OP_LOAD_FIELD,
    OP_LOAD_CONST,
    OP_CMP_EQ,
    OP_CMP_REGEX,
    OP_LOGICAL_AND,
    OP_LOGICAL_OR,
    OP_LOGICAL_NOT,
};
```

### 6. Correlation Engine

**Location**: `src/core/correlation_engine.c`

**Purpose**: Identify related events and patterns

**Key Features**:
- Sliding time window
- Pattern detection
- Anomaly detection
- Correlation ID assignment

**Components**:
- **Time Window** (`time_window.c`): Maintain event history
- **Pattern Detector** (`pattern_detector.c`): Find repeating patterns
- **Anomaly Detector** (`anomaly_detector.c`): Statistical anomaly detection

**Correlation Rules**:
```yaml
correlations:
  - name: "interface_init"
    events:
      - type: RTM_NEWLINK
      - type: RTM_NEWADDR
    time_window: 5s
```

### 7. Plugin Manager

**Location**: `src/plugins/plugin_manager.c`

**Purpose**: Load and manage plugins

**Key Features**:
- Dynamic loading (dlopen)
- API version verification
- Dependency resolution
- Error isolation

**Plugin Lifecycle**:
```
Discovery → Load → Verify → Initialize → Active → Cleanup → Unload
```

## Data Flow

### Event Processing Pipeline

```
┌──────────┐
│  Kernel  │
└────┬─────┘
     │ Netlink Message
     ▼
┌──────────────┐
│   Netlink    │
│   Listener   │
└────┬─────────┘
     │ Raw Message
     ▼
┌──────────────┐
│    Event     │
│   Parser     │
└────┬─────────┘
     │ nlmon_event
     ▼
┌──────────────┐
│ Ring Buffer  │
└────┬─────────┘
     │
     ▼
┌──────────────┐
│   Worker     │
│   Thread     │
└────┬─────────┘
     │
     ├─────────────┐
     │             │
     ▼             ▼
┌──────────┐  ┌──────────┐
│  Filter  │  │  Rate    │
│  Manager │  │ Limiter  │
└────┬─────┘  └────┬─────┘
     │             │
     └──────┬──────┘
            ▼
     ┌──────────────┐
     │ Correlation  │
     │   Engine     │
     └────┬─────────┘
          │
          ├──────────┬──────────┬──────────┐
          ▼          ▼          ▼          ▼
     ┌────────┐ ┌────────┐ ┌────────┐ ┌────────┐
     │Storage │ │ Export │ │ Plugin │ │  CLI   │
     └────────┘ └────────┘ └────────┘ └────────┘
```

### Configuration Flow

```
┌──────────────┐
│ Config File  │
│ (YAML)       │
└────┬─────────┘
     │
     ▼
┌──────────────┐
│    YAML      │
│   Parser     │
└────┬─────────┘
     │
     ▼
┌──────────────┐
│ Validation   │
└────┬─────────┘
     │
     ▼
┌──────────────┐
│   Config     │
│   Tree       │
└────┬─────────┘
     │
     ├──────────┬──────────┬──────────┐
     ▼          ▼          ▼          ▼
┌─────────┐ ┌────────┐ ┌────────┐ ┌────────┐
│  Core   │ │Storage │ │ Export │ │Plugins │
└─────────┘ └────────┘ └────────┘ └────────┘
```

## Threading Model

### Thread Architecture

nlmon uses a multi-threaded architecture for performance:

**Main Thread**:
- Configuration management
- Signal handling
- Component coordination
- CLI rendering (if enabled)

**Netlink Listener Thread**:
- Receive netlink messages
- Parse messages
- Queue events to ring buffer

**Worker Threads** (configurable count):
- Pop events from ring buffer
- Apply filters
- Invoke plugins
- Write to storage/export

**Web Server Thread** (if enabled):
- Handle HTTP requests
- WebSocket connections
- API endpoints

### Synchronization

**Lock-Free Structures**:
- Ring buffer (atomic operations)
- Event pool (per-thread allocation)

**Mutexes**:
- Configuration reload
- Plugin management
- Database writes
- File I/O

**Read-Write Locks**:
- Filter list access
- Configuration access

## Memory Management

### Object Pooling

**Purpose**: Reduce allocation overhead

**Pooled Objects**:
- Events
- Filters
- Buffers

**Implementation**:
```c
struct object_pool {
    void **objects;
    size_t capacity;
    size_t size;
    size_t object_size;
    pthread_mutex_t lock;
};
```

**Operations**:
- `pool_alloc()`: Get object from pool
- `pool_free()`: Return object to pool
- `pool_create()`: Initialize pool
- `pool_destroy()`: Cleanup pool

### Memory Tracking

**Location**: `src/core/memory_tracker.c`

**Features**:
- Allocation tracking
- Leak detection (debug builds)
- Memory usage reporting
- Peak usage tracking

**API**:
```c
void *tracked_malloc(size_t size, const char *tag);
void tracked_free(void *ptr);
void memory_report(void);
```

### Resource Cleanup

**Strategy**: RAII-style cleanup handlers

**Implementation**:
```c
struct resource_tracker {
    void (*cleanup)(void *data);
    void *data;
    struct resource_tracker *next;
};

void register_cleanup(void (*fn)(void *), void *data);
void cleanup_all(void);
```

## Plugin System

### Architecture

```
┌─────────────────────────────────────────┐
│         Plugin Manager                   │
│                                          │
│  ┌────────────┐  ┌────────────┐        │
│  │  Plugin    │  │  Plugin    │        │
│  │  Loader    │  │  Router    │        │
│  └────────────┘  └────────────┘        │
└─────────────────────────────────────────┘
         │                  │
         ▼                  ▼
┌─────────────┐    ┌─────────────┐
│  Plugin A   │    │  Plugin B   │
│  (.so)      │    │  (.so)      │
└─────────────┘    └─────────────┘
```

### Plugin Loading

**Process**:
1. Scan plugin directory
2. Load .so file with dlopen()
3. Resolve `nlmon_plugin_register` symbol
4. Verify API version
5. Check dependencies
6. Call init() callback
7. Register event handlers

### Plugin API

**Core Functions**:
```c
typedef struct nlmon_plugin_context {
    void (*log)(int level, const char *fmt, ...);
    int (*register_command)(const char *name, handler_t fn);
    int (*emit_event)(struct nlmon_event *event);
    const char *(*get_config)(const char *key);
    void *plugin_data;
} nlmon_plugin_context_t;
```

### Event Routing

**Flow**:
1. Event arrives at plugin manager
2. Check plugin event filter
3. If match, invoke plugin callback
4. Handle plugin errors
5. Continue to next plugin

**Error Isolation**:
- Plugin errors don't crash nlmon
- Timeout protection
- Error counting
- Automatic disable on repeated failures

## Storage Architecture

### Memory Buffer

**Purpose**: Fast access to recent events

**Implementation**: Circular buffer
- Fixed size (configurable)
- Overwrite oldest on full
- Lock-protected access

### SQLite Database

**Schema**:
```sql
CREATE TABLE events (
    id INTEGER PRIMARY KEY,
    timestamp INTEGER,
    message_type INTEGER,
    interface TEXT,
    namespace TEXT,
    details TEXT
);

CREATE INDEX idx_timestamp ON events(timestamp);
CREATE INDEX idx_interface ON events(interface);
```

**Features**:
- Batched inserts
- Prepared statements
- WAL mode for concurrency
- Automatic vacuum

### Audit Log

**Format**: Append-only with cryptographic chaining

**Structure**:
```
[TIMESTAMP] [SEQ] [PREV_HASH] [EVENT_TYPE] [DATA]
```

**Features**:
- SHA-256 hash chain
- Tamper detection
- Separate security log
- Log rotation

## Export Layer

### Architecture

```
┌──────────────┐
│    Event     │
└──────┬───────┘
       │
       ├─────────┬─────────┬─────────┬─────────┐
       ▼         ▼         ▼         ▼         ▼
┌──────────┐ ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐
│   PCAP   │ │ JSON │ │Prom. │ │Syslog│ │Custom│
│ Exporter │ │Export│ │Metrics│ │Fwd.  │ │Plugin│
└──────────┘ └──────┘ └──────┘ └──────┘ └──────┘
```

### PCAP Export

**Location**: `src/export/pcap_export.c`

**Features**:
- Standard PCAP format
- File rotation
- Compression support

### JSON Export

**Location**: `src/export/json_export.c`

**Format**:
```json
{
  "timestamp": "2025-11-04T12:34:56Z",
  "type": "link_change",
  "interface": "eth0",
  "details": {...}
}
```

### Prometheus Metrics

**Location**: `src/export/prometheus_exporter.c`

**Metrics**:
- Event counters by type
- Processing latency
- Queue depth
- Memory usage

**Endpoint**: `http://localhost:9090/metrics`

## Web Interface

### Architecture

```
┌─────────────────────────────────────┐
│         Web Server                   │
│  ┌──────────┐  ┌──────────┐        │
│  │   HTTP   │  │WebSocket │        │
│  │  Server  │  │  Server  │        │
│  └────┬─────┘  └────┬─────┘        │
└───────┼─────────────┼──────────────┘
        │             │
        ▼             ▼
┌──────────┐    ┌──────────┐
│ REST API │    │  Event   │
│          │    │ Streaming│
└──────────┘    └──────────┘
```

### Components

**HTTP Server**: libmicrohttpd
- Static file serving
- REST API endpoints
- TLS support

**WebSocket Server**: Custom implementation
- Real-time event streaming
- Subscription filtering
- Connection management

**REST API**:
```
GET  /api/events
GET  /api/events/:id
GET  /api/stats
GET  /api/config
PUT  /api/config
GET  /api/filters
POST /api/filters
```

## Security Architecture

### Access Control

**Layers**:
1. File system permissions
2. User authentication (web)
3. API key authentication
4. TLS encryption

### Security Features

**Event Detection**:
- Promiscuous mode detection
- ARP flood detection
- Route hijack detection
- Suspicious interface changes

**Audit Logging**:
- Cryptographic hash chain
- Tamper detection
- Separate security log

**Web Security**:
- JWT authentication
- HTTPS/TLS
- CORS protection
- Rate limiting

## Performance Considerations

### Optimization Techniques

**Lock-Free Data Structures**:
- Ring buffer for event queue
- Atomic operations for counters

**Zero-Copy**:
- Event passing by pointer
- Memory-mapped files

**Batching**:
- Database inserts
- File writes
- Network sends

**Caching**:
- Interface name lookup
- Configuration values
- Compiled filters

### Performance Metrics

**Target Performance**:
- Event throughput: 10,000+ events/sec
- Processing latency: < 1ms per event
- Memory usage: < 100MB baseline
- CPU usage: < 10% on idle

### Profiling

**Tools**:
- Built-in performance profiler
- gprof for CPU profiling
- valgrind for memory profiling
- perf for system profiling

## Extension Points

### Adding New Features

**1. New Event Type**:
- Add parser in `event_processor.c`
- Update event structure
- Add filter support

**2. New Export Format**:
- Implement exporter interface
- Add to export layer
- Update configuration

**3. New Integration**:
- Create plugin or built-in module
- Implement API client
- Add configuration section

**4. New CLI Command**:
- Register command handler
- Implement command logic
- Update help text

### API Stability

**Stable APIs**:
- Plugin API (versioned)
- Configuration format
- REST API (versioned)

**Internal APIs**:
- May change between versions
- Not for external use

## Conclusion

The nlmon architecture is designed for high performance, extensibility, and reliability. Key architectural decisions include:

- **Lock-free data structures** for high throughput
- **Plugin system** for extensibility
- **Layered architecture** for maintainability
- **Comprehensive error handling** for reliability
- **Multiple export options** for integration

For more information:
- [Plugin Development Guide](PLUGIN_DEVELOPMENT_GUIDE.md)
- [Configuration Guide](man/nlmon.conf.5)
- [API Documentation](PLUGIN_API.md)

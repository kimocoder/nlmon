# Netlink Implementation Migration Guide

## Overview

nlmon now supports two netlink implementations:

1. **New libnl-based implementation** (default) - Uses the libnl-tiny library for robust, production-ready netlink communication
2. **Legacy implementation** - The original basic netlink implementation

This guide explains how to choose between implementations and migrate existing deployments.

## Quick Start

### Using the New Implementation (Default)

The new libnl-based implementation is enabled by default. No configuration changes are needed:

```yaml
netlink:
  use_libnl: true  # This is the default
  protocols:
    route: true
    generic: true
```

### Using the Legacy Implementation

To use the legacy implementation, set `use_libnl` to `false`:

```yaml
netlink:
  use_libnl: false
  protocols:
    route: true
    generic: true
```

## Implementation Comparison

### New libnl-based Implementation

**Advantages:**
- Production-tested library used in OpenWrt and other embedded systems
- Robust error handling and recovery
- Proper attribute parsing with type safety
- Support for caching (link, address, route caches)
- Better handling of multipart messages
- Automatic reconnection on connection loss
- Comprehensive logging and debugging

**Features:**
- Full support for NETLINK_ROUTE, NETLINK_GENERIC, NETLINK_SOCK_DIAG, NETLINK_NETFILTER
- Generic netlink family resolution
- Attribute validation with policies
- Cache management for performance
- Message sequence tracking
- Non-blocking socket operation

**Requirements:**
- libnl-tiny library (included in nlmon)
- Slightly higher memory usage due to caching

### Legacy Implementation

**Advantages:**
- Simpler, more straightforward code
- Lower memory footprint
- No external dependencies

**Limitations:**
- Basic error handling
- Manual attribute parsing
- No caching support
- Limited reconnection logic
- Less comprehensive logging

**Use Cases:**
- Resource-constrained environments
- Simple monitoring scenarios
- Debugging or comparison purposes

## Configuration Options

### Common Options (Both Implementations)

```yaml
netlink:
  protocols:
    route: true          # NETLINK_ROUTE
    generic: true        # NETLINK_GENERIC
    sock_diag: false     # NETLINK_SOCK_DIAG
    netfilter: false     # NETLINK_NETFILTER
  
  buffer_size:
    receive: 32768
    send: 32768
```

### libnl-specific Options

These options are only available when `use_libnl: true`:

```yaml
netlink:
  use_libnl: true
  
  # Caching for performance
  caching:
    enabled: true
    link_cache: true     # Cache network interfaces
    addr_cache: true     # Cache IP addresses
    route_cache: false   # Cache routing table (optional)
  
  # Generic netlink families to resolve
  generic_families:
    - nl80211
    - taskstats
  
  # Multicast groups (informational)
  multicast_groups:
    - link
    - ipv4_ifaddr
    - ipv6_ifaddr
```

## Migration Steps

### For New Deployments

1. Use the default configuration (libnl-based implementation)
2. Enable desired protocols
3. Configure caching if needed for performance
4. Test thoroughly in your environment

### For Existing Deployments

#### Option 1: Migrate to New Implementation (Recommended)

1. **Backup your configuration:**
   ```bash
   cp /etc/nlmon/nlmon.yaml /etc/nlmon/nlmon.yaml.backup
   ```

2. **Update configuration to explicitly enable libnl:**
   ```yaml
   netlink:
     use_libnl: true
     # ... rest of your configuration
   ```

3. **Test in a non-production environment first**

4. **Monitor for any issues:**
   - Check logs for errors
   - Verify event capture is working
   - Compare output with legacy implementation if needed

5. **Deploy to production**

#### Option 2: Continue with Legacy Implementation

1. **Update configuration to disable libnl:**
   ```yaml
   netlink:
     use_libnl: false
     # ... rest of your configuration
   ```

2. **Remove any libnl-specific options:**
   - Remove `caching` section
   - Remove `generic_families` list
   - Remove `multicast_groups` list

3. **Restart nlmon**

## API Changes

### For Application Developers

If you're integrating nlmon into your application, use the unified API:

```c
#include "nlmon_netlink_api.h"

/* Initialize with configuration */
struct nlmon_netlink_config config;
config.use_libnl = true;  /* or false for legacy */

struct nlmon_multi_protocol_ctx *ctx = nlmon_netlink_init(&config);

/* Enable protocols */
nlmon_netlink_enable(ctx, NLMON_PROTO_ROUTE, config.use_libnl);

/* Set callback */
nlmon_netlink_set_callback(ctx, my_callback, user_data, config.use_libnl);

/* Get FD for event loop */
int fd = nlmon_netlink_get_fd(ctx, NLMON_PROTO_ROUTE, config.use_libnl);

/* Process messages */
nlmon_netlink_process(ctx, NLMON_PROTO_ROUTE, config.use_libnl);

/* Cleanup */
nlmon_netlink_destroy(ctx, config.use_libnl);
```

### Direct libnl API (Advanced)

For advanced use cases, you can use the libnl API directly:

```c
#include "nlmon_netlink.h"

/* Initialize netlink manager */
struct nlmon_nl_manager *mgr = nlmon_nl_manager_init();

/* Enable specific protocols */
nlmon_nl_enable_route(mgr);
nlmon_nl_enable_generic(mgr);

/* Resolve generic netlink families */
int nl80211_id = nlmon_nl_resolve_family(mgr, "nl80211");

/* Set event callback */
nlmon_nl_set_callback(mgr, my_event_handler, user_data);

/* Get file descriptors for event loop */
int route_fd = nlmon_nl_get_route_fd(mgr);
int genl_fd = nlmon_nl_get_genl_fd(mgr);

/* Process messages when data available */
nlmon_nl_process_route(mgr);
nlmon_nl_process_genl(mgr);

/* Cleanup */
nlmon_nl_manager_destroy(mgr);
```

### Compatibility Layer

The compatibility layer ensures that existing code continues to work:

- Old `nlmon_multi_protocol_*` functions still work
- Event format is automatically translated
- No code changes required for basic usage

### Deprecated Functions

The following functions from the legacy implementation are deprecated but still supported:

| Deprecated Function | Replacement | Notes |
|---------------------|-------------|-------|
| `nlmon_multi_protocol_init()` | `nlmon_netlink_init()` or `nlmon_nl_manager_init()` | Use unified API or direct libnl API |
| `nlmon_multi_protocol_enable()` | `nlmon_netlink_enable()` or `nlmon_nl_enable_*()` | Protocol-specific enable functions |
| `nlmon_multi_protocol_get_fd()` | `nlmon_netlink_get_fd()` or `nlmon_nl_get_*_fd()` | Protocol-specific FD getters |
| `nlmon_multi_protocol_process()` | `nlmon_netlink_process()` or `nlmon_nl_process_*()` | Protocol-specific process functions |
| `nlmon_multi_protocol_destroy()` | `nlmon_netlink_destroy()` or `nlmon_nl_manager_destroy()` | Cleanup functions |

**Migration Timeline:**
- Current: All functions supported
- Next major version: Deprecation warnings added
- Future: Legacy functions may be removed (with 6+ months notice)

### Event Structure Changes

The event structure has been enhanced with netlink-specific fields:

**Old Structure (Legacy):**
```c
struct nlmon_event {
    uint64_t timestamp;
    nlmon_event_type_t type;
    // Basic netlink info
    int protocol;
    int msg_type;
    // Limited parsed data
};
```

**New Structure (libnl):**
```c
struct nlmon_event {
    uint64_t timestamp;
    nlmon_event_type_t type;
    
    struct {
        int protocol;
        uint16_t msg_type;
        uint16_t msg_flags;
        uint32_t seq;
        uint32_t pid;
        
        // Generic netlink specific
        uint8_t genl_cmd;
        uint8_t genl_version;
        uint16_t genl_family_id;
        char genl_family_name[32];
        
        // Comprehensive parsed data
        union {
            struct nlmon_link_info link;
            struct nlmon_addr_info addr;
            struct nlmon_route_info route;
            struct nlmon_neigh_info neigh;
            struct nlmon_diag_info diag;
            struct nlmon_ct_info conntrack;
            struct nlmon_nl80211_info nl80211;
            struct nlmon_qca_vendor_info qca;
        } data;
    } netlink;
};
```

**Compatibility:** The compatibility layer automatically populates both old and new fields, so existing code continues to work.

## Detailed Migration Scenarios

### Scenario 1: Basic Monitoring Setup

**Before (Legacy):**
```yaml
netlink:
  protocols:
    route: true
```

**After (libnl):**
```yaml
netlink:
  use_libnl: true  # Explicitly enable (or omit, as it's default)
  protocols:
    route: true
  caching:
    enabled: true
    link_cache: true
```

**Benefits:** Improved reliability, automatic caching, better error handling

---

### Scenario 2: WiFi Monitoring

**Before (Legacy):**
```yaml
netlink:
  protocols:
    generic: true
```

**After (libnl):**
```yaml
netlink:
  use_libnl: true
  protocols:
    generic: true
  generic_families:
    - nl80211
  caching:
    enabled: true
```

**Benefits:** Automatic family resolution, better nl80211 attribute parsing

---

### Scenario 3: High-Traffic Production Environment

**Before (Legacy):**
```yaml
netlink:
  protocols:
    route: true
    generic: true
    netfilter: true
  buffer_size:
    receive: 65536
```

**After (libnl):**
```yaml
netlink:
  use_libnl: true
  protocols:
    route: true
    generic: true
    netfilter: true
  buffer_size:
    receive: 65536
    send: 32768
  caching:
    enabled: true
    link_cache: true
    addr_cache: true
    route_cache: true
  limits:
    max_message_size: 16384
    message_pool_size: 2048
```

**Benefits:** Message pooling, caching reduces kernel queries, better performance under load

---

### Scenario 4: Resource-Constrained Environment

**Before (Legacy):**
```yaml
netlink:
  protocols:
    route: true
  buffer_size:
    receive: 16384
```

**After (Stay with Legacy):**
```yaml
netlink:
  use_libnl: false  # Explicitly use legacy
  protocols:
    route: true
  buffer_size:
    receive: 16384
```

**Rationale:** Legacy implementation has lower memory footprint for very constrained systems

---

### Scenario 5: Custom Application Integration

**Before (Legacy Code):**
```c
#include "netlink_multi_protocol.h"

struct nlmon_multi_protocol_ctx *ctx = nlmon_multi_protocol_init();
nlmon_multi_protocol_enable(ctx, NLMON_PROTO_ROUTE);
nlmon_multi_protocol_set_callback(ctx, my_callback, NULL);

// Event loop
int fd = nlmon_multi_protocol_get_fd(ctx, NLMON_PROTO_ROUTE);
// ... poll/select on fd ...
nlmon_multi_protocol_process(ctx, NLMON_PROTO_ROUTE);

nlmon_multi_protocol_destroy(ctx);
```

**After (Using Compatibility API):**
```c
#include "nlmon_netlink_api.h"

struct nlmon_netlink_config config = {
    .use_libnl = true,
    .enable_route = true
};

struct nlmon_multi_protocol_ctx *ctx = nlmon_netlink_init(&config);
nlmon_netlink_enable(ctx, NLMON_PROTO_ROUTE, config.use_libnl);
nlmon_netlink_set_callback(ctx, my_callback, NULL, config.use_libnl);

// Event loop
int fd = nlmon_netlink_get_fd(ctx, NLMON_PROTO_ROUTE, config.use_libnl);
// ... poll/select on fd ...
nlmon_netlink_process(ctx, NLMON_PROTO_ROUTE, config.use_libnl);

nlmon_netlink_destroy(ctx, config.use_libnl);
```

**After (Using Direct libnl API):**
```c
#include "nlmon_netlink.h"

struct nlmon_nl_manager *mgr = nlmon_nl_manager_init();
nlmon_nl_enable_route(mgr);
nlmon_nl_set_callback(mgr, my_callback, NULL);

// Event loop
int fd = nlmon_nl_get_route_fd(mgr);
// ... poll/select on fd ...
nlmon_nl_process_route(mgr);

nlmon_nl_manager_destroy(mgr);
```

---

## Troubleshooting

### Issue: Events not being captured with libnl

**Symptoms:**
- No events appearing in logs or exports
- File descriptors show no activity

**Solution:**
1. Check that protocols are enabled in configuration
2. Verify buffer sizes are adequate
3. Check logs for connection errors:
   ```bash
   journalctl -u nlmon | grep -i netlink
   ```
4. Try disabling caching temporarily:
   ```yaml
   caching:
     enabled: false
   ```
5. Verify permissions (CAP_NET_ADMIN required):
   ```bash
   getcap /usr/bin/nlmon
   ```
6. Test with debug logging:
   ```yaml
   logging:
     level: debug
     modules:
       netlink: trace
   ```

---

### Issue: High memory usage with libnl

**Symptoms:**
- Memory usage grows over time
- OOM killer terminates nlmon

**Solution:**
1. Disable caching if not needed:
   ```yaml
   caching:
     enabled: false
   ```
2. Reduce buffer sizes if appropriate:
   ```yaml
   buffer_size:
     receive: 16384
     send: 16384
   ```
3. Set resource limits:
   ```yaml
   limits:
     max_message_size: 8192
     message_pool_size: 512
   ```
4. Monitor with resource tracker:
   ```yaml
   monitoring:
     resource_tracking: true
   ```
5. Consider using legacy implementation for very constrained environments

---

### Issue: Generic netlink family not found

**Symptoms:**
- Error: "Failed to resolve family 'nl80211'"
- Generic netlink events not captured

**Solution:**
1. Ensure the kernel module is loaded:
   ```bash
   lsmod | grep nl80211
   modprobe cfg80211  # Load if needed
   ```
2. Check that `generic` protocol is enabled:
   ```yaml
   protocols:
     generic: true
   ```
3. Verify family name is correct in configuration:
   ```yaml
   generic_families:
     - nl80211  # Correct spelling
   ```
4. Check available families:
   ```bash
   genl-ctrl-list
   ```

---

### Issue: Connection loss and reconnection

**Symptoms:**
- Intermittent event loss
- Log messages about reconnection

**Solution:**
- The libnl implementation automatically attempts reconnection
- Check logs for reconnection attempts:
  ```bash
  journalctl -u nlmon | grep reconnect
  ```
- Verify network namespace configuration if using containers:
  ```bash
  ip netns list
  ```
- Increase buffer sizes to prevent overruns:
  ```yaml
  buffer_size:
    receive: 65536
  ```

---

### Issue: Compilation errors after migration

**Symptoms:**
- Undefined references to libnl functions
- Missing header files

**Solution:**
1. Ensure libnl-tiny is compiled:
   ```bash
   make clean
   make
   ```
2. Check that libnl-tiny.a exists:
   ```bash
   ls -l libnl/libnl-tiny.a
   ```
3. Verify Makefile includes libnl:
   ```makefile
   LDFLAGS += -Llibnl -lnl-tiny
   ```

---

### Issue: Different event data between implementations

**Symptoms:**
- Filters work differently
- Missing fields in events

**Solution:**
1. Use the compatibility layer for consistent behavior
2. Update filters to use new event structure fields:
   ```yaml
   # Old filter
   filter: "protocol == NETLINK_ROUTE"
   
   # New filter
   filter: "netlink.protocol == NETLINK_ROUTE"
   ```
3. Check event structure documentation in NETLINK_API.md

---

### Issue: Performance degradation after migration

**Symptoms:**
- Higher CPU usage
- Increased latency

**Solution:**
1. Enable caching to reduce kernel queries:
   ```yaml
   caching:
     enabled: true
     link_cache: true
     addr_cache: true
   ```
2. Increase message pool size:
   ```yaml
   limits:
     message_pool_size: 2048
   ```
3. Profile with performance tools:
   ```bash
   perf record -g nlmon
   perf report
   ```
4. Compare with legacy implementation to identify bottlenecks

## Performance Considerations

### libnl Implementation

- **Throughput:** Can handle 10,000+ messages/second
- **Memory:** ~2-5 MB base + cache overhead
- **CPU:** Minimal overhead, efficient attribute parsing
- **Latency:** Low latency with non-blocking sockets

### Legacy Implementation

- **Throughput:** Can handle 5,000+ messages/second
- **Memory:** ~1-2 MB base
- **CPU:** Slightly higher due to manual parsing
- **Latency:** Low latency

## Testing Your Migration

### Pre-Migration Testing

Before migrating to production:

1. **Test in development environment:**
   ```bash
   # Run with libnl
   nlmon --config test-libnl.yaml --test-mode
   
   # Run with legacy
   nlmon --config test-legacy.yaml --test-mode
   
   # Compare outputs
   diff <(nlmon --config test-libnl.yaml --dump-events) \
        <(nlmon --config test-legacy.yaml --dump-events)
   ```

2. **Verify event capture:**
   ```bash
   # Generate test events
   ip link set eth0 down
   ip link set eth0 up
   
   # Check nlmon captured them
   journalctl -u nlmon -n 50
   ```

3. **Load testing:**
   ```bash
   # Generate high event rate
   for i in {1..1000}; do
     ip addr add 10.0.$((i/256)).$((i%256))/32 dev lo
   done
   
   # Monitor nlmon performance
   top -p $(pidof nlmon)
   ```

4. **Memory leak testing:**
   ```bash
   # Run for extended period
   valgrind --leak-check=full --show-leak-kinds=all nlmon
   ```

### Post-Migration Validation

After migrating:

1. **Verify all protocols working:**
   ```bash
   # Check active protocols
   nlmon --status | grep -A 10 "Netlink Protocols"
   ```

2. **Monitor for errors:**
   ```bash
   # Watch for netlink errors
   journalctl -u nlmon -f | grep -i error
   ```

3. **Validate exports:**
   ```bash
   # Check JSON export
   tail -f /var/log/nlmon/events.json | jq .
   
   # Check Prometheus metrics
   curl http://localhost:9090/metrics | grep nlmon_netlink
   ```

4. **Performance comparison:**
   ```bash
   # Measure event processing rate
   nlmon --benchmark --duration 60
   ```

---

## Best Practices

### General Recommendations

1. **Use libnl for production deployments** - More robust and feature-rich
2. **Enable caching for high-traffic scenarios** - Reduces kernel queries
3. **Monitor memory usage** - Adjust cache settings if needed
4. **Test both implementations** - Verify behavior in your environment
5. **Keep configuration simple** - Only enable needed protocols
6. **Use filters** - Reduce processing overhead by filtering early

### Configuration Best Practices

1. **Start minimal, add as needed:**
   ```yaml
   netlink:
     use_libnl: true
     protocols:
       route: true  # Start with one protocol
   ```

2. **Enable caching for frequently accessed data:**
   ```yaml
   caching:
     enabled: true
     link_cache: true    # Interfaces change infrequently
     addr_cache: true    # Addresses change moderately
     route_cache: false  # Routes may change frequently
   ```

3. **Set appropriate buffer sizes:**
   ```yaml
   buffer_size:
     receive: 32768  # Default for normal traffic
     # receive: 65536  # High traffic
     # receive: 16384  # Low traffic / constrained memory
   ```

4. **Use resource limits:**
   ```yaml
   limits:
     max_message_size: 16384
     message_pool_size: 1024
   ```

### Development Best Practices

1. **Use the compatibility API for portability:**
   ```c
   #include "nlmon_netlink_api.h"
   // Works with both implementations
   ```

2. **Handle errors gracefully:**
   ```c
   int ret = nlmon_nl_enable_route(mgr);
   if (ret < 0) {
       fprintf(stderr, "Error: %s\n", nlmon_nl_strerror(ret));
       // Fallback or exit gracefully
   }
   ```

3. **Use event callbacks for loose coupling:**
   ```c
   void my_handler(struct nlmon_event *evt, void *data) {
       // Process event
   }
   nlmon_nl_set_callback(mgr, my_handler, user_data);
   ```

4. **Test with both implementations:**
   ```c
   #ifdef USE_LIBNL
       struct nlmon_nl_manager *mgr = nlmon_nl_manager_init();
   #else
       struct nlmon_multi_protocol_ctx *ctx = nlmon_multi_protocol_init();
   #endif
   ```

---

## Rollback Procedure

If you need to rollback to the legacy implementation:

1. **Update configuration:**
   ```yaml
   netlink:
     use_libnl: false
   ```

2. **Remove libnl-specific options:**
   ```bash
   # Edit config file
   sed -i '/caching:/,/route_cache:/d' /etc/nlmon/nlmon.yaml
   sed -i '/generic_families:/,/- nl80211/d' /etc/nlmon/nlmon.yaml
   ```

3. **Restart nlmon:**
   ```bash
   systemctl restart nlmon
   ```

4. **Verify operation:**
   ```bash
   journalctl -u nlmon -n 50
   nlmon --status
   ```

---

## Future Plans

### Roadmap

- **Current (v1.x):** Both implementations supported
- **Next release (v2.0):** Deprecation warnings for legacy API
- **Future (v3.0):** Legacy implementation may be removed (with 6+ months notice)

### Upcoming Features (libnl only)

- Enhanced caching with TTL and refresh policies
- Support for additional netlink protocols (NETLINK_XFRM, NETLINK_AUDIT)
- Improved error recovery and reconnection logic
- Performance optimizations for high-throughput scenarios
- Better integration with container networking

### Legacy Implementation

- Will be maintained for compatibility
- Security fixes and critical bug fixes only
- No new features planned
- Recommended for resource-constrained environments only

## Common Migration Patterns

### Pattern 1: Gradual Migration

Migrate one protocol at a time to minimize risk:

**Step 1: Migrate NETLINK_ROUTE**
```yaml
netlink:
  use_libnl: true
  protocols:
    route: true
    # Keep others disabled initially
```

**Step 2: Add NETLINK_GENERIC**
```yaml
netlink:
  use_libnl: true
  protocols:
    route: true
    generic: true
  generic_families:
    - nl80211
```

**Step 3: Add remaining protocols**
```yaml
netlink:
  use_libnl: true
  protocols:
    route: true
    generic: true
    sock_diag: true
    netfilter: true
```

---

### Pattern 2: Parallel Deployment

Run both implementations side-by-side for comparison:

**Instance 1 (libnl):**
```yaml
# /etc/nlmon/nlmon-libnl.yaml
netlink:
  use_libnl: true
  protocols:
    route: true

export:
  json:
    file: /var/log/nlmon/events-libnl.json
```

**Instance 2 (legacy):**
```yaml
# /etc/nlmon/nlmon-legacy.yaml
netlink:
  use_libnl: false
  protocols:
    route: true

export:
  json:
    file: /var/log/nlmon/events-legacy.json
```

**Compare outputs:**
```bash
diff <(jq -S . /var/log/nlmon/events-libnl.json) \
     <(jq -S . /var/log/nlmon/events-legacy.json)
```

---

### Pattern 3: Feature Flag Migration

Use feature flags for gradual rollout:

```yaml
netlink:
  use_libnl: ${NLMON_USE_LIBNL:-true}  # Environment variable
  protocols:
    route: true
```

```bash
# Test with libnl
NLMON_USE_LIBNL=true nlmon --config nlmon.yaml

# Test with legacy
NLMON_USE_LIBNL=false nlmon --config nlmon.yaml
```

---

### Pattern 4: Canary Deployment

Deploy to a subset of servers first:

1. **Canary servers (10%):** Use libnl implementation
2. **Monitor for 24-48 hours:** Check metrics, logs, errors
3. **Expand to 50%:** If no issues, expand deployment
4. **Full rollout:** Deploy to all servers

---

## Frequently Asked Questions

### Q: Do I need to migrate immediately?

**A:** No. The legacy implementation is still fully supported. Migrate when convenient or when you need features only available in libnl.

---

### Q: Will my existing configuration work with libnl?

**A:** Yes. Basic configuration options are compatible. You may want to add libnl-specific options for enhanced features.

---

### Q: Can I switch between implementations without downtime?

**A:** Yes, but you'll need to restart nlmon. Use a rolling restart strategy for high-availability deployments.

---

### Q: What's the performance difference?

**A:** libnl typically has better performance due to caching and optimized parsing. Exact difference depends on workload.

---

### Q: Is there a way to test compatibility before migrating?

**A:** Yes. Run both implementations in parallel (different instances) and compare outputs.

---

### Q: What happens to my filters after migration?

**A:** Filters continue to work. You may want to update them to use new event structure fields for enhanced capabilities.

---

### Q: Can I use libnl for some protocols and legacy for others?

**A:** No. The implementation choice applies to all protocols. Use one or the other.

---

### Q: How do I know if migration was successful?

**A:** Check logs for errors, verify events are being captured, compare event counts with legacy implementation.

---

### Q: What if I encounter a bug in libnl?

**A:** Report the bug and temporarily switch back to legacy implementation while the issue is resolved.

---

### Q: Will my custom code need changes?

**A:** If using the compatibility API, no changes needed. If using protocol-specific functions, minor updates may be required.

---

## Migration Checklist

Use this checklist to ensure a smooth migration:

### Pre-Migration

- [ ] Review current configuration
- [ ] Identify enabled protocols
- [ ] Document custom integrations
- [ ] Set up test environment
- [ ] Backup current configuration
- [ ] Review libnl features and benefits

### Testing Phase

- [ ] Create test configuration with libnl
- [ ] Test basic event capture
- [ ] Test all enabled protocols
- [ ] Verify filters work correctly
- [ ] Test exports (JSON, syslog, etc.)
- [ ] Load test with high event rate
- [ ] Memory leak test (24+ hours)
- [ ] Compare outputs with legacy

### Deployment Phase

- [ ] Update production configuration
- [ ] Deploy to canary servers first
- [ ] Monitor logs for errors
- [ ] Check event capture rates
- [ ] Verify exports working
- [ ] Monitor memory usage
- [ ] Monitor CPU usage
- [ ] Expand to more servers

### Post-Migration

- [ ] Verify all servers migrated
- [ ] Remove legacy configuration backups (after grace period)
- [ ] Update documentation
- [ ] Train team on new features
- [ ] Set up monitoring alerts
- [ ] Document any issues encountered
- [ ] Share feedback with nlmon team

---

## Support

### Getting Help

For issues or questions:

1. **Check documentation:**
   - [Netlink Integration Guide](NETLINK_INTEGRATION.md)
   - [Netlink API Documentation](NETLINK_API.md)
   - [Troubleshooting section](#troubleshooting) above

2. **Enable debug logging:**
   ```yaml
   logging:
     level: debug
     modules:
       netlink: trace
       libnl: trace
   ```

3. **Compare implementations:**
   ```bash
   # Run both and compare
   diff <(nlmon --config libnl.yaml --dump-events) \
        <(nlmon --config legacy.yaml --dump-events)
   ```

4. **Report bugs:**
   - Include configuration file
   - Include relevant log excerpts
   - Include steps to reproduce
   - Include system information (kernel version, etc.)

### Community Resources

- GitHub Issues: Report bugs and feature requests
- Documentation: Comprehensive guides and API docs
- Examples: Sample configurations in `docs/examples/`

---

## References

### Documentation

- [Netlink Integration Guide](NETLINK_INTEGRATION.md) - Comprehensive integration guide
- [Netlink API Documentation](NETLINK_API.md) - Complete API reference
- [Netlink Filtering Guide](NETLINK_FILTERING.md) - Filter syntax and examples
- [nlmon Architecture](ARCHITECTURE.md) - Overall system architecture
- [Configuration Guide](README.md) - General configuration reference

### External Resources

- [libnl-tiny source](https://git.openwrt.org/project/libnl-tiny.git) - OpenWrt libnl-tiny repository
- [Linux Netlink Documentation](https://www.kernel.org/doc/html/latest/userspace-api/netlink/intro.html) - Kernel netlink documentation
- [Netlink Protocol](https://man7.org/linux/man-pages/man7/netlink.7.html) - Man page for netlink
- [nl80211 Documentation](https://wireless.wiki.kernel.org/en/developers/documentation/nl80211) - WiFi netlink documentation

### Related Guides

- [QCA Vendor Support](QCA_VENDOR_SUPPORT.md) - Qualcomm Atheros vendor commands
- [WMI Monitoring](WMI_MONITORING.md) - WiFi Management Interface monitoring
- [Event Hooks](EVENT_HOOKS.md) - Event-driven automation
- [Alert System](ALERT_SYSTEM.md) - Alert configuration and management

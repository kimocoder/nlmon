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

### Compatibility Layer

The compatibility layer ensures that existing code continues to work:

- Old `nlmon_multi_protocol_*` functions still work
- Event format is automatically translated
- No code changes required for basic usage

## Troubleshooting

### Issue: Events not being captured with libnl

**Solution:**
1. Check that protocols are enabled in configuration
2. Verify buffer sizes are adequate
3. Check logs for connection errors
4. Try disabling caching temporarily

### Issue: High memory usage with libnl

**Solution:**
1. Disable caching if not needed:
   ```yaml
   caching:
     enabled: false
   ```
2. Reduce buffer sizes if appropriate
3. Consider using legacy implementation for very constrained environments

### Issue: Generic netlink family not found

**Solution:**
1. Ensure the kernel module is loaded (e.g., `nl80211` for WiFi)
2. Check that `generic` protocol is enabled
3. Verify family name is correct in configuration

### Issue: Connection loss and reconnection

**Solution:**
- The libnl implementation automatically attempts reconnection
- Check logs for reconnection attempts
- Verify network namespace configuration if using containers

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

## Best Practices

1. **Use libnl for production deployments** - More robust and feature-rich
2. **Enable caching for high-traffic scenarios** - Reduces kernel queries
3. **Monitor memory usage** - Adjust cache settings if needed
4. **Test both implementations** - Verify behavior in your environment
5. **Keep configuration simple** - Only enable needed protocols
6. **Use filters** - Reduce processing overhead by filtering early

## Future Plans

- The legacy implementation will be maintained for compatibility
- New features will primarily target the libnl implementation
- Eventually, the legacy implementation may be deprecated (with advance notice)

## Support

For issues or questions:
- Check the troubleshooting section above
- Review logs with `log_level: debug`
- Compare behavior between implementations
- Report bugs with configuration and logs

## References

- [libnl-tiny documentation](https://git.openwrt.org/project/libnl-tiny.git)
- [Netlink Protocol](https://www.kernel.org/doc/html/latest/userspace-api/netlink/intro.html)
- [nlmon Architecture](ARCHITECTURE.md)
- [Configuration Guide](README.md)

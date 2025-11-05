# Netlink Attribute Filtering

This document describes the enhanced filter system that supports filtering on netlink-specific attributes across different netlink protocols.

## Overview

The nlmon filter system has been extended to support filtering on netlink message attributes, allowing fine-grained control over which network events are captured and processed. This enables filtering based on interface names, IP addresses, ports, protocols, and many other netlink-specific fields.

## Supported Netlink Protocols

The filter system supports the following netlink protocols:

- **NETLINK_ROUTE** (protocol 0): Network interfaces, addresses, routes, neighbors
- **NETLINK_GENERIC** (protocol 16): nl80211, QCA vendor commands
- **NETLINK_SOCK_DIAG** (protocol 4): Socket diagnostics
- **NETLINK_NETFILTER** (protocol 12): Connection tracking

## Filter Syntax

Filters use a simple expression language with the following operators:

- **Comparison**: `==`, `!=`, `<`, `>`, `<=`, `>=`
- **Pattern matching**: `=~` (regex match), `!~` (regex not match)
- **Logical**: `AND`, `OR`, `NOT`
- **Set membership**: `IN`
- **Grouping**: `(` and `)`

### Field Paths

Netlink attributes are accessed using dot notation:

```
netlink.<category>.<field>
```

For example:
- `netlink.link.ifname` - Interface name from link message
- `netlink.addr.addr` - IP address from address message
- `netlink.diag.src_port` - Source port from socket diagnostics

## Available Filter Fields

### Common Netlink Fields

These fields are available for all netlink messages:

| Field | Type | Description |
|-------|------|-------------|
| `netlink.protocol` | number | Netlink protocol family (0=ROUTE, 4=SOCK_DIAG, etc.) |
| `netlink.msg_type` | number | Message type (RTM_NEWLINK, etc.) |
| `netlink.msg_flags` | number | Message flags (NLM_F_MULTI, etc.) |
| `netlink.seq` | number | Sequence number |
| `netlink.pid` | number | Port ID |

### Generic Netlink Fields

For NETLINK_GENERIC messages:

| Field | Type | Description |
|-------|------|-------------|
| `netlink.genl_cmd` | number | Generic netlink command |
| `netlink.genl_version` | number | Generic netlink version |
| `netlink.genl_family_id` | number | Family ID |
| `netlink.genl_family_name` | string | Family name (e.g., "nl80211") |

### NETLINK_ROUTE - Link (Interface) Fields

| Field | Type | Description |
|-------|------|-------------|
| `netlink.link.ifname` | string | Interface name (e.g., "eth0", "wlan0") |
| `netlink.link.ifindex` | number | Interface index |
| `netlink.link.flags` | number | Interface flags (IFF_UP, IFF_RUNNING, etc.) |
| `netlink.link.mtu` | number | Maximum transmission unit |
| `netlink.link.operstate` | number | Operational state |
| `netlink.link.qdisc` | string | Queueing discipline (e.g., "mq", "fq_codel") |

### NETLINK_ROUTE - Address Fields

| Field | Type | Description |
|-------|------|-------------|
| `netlink.addr.family` | number | Address family (2=AF_INET, 10=AF_INET6) |
| `netlink.addr.ifindex` | number | Interface index |
| `netlink.addr.prefixlen` | number | Prefix length (e.g., 24 for /24) |
| `netlink.addr.scope` | number | Address scope |
| `netlink.addr.addr` | string | IP address (e.g., "192.168.1.100") |
| `netlink.addr.label` | string | Address label |

### NETLINK_ROUTE - Route Fields

| Field | Type | Description |
|-------|------|-------------|
| `netlink.route.family` | number | Address family |
| `netlink.route.dst` | string | Destination address |
| `netlink.route.src` | string | Source address |
| `netlink.route.gateway` | string | Gateway address |
| `netlink.route.oif` | number | Output interface index |
| `netlink.route.protocol` | number | Routing protocol |
| `netlink.route.scope` | number | Route scope |
| `netlink.route.type` | number | Route type |
| `netlink.route.priority` | number | Route priority/metric |

### NETLINK_ROUTE - Neighbor Fields

| Field | Type | Description |
|-------|------|-------------|
| `netlink.neigh.family` | number | Address family |
| `netlink.neigh.ifindex` | number | Interface index |
| `netlink.neigh.state` | number | Neighbor state (NUD_REACHABLE, etc.) |
| `netlink.neigh.dst` | string | Neighbor address |

### NETLINK_SOCK_DIAG - Socket Diagnostics Fields

| Field | Type | Description |
|-------|------|-------------|
| `netlink.diag.family` | number | Address family |
| `netlink.diag.state` | number | Socket state (TCP_ESTABLISHED, etc.) |
| `netlink.diag.protocol` | number | Protocol (6=TCP, 17=UDP) |
| `netlink.diag.src_port` | number | Source port |
| `netlink.diag.dst_port` | number | Destination port |
| `netlink.diag.src_addr` | string | Source address |
| `netlink.diag.dst_addr` | string | Destination address |
| `netlink.diag.uid` | number | User ID |
| `netlink.diag.inode` | number | Socket inode |

### NETLINK_NETFILTER - Connection Tracking Fields

| Field | Type | Description |
|-------|------|-------------|
| `netlink.ct.protocol` | number | Protocol (6=TCP, 17=UDP) |
| `netlink.ct.tcp_state` | number | TCP connection state |
| `netlink.ct.src_addr` | string | Source address |
| `netlink.ct.dst_addr` | string | Destination address |
| `netlink.ct.src_port` | number | Source port |
| `netlink.ct.dst_port` | number | Destination port |
| `netlink.ct.mark` | number | Connection mark |

### NETLINK_GENERIC - nl80211 Fields

| Field | Type | Description |
|-------|------|-------------|
| `netlink.nl80211.cmd` | number | nl80211 command |
| `netlink.nl80211.wiphy` | number | Wireless PHY index |
| `netlink.nl80211.ifindex` | number | Interface index |
| `netlink.nl80211.ifname` | string | Interface name |
| `netlink.nl80211.iftype` | number | Interface type |
| `netlink.nl80211.freq` | number | Frequency (MHz) |

### QCA Vendor Fields

| Field | Type | Description |
|-------|------|-------------|
| `netlink.qca.subcmd` | number | QCA vendor subcommand |
| `netlink.qca.subcmd_name` | string | Subcommand name |
| `netlink.qca.vendor_id` | number | Vendor ID (OUI) |

## Filter Examples

### Basic Interface Filtering

Monitor only events for a specific interface:

```
netlink.link.ifname == "wlan0"
```

Monitor multiple interfaces:

```
netlink.link.ifname IN ["eth0", "wlan0", "wlan1"]
```

### IP Address Filtering

Monitor address changes for a specific subnet:

```
netlink.addr.addr =~ "^192\\.168\\.1\\."
```

Monitor only IPv4 addresses:

```
netlink.addr.family == 2
```

### Port-Based Filtering

Monitor SSH connections:

```
netlink.diag.protocol == 6 AND (netlink.diag.src_port == 22 OR netlink.diag.dst_port == 22)
```

Monitor high-numbered ports (ephemeral):

```
netlink.diag.src_port > 32768
```

### Protocol-Specific Filtering

Monitor only NETLINK_ROUTE messages:

```
netlink.protocol == 0
```

Monitor link state changes (up/down):

```
netlink.protocol == 0 AND netlink.msg_type == 16
```

### Complex Filters

Monitor WiFi interface changes with high MTU:

```
netlink.link.ifname =~ "^wlan" AND netlink.link.mtu >= 1500
```

Monitor established TCP connections to external addresses:

```
netlink.diag.protocol == 6 AND 
netlink.diag.state == 1 AND 
NOT netlink.diag.dst_addr =~ "^(127\\.|192\\.168\\.)"
```

Monitor routes with specific gateway:

```
netlink.route.gateway == "192.168.1.1" AND netlink.route.priority < 100
```

### User-Based Filtering

Monitor sockets owned by specific user:

```
netlink.diag.uid == 1000
```

Monitor sockets owned by root:

```
netlink.diag.uid == 0
```

### Connection Tracking Filters

Monitor HTTPS connections:

```
netlink.ct.protocol == 6 AND netlink.ct.dst_port == 443
```

Monitor connections with specific mark:

```
netlink.ct.mark == 256
```

## Configuration

Filters can be configured in the YAML configuration file:

```yaml
filters:
  - name: "wifi-only"
    expression: "netlink.link.ifname =~ '^wlan'"
    enabled: true
  
  - name: "ssh-connections"
    expression: "netlink.diag.protocol == 6 AND (netlink.diag.src_port == 22 OR netlink.diag.dst_port == 22)"
    enabled: true
  
  - name: "local-addresses"
    expression: "netlink.addr.addr =~ '^192\\.168\\.'"
    enabled: false
```

## Performance Considerations

1. **Attribute Validation**: Filters automatically validate that attributes exist before accessing them. Accessing non-existent attributes returns false.

2. **Type Safety**: The filter system performs type-aware comparisons. String fields are compared as strings, numeric fields as numbers.

3. **Short-Circuit Evaluation**: Logical operators (AND, OR) use short-circuit evaluation for efficiency.

4. **Regex Caching**: Regular expressions are compiled once and cached for reuse.

## Best Practices

1. **Be Specific**: Use specific filters to reduce processing overhead. Filter on protocol first, then on specific attributes.

2. **Use Numeric Comparisons**: When possible, use numeric comparisons instead of string comparisons for better performance.

3. **Combine Filters**: Use logical operators to combine multiple conditions in a single filter rather than multiple separate filters.

4. **Test Filters**: Use the filter validation tools to test filter expressions before deploying them.

## Troubleshooting

### Filter Not Matching

If a filter isn't matching expected events:

1. Check that the netlink protocol is correct
2. Verify the attribute path is correct for the message type
3. Ensure the attribute exists in the message (use debug logging)
4. Check data types (string vs. number)

### Performance Issues

If filtering is causing performance issues:

1. Move protocol checks to the beginning of the filter
2. Avoid complex regex patterns when simple comparisons work
3. Use numeric comparisons instead of string comparisons where possible
4. Consider using multiple specific filters instead of one complex filter

## See Also

- [Filter System Documentation](FILTER_SYSTEM.md)
- [Netlink Integration Guide](NETLINK_INTEGRATION.md)
- [Configuration Reference](nlmon.conf.5)

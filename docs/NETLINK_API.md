# Netlink API Documentation

## Overview

This document provides comprehensive API documentation for nlmon's netlink integration. The API is organized into several layers:

1. **Netlink Manager API** - High-level interface for netlink operations
2. **Protocol Handler APIs** - Protocol-specific message handling
3. **Event Structure API** - Enhanced event structures for netlink data
4. **Filter API Extensions** - Netlink-specific filtering capabilities

## Netlink Manager API

The netlink manager provides the primary interface for netlink operations.

### Header

```c
#include "nlmon_netlink.h"
```

### Data Structures

#### nlmon_nl_manager

```c
struct nlmon_nl_manager {
    // libnl-tiny sockets
    struct nl_sock *route_sock;      // NETLINK_ROUTE
    struct nl_sock *genl_sock;       // NETLINK_GENERIC
    struct nl_sock *diag_sock;       // NETLINK_SOCK_DIAG
    struct nl_sock *nf_sock;         // NETLINK_NETFILTER
    
    // Generic netlink family IDs
    int nl80211_id;
    int taskstats_id;
    
    // Callbacks
    struct nl_cb *route_cb;
    struct nl_cb *genl_cb;
    struct nl_cb *diag_cb;
    struct nl_cb *nf_cb;
    
    // nlmon integration
    void (*event_callback)(struct nlmon_event *evt, void *user_data);
    void *user_data;
    
    // Configuration
    int enable_route;
    int enable_genl;
    int enable_diag;
    int enable_netfilter;
    
    // Optional caches
    struct nl_cache *link_cache;
    struct nl_cache *addr_cache;
    struct nl_cache *route_cache;
};
```

### Functions

#### nlmon_nl_manager_init

```c
struct nlmon_nl_manager *nlmon_nl_manager_init(void);
```

**Description**: Initialize the netlink manager and allocate resources.

**Returns**: 
- Pointer to initialized manager on success
- NULL on failure

**Example**:
```c
struct nlmon_nl_manager *mgr = nlmon_nl_manager_init();
if (!mgr) {
    fprintf(stderr, "Failed to initialize netlink manager\n");
    return -1;
}
```

---

#### nlmon_nl_manager_destroy

```c
void nlmon_nl_manager_destroy(struct nlmon_nl_manager *mgr);
```

**Description**: Clean up and free all netlink manager resources.

**Parameters**:
- `mgr` - Netlink manager to destroy

**Example**:
```c
nlmon_nl_manager_destroy(mgr);
```

---

#### nlmon_nl_enable_route

```c
int nlmon_nl_enable_route(struct nlmon_nl_manager *mgr);
```

**Description**: Enable NETLINK_ROUTE protocol monitoring for interface, address, route, and neighbor events.

**Parameters**:
- `mgr` - Netlink manager

**Returns**:
- 0 on success
- Negative error code on failure

**Example**:
```c
if (nlmon_nl_enable_route(mgr) < 0) {
    fprintf(stderr, "Failed to enable route protocol\n");
}
```

---

#### nlmon_nl_enable_generic

```c
int nlmon_nl_enable_generic(struct nlmon_nl_manager *mgr);
```

**Description**: Enable NETLINK_GENERIC protocol monitoring for nl80211 and other generic netlink families.

**Parameters**:
- `mgr` - Netlink manager

**Returns**:
- 0 on success
- Negative error code on failure

**Example**:
```c
if (nlmon_nl_enable_generic(mgr) < 0) {
    fprintf(stderr, "Failed to enable generic netlink\n");
}
```

---

#### nlmon_nl_enable_diag

```c
int nlmon_nl_enable_diag(struct nlmon_nl_manager *mgr);
```

**Description**: Enable NETLINK_SOCK_DIAG protocol for socket diagnostics.

**Parameters**:
- `mgr` - Netlink manager

**Returns**:
- 0 on success
- Negative error code on failure

**Example**:
```c
if (nlmon_nl_enable_diag(mgr) < 0) {
    fprintf(stderr, "Failed to enable socket diagnostics\n");
}
```

---

#### nlmon_nl_enable_netfilter

```c
int nlmon_nl_enable_netfilter(struct nlmon_nl_manager *mgr);
```

**Description**: Enable NETLINK_NETFILTER protocol for connection tracking and firewall events.

**Parameters**:
- `mgr` - Netlink manager

**Returns**:
- 0 on success
- Negative error code on failure

**Example**:
```c
if (nlmon_nl_enable_netfilter(mgr) < 0) {
    fprintf(stderr, "Failed to enable netfilter\n");
}
```

---

#### nlmon_nl_resolve_family

```c
int nlmon_nl_resolve_family(struct nlmon_nl_manager *mgr, const char *name);
```

**Description**: Resolve a generic netlink family name to its numeric ID.

**Parameters**:
- `mgr` - Netlink manager
- `name` - Family name (e.g., "nl80211")

**Returns**:
- Positive family ID on success
- Negative error code on failure

**Example**:
```c
int nl80211_id = nlmon_nl_resolve_family(mgr, "nl80211");
if (nl80211_id < 0) {
    fprintf(stderr, "Failed to resolve nl80211 family\n");
}
```

---

#### nlmon_nl_get_route_fd

```c
int nlmon_nl_get_route_fd(struct nlmon_nl_manager *mgr);
```

**Description**: Get file descriptor for NETLINK_ROUTE socket.

**Parameters**:
- `mgr` - Netlink manager

**Returns**:
- File descriptor on success
- -1 if protocol not enabled

**Example**:
```c
int fd = nlmon_nl_get_route_fd(mgr);
if (fd >= 0) {
    // Add to event loop
    poll_fds[0].fd = fd;
    poll_fds[0].events = POLLIN;
}
```

---

#### nlmon_nl_get_genl_fd

```c
int nlmon_nl_get_genl_fd(struct nlmon_nl_manager *mgr);
```

**Description**: Get file descriptor for NETLINK_GENERIC socket.

**Parameters**:
- `mgr` - Netlink manager

**Returns**:
- File descriptor on success
- -1 if protocol not enabled

---

#### nlmon_nl_get_diag_fd

```c
int nlmon_nl_get_diag_fd(struct nlmon_nl_manager *mgr);
```

**Description**: Get file descriptor for NETLINK_SOCK_DIAG socket.

**Parameters**:
- `mgr` - Netlink manager

**Returns**:
- File descriptor on success
- -1 if protocol not enabled

---

#### nlmon_nl_get_nf_fd

```c
int nlmon_nl_get_nf_fd(struct nlmon_nl_manager *mgr);
```

**Description**: Get file descriptor for NETLINK_NETFILTER socket.

**Parameters**:
- `mgr` - Netlink manager

**Returns**:
- File descriptor on success
- -1 if protocol not enabled

---

#### nlmon_nl_process_route

```c
int nlmon_nl_process_route(struct nlmon_nl_manager *mgr);
```

**Description**: Process pending NETLINK_ROUTE messages. Call when route socket has data available.

**Parameters**:
- `mgr` - Netlink manager

**Returns**:
- Number of messages processed
- Negative error code on failure

**Example**:
```c
if (poll_fds[0].revents & POLLIN) {
    int n = nlmon_nl_process_route(mgr);
    if (n < 0) {
        fprintf(stderr, "Error processing route messages\n");
    }
}
```

---

#### nlmon_nl_process_genl

```c
int nlmon_nl_process_genl(struct nlmon_nl_manager *mgr);
```

**Description**: Process pending NETLINK_GENERIC messages.

**Parameters**:
- `mgr` - Netlink manager

**Returns**:
- Number of messages processed
- Negative error code on failure

---

#### nlmon_nl_process_diag

```c
int nlmon_nl_process_diag(struct nlmon_nl_manager *mgr);
```

**Description**: Process pending NETLINK_SOCK_DIAG messages.

**Parameters**:
- `mgr` - Netlink manager

**Returns**:
- Number of messages processed
- Negative error code on failure

---

#### nlmon_nl_process_nf

```c
int nlmon_nl_process_nf(struct nlmon_nl_manager *mgr);
```

**Description**: Process pending NETLINK_NETFILTER messages.

**Parameters**:
- `mgr` - Netlink manager

**Returns**:
- Number of messages processed
- Negative error code on failure

---

#### nlmon_nl_set_callback

```c
void nlmon_nl_set_callback(struct nlmon_nl_manager *mgr,
                           void (*cb)(struct nlmon_event *, void *),
                           void *user_data);
```

**Description**: Set callback function for netlink events.

**Parameters**:
- `mgr` - Netlink manager
- `cb` - Callback function
- `user_data` - User data passed to callback

**Example**:
```c
void my_event_handler(struct nlmon_event *evt, void *user_data) {
    printf("Received netlink event: protocol=%d\n", evt->netlink.protocol);
}

nlmon_nl_set_callback(mgr, my_event_handler, NULL);
```

---

## Event Structure API

### Enhanced Event Structure

```c
struct nlmon_event {
    // Existing fields
    uint64_t timestamp;
    nlmon_event_type_t type;
    
    // Enhanced netlink fields
    struct {
        int protocol;                // NETLINK_ROUTE, NETLINK_GENERIC, etc.
        uint16_t msg_type;           // RTM_NEWLINK, etc.
        uint16_t msg_flags;          // NLM_F_* flags
        uint32_t seq;                // Sequence number
        uint32_t pid;                // Port ID
        
        // Generic netlink specific
        uint8_t genl_cmd;
        uint8_t genl_version;
        uint16_t genl_family_id;
        char genl_family_name[32];
        
        // Parsed attributes (protocol-specific)
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
    
    // Raw message (optional, for debugging)
    struct nlmsghdr *raw_msg;
    size_t raw_msg_len;
};
```

### Protocol-Specific Data Structures

#### nlmon_link_info

```c
struct nlmon_link_info {
    char ifname[IFNAMSIZ];           // Interface name
    int ifindex;                     // Interface index
    unsigned int flags;              // IFF_UP, IFF_RUNNING, etc.
    unsigned int mtu;                // Maximum transmission unit
    unsigned char addr[ETH_ALEN];    // MAC address
    char qdisc[32];                  // Queueing discipline
    int operstate;                   // Operational state
};
```

**Fields**:
- `ifname` - Interface name (e.g., "eth0", "wlan0")
- `ifindex` - Kernel interface index
- `flags` - Interface flags (IFF_UP, IFF_RUNNING, IFF_BROADCAST, etc.)
- `mtu` - Maximum transmission unit in bytes
- `addr` - Hardware (MAC) address
- `qdisc` - Queueing discipline (e.g., "fq_codel")
- `operstate` - Operational state (IF_OPER_UP, IF_OPER_DOWN, etc.)

---

#### nlmon_addr_info

```c
struct nlmon_addr_info {
    int family;                      // AF_INET, AF_INET6
    int ifindex;                     // Interface index
    unsigned char prefixlen;         // Prefix length
    unsigned char scope;             // Address scope
    char addr[64];                   // IP address string
    char label[IFNAMSIZ];            // Address label
};
```

**Fields**:
- `family` - Address family (AF_INET for IPv4, AF_INET6 for IPv6)
- `ifindex` - Interface index
- `prefixlen` - Prefix length (e.g., 24 for /24)
- `scope` - Address scope (RT_SCOPE_UNIVERSE, RT_SCOPE_LINK, etc.)
- `addr` - IP address as string (e.g., "192.168.1.100")
- `label` - Address label

---

#### nlmon_route_info

```c
struct nlmon_route_info {
    int family;                      // AF_INET, AF_INET6
    unsigned char dst_len;           // Destination prefix length
    unsigned char src_len;           // Source prefix length
    unsigned char tos;               // Type of service
    unsigned char protocol;          // Routing protocol
    unsigned char scope;             // Route scope
    unsigned char type;              // Route type
    char dst[64];                    // Destination address
    char src[64];                    // Source address
    char gateway[64];                // Gateway address
    int oif;                         // Output interface
    unsigned int priority;           // Route priority
};
```

---

#### nlmon_neigh_info

```c
struct nlmon_neigh_info {
    int family;                      // AF_INET, AF_INET6
    int ifindex;                     // Interface index
    uint16_t state;                  // Neighbor state
    uint8_t flags;                   // Neighbor flags
    char dst[64];                    // Neighbor IP address
    unsigned char lladdr[ETH_ALEN];  // Link-layer address
};
```

**Fields**:
- `family` - Address family
- `ifindex` - Interface index
- `state` - Neighbor state (NUD_REACHABLE, NUD_STALE, etc.)
- `flags` - Neighbor flags
- `dst` - Neighbor IP address
- `lladdr` - Link-layer (MAC) address

---

#### nlmon_diag_info

```c
struct nlmon_diag_info {
    uint8_t family;                  // AF_INET, AF_INET6
    uint8_t state;                   // Socket state
    uint8_t protocol;                // IPPROTO_TCP, IPPROTO_UDP
    uint16_t src_port;               // Source port
    uint16_t dst_port;               // Destination port
    char src_addr[64];               // Source address
    char dst_addr[64];               // Destination address
    uint32_t inode;                  // Socket inode
    uint32_t uid;                    // User ID
};
```

**Fields**:
- `family` - Address family
- `state` - Socket state (TCP_ESTABLISHED, TCP_LISTEN, etc.)
- `protocol` - Transport protocol
- `src_port` - Source port number
- `dst_port` - Destination port number
- `src_addr` - Source IP address
- `dst_addr` - Destination IP address
- `inode` - Socket inode number
- `uid` - User ID owning the socket

---

#### nlmon_ct_info

```c
struct nlmon_ct_info {
    uint8_t protocol;                // IPPROTO_TCP, IPPROTO_UDP, etc.
    uint8_t tcp_state;               // TCP connection state
    char src_addr[64];               // Source address
    char dst_addr[64];               // Destination address
    uint16_t src_port;               // Source port
    uint16_t dst_port;               // Destination port
    uint32_t mark;                   // Connection mark
    uint64_t packets_orig;           // Packets (original direction)
    uint64_t packets_reply;          // Packets (reply direction)
    uint64_t bytes_orig;             // Bytes (original direction)
    uint64_t bytes_reply;            // Bytes (reply direction)
};
```

**Fields**:
- `protocol` - IP protocol number
- `tcp_state` - TCP connection state (for TCP connections)
- `src_addr` - Source IP address
- `dst_addr` - Destination IP address
- `src_port` - Source port number
- `dst_port` - Destination port number
- `mark` - Netfilter connection mark
- `packets_orig` - Packet count in original direction
- `packets_reply` - Packet count in reply direction
- `bytes_orig` - Byte count in original direction
- `bytes_reply` - Byte count in reply direction

---

#### nlmon_nl80211_info

```c
struct nlmon_nl80211_info {
    uint8_t cmd;                     // nl80211 command
    int wiphy;                       // Wireless PHY index
    int ifindex;                     // Interface index
    char ifname[IFNAMSIZ];           // Interface name
    uint32_t iftype;                 // Interface type
    uint8_t mac[ETH_ALEN];           // MAC address
    uint32_t freq;                   // Frequency (MHz)
    uint32_t channel_type;           // Channel type
};
```

**Fields**:
- `cmd` - nl80211 command (NL80211_CMD_NEW_STATION, etc.)
- `wiphy` - Wireless PHY index
- `ifindex` - Interface index
- `ifname` - Interface name
- `iftype` - Interface type (NL80211_IFTYPE_STATION, etc.)
- `mac` - MAC address
- `freq` - Operating frequency in MHz
- `channel_type` - Channel type (HT20, HT40+, etc.)

---

## Filter API Extensions

### Filter Expression Syntax

Netlink-specific filter expressions use dot notation to access nested fields:

```
netlink.<field>
netlink.data.<protocol_type>.<field>
```

### Available Filter Fields

#### Common Netlink Fields

```c
netlink.protocol          // int: NETLINK_ROUTE, NETLINK_GENERIC, etc.
netlink.msg_type          // uint16_t: RTM_NEWLINK, etc.
netlink.msg_flags         // uint16_t: NLM_F_* flags
netlink.seq               // uint32_t: Sequence number
netlink.pid               // uint32_t: Port ID
```

#### Generic Netlink Fields

```c
netlink.genl_cmd          // uint8_t: Generic netlink command
netlink.genl_version      // uint8_t: Family version
netlink.genl_family_id    // uint16_t: Numeric family ID
netlink.genl_family_name  // string: Family name (e.g., "nl80211")
```

#### Protocol-Specific Fields

Access protocol-specific data through `netlink.data.<type>.<field>`:

```c
// Link (interface) fields
netlink.data.link.ifname
netlink.data.link.ifindex
netlink.data.link.flags
netlink.data.link.mtu
netlink.data.link.operstate

// Address fields
netlink.data.addr.family
netlink.data.addr.addr
netlink.data.addr.prefixlen
netlink.data.addr.scope

// Route fields
netlink.data.route.dst
netlink.data.route.gateway
netlink.data.route.oif
netlink.data.route.priority

// Socket diagnostic fields
netlink.data.diag.protocol
netlink.data.diag.state
netlink.data.diag.src_addr
netlink.data.diag.dst_addr
netlink.data.diag.src_port
netlink.data.diag.dst_port
netlink.data.diag.uid

// Connection tracking fields
netlink.data.conntrack.protocol
netlink.data.conntrack.tcp_state
netlink.data.conntrack.src_addr
netlink.data.conntrack.dst_addr
netlink.data.conntrack.bytes_orig

// nl80211 fields
netlink.data.nl80211.ifname
netlink.data.nl80211.freq
netlink.data.nl80211.iftype
```

### Filter Examples

```c
// Filter by protocol
"netlink.protocol == NETLINK_ROUTE"

// Filter by message type
"netlink.msg_type == RTM_NEWLINK"

// Filter by interface name
"netlink.data.link.ifname == 'wlan0'"

// Filter by IP address prefix
"netlink.data.addr.addr startswith '192.168.'"

// Filter by TCP state
"netlink.data.diag.state == TCP_ESTABLISHED"

// Filter by WiFi frequency (5GHz)
"netlink.data.nl80211.freq >= 5000"

// Complex filter
"netlink.protocol == NETLINK_NETFILTER && netlink.data.conntrack.bytes_orig > 1000000"
```

## Error Handling

### Error Codes

```c
enum nlmon_nl_error {
    NLMON_NL_SUCCESS = 0,
    NLMON_NL_ERR_NOMEM = -1,        // Out of memory
    NLMON_NL_ERR_INVAL = -2,        // Invalid argument
    NLMON_NL_ERR_AGAIN = -3,        // Try again
    NLMON_NL_ERR_PROTO = -4,        // Protocol error
    NLMON_NL_ERR_NOACCESS = -5,     // Access denied
    NLMON_NL_ERR_NOATTR = -6,       // Attribute not found
    NLMON_NL_ERR_PARSE = -7,        // Parse error
};
```

### Error Functions

#### nlmon_nl_strerror

```c
const char *nlmon_nl_strerror(int err);
```

**Description**: Get error string for error code.

**Parameters**:
- `err` - Error code

**Returns**: Error description string

**Example**:
```c
int ret = nlmon_nl_enable_route(mgr);
if (ret < 0) {
    fprintf(stderr, "Error: %s\n", nlmon_nl_strerror(ret));
}
```

---

## Complete Usage Example

```c
#include "nlmon_netlink.h"
#include "event_processor.h"
#include <poll.h>

void netlink_event_handler(struct nlmon_event *evt, void *user_data) {
    if (evt->netlink.protocol == NETLINK_ROUTE) {
        if (evt->netlink.msg_type == RTM_NEWLINK) {
            printf("Interface %s: state=%s\n",
                   evt->netlink.data.link.ifname,
                   (evt->netlink.data.link.flags & IFF_UP) ? "UP" : "DOWN");
        }
    }
}

int main(void) {
    // Initialize netlink manager
    struct nlmon_nl_manager *mgr = nlmon_nl_manager_init();
    if (!mgr) {
        fprintf(stderr, "Failed to initialize netlink manager\n");
        return 1;
    }
    
    // Enable protocols
    if (nlmon_nl_enable_route(mgr) < 0) {
        fprintf(stderr, "Failed to enable route protocol\n");
        goto cleanup;
    }
    
    if (nlmon_nl_enable_generic(mgr) < 0) {
        fprintf(stderr, "Failed to enable generic netlink\n");
        goto cleanup;
    }
    
    // Resolve nl80211 family
    int nl80211_id = nlmon_nl_resolve_family(mgr, "nl80211");
    if (nl80211_id < 0) {
        fprintf(stderr, "Failed to resolve nl80211 family\n");
    }
    
    // Set event callback
    nlmon_nl_set_callback(mgr, netlink_event_handler, NULL);
    
    // Set up event loop
    struct pollfd fds[2];
    fds[0].fd = nlmon_nl_get_route_fd(mgr);
    fds[0].events = POLLIN;
    fds[1].fd = nlmon_nl_get_genl_fd(mgr);
    fds[1].events = POLLIN;
    
    // Main event loop
    while (1) {
        int ret = poll(fds, 2, 1000);
        if (ret < 0) {
            perror("poll");
            break;
        }
        
        if (fds[0].revents & POLLIN) {
            nlmon_nl_process_route(mgr);
        }
        
        if (fds[1].revents & POLLIN) {
            nlmon_nl_process_genl(mgr);
        }
    }
    
cleanup:
    nlmon_nl_manager_destroy(mgr);
    return 0;
}
```

## Thread Safety

The netlink manager is **not thread-safe**. If using multiple threads:

1. Create separate manager instances per thread, or
2. Use external synchronization (mutex) around all API calls

## Performance Tips

1. **Batch Processing**: Process multiple messages per event loop iteration
2. **Buffer Sizing**: Increase socket buffer sizes for high-traffic scenarios
3. **Selective Protocols**: Only enable needed protocols
4. **Filter Early**: Apply filters before expensive processing
5. **Cache Usage**: Enable caching for frequently accessed data

## See Also

- [Netlink Integration Guide](NETLINK_INTEGRATION.md)
- [Netlink Migration Guide](NETLINK_MIGRATION.md)
- [Filter Documentation](NETLINK_FILTERING.md)
- [Configuration Guide](README.md)

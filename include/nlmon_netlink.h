#ifndef NLMON_NETLINK_H
#define NLMON_NETLINK_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdint.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netlink/netlink.h>
#include <netlink/socket.h>
#include <netlink/msg.h>
#include <netlink/handlers.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Logging levels for netlink operations */
enum nlmon_nl_log_level {
	NLMON_NL_LOG_ERROR = 0,   /* Error messages */
	NLMON_NL_LOG_WARN = 1,    /* Warning messages */
	NLMON_NL_LOG_INFO = 2,    /* Informational messages */
	NLMON_NL_LOG_DEBUG = 3,   /* Debug messages */
};

/* Set logging level for netlink operations */
void nlmon_nl_set_log_level(enum nlmon_nl_log_level level);

/* Get current logging level */
enum nlmon_nl_log_level nlmon_nl_get_log_level(void);

/* Enable/disable message dumping on parse errors */
void nlmon_nl_set_dump_on_error(int enable);

/* Log netlink error with context */
void nlmon_nl_log_error(const char *context, int error);

/* Log netlink message details (for debugging) */
void nlmon_nl_log_message(struct nlmsghdr *nlh, const char *direction);

/* Dump netlink message in hex format */
void nlmon_nl_dump_message(struct nlmsghdr *nlh, size_t len);

/* Forward declarations */
struct nlmon_event;
struct nlmon_netlink_config;

/**
 * nlmon netlink manager structure
 * 
 * Central coordinator for all netlink operations using libnl-tiny.
 * Manages sockets for multiple netlink protocols and routes messages
 * to the nlmon event processor.
 */
struct nlmon_nl_manager {
	/* libnl-tiny sockets for each protocol */
	struct nl_sock *route_sock;      /* NETLINK_ROUTE */
	struct nl_sock *genl_sock;       /* NETLINK_GENERIC */
	struct nl_sock *diag_sock;       /* NETLINK_SOCK_DIAG */
	struct nl_sock *nf_sock;         /* NETLINK_NETFILTER */
	
	/* Callback handlers for each protocol */
	struct nl_cb *route_cb;
	struct nl_cb *genl_cb;
	struct nl_cb *diag_cb;
	struct nl_cb *nf_cb;
	
	/* Generic netlink family IDs (resolved dynamically) */
	int nl80211_id;
	int taskstats_id;
	
	/* nlmon integration - callback to event processor */
	void (*event_callback)(struct nlmon_event *evt, void *user_data);
	void *user_data;
	
	/* Protocol enable flags */
	int enable_route;
	int enable_genl;
	int enable_diag;
	int enable_netfilter;
	
	/* Optional caches for performance */
	struct nl_cache *link_cache;
	struct nl_cache *addr_cache;
	struct nl_cache *route_cache;
};

/**
 * Initialize netlink manager
 * 
 * Allocates and initializes the netlink manager structure.
 * Does not create sockets - use enable functions to activate protocols.
 * 
 * @return Pointer to initialized manager, or NULL on error
 */
struct nlmon_nl_manager *nlmon_nl_manager_init(void);

/**
 * Destroy netlink manager and free resources
 * 
 * Closes all sockets, frees callbacks and caches, and deallocates
 * the manager structure.
 * 
 * @param mgr Netlink manager to destroy
 */
void nlmon_nl_manager_destroy(struct nlmon_nl_manager *mgr);

/**
 * Enable NETLINK_ROUTE protocol
 * 
 * Creates socket, connects to NETLINK_ROUTE, subscribes to multicast
 * groups for link, address, route, and neighbor events.
 * 
 * @param mgr Netlink manager
 * @return 0 on success, negative error code on failure
 */
int nlmon_nl_enable_route(struct nlmon_nl_manager *mgr);

/**
 * Enable NETLINK_GENERIC protocol
 * 
 * Creates socket, connects to NETLINK_GENERIC, resolves common
 * family IDs (nl80211, taskstats).
 * 
 * @param mgr Netlink manager
 * @return 0 on success, negative error code on failure
 */
int nlmon_nl_enable_generic(struct nlmon_nl_manager *mgr);

/**
 * Enable NETLINK_SOCK_DIAG protocol
 * 
 * Creates socket and connects to NETLINK_SOCK_DIAG for socket
 * diagnostics monitoring.
 * 
 * @param mgr Netlink manager
 * @return 0 on success, negative error code on failure
 */
int nlmon_nl_enable_diag(struct nlmon_nl_manager *mgr);

/**
 * Enable NETLINK_NETFILTER protocol
 * 
 * Creates socket and connects to NETLINK_NETFILTER for connection
 * tracking and firewall events.
 * 
 * @param mgr Netlink manager
 * @return 0 on success, negative error code on failure
 */
int nlmon_nl_enable_netfilter(struct nlmon_nl_manager *mgr);

/**
 * Resolve generic netlink family name to ID
 * 
 * Queries the kernel's generic netlink controller to resolve
 * a family name to its numeric ID. Results are cached in the manager.
 * 
 * @param mgr Netlink manager
 * @param name Family name (e.g., "nl80211")
 * @return Family ID on success, negative error code on failure
 */
int nlmon_nl_resolve_family(struct nlmon_nl_manager *mgr, const char *name);

/**
 * Get file descriptor for NETLINK_ROUTE socket
 * 
 * @param mgr Netlink manager
 * @return File descriptor, or -1 if protocol not enabled
 */
int nlmon_nl_get_route_fd(struct nlmon_nl_manager *mgr);

/**
 * Get file descriptor for NETLINK_GENERIC socket
 * 
 * @param mgr Netlink manager
 * @return File descriptor, or -1 if protocol not enabled
 */
int nlmon_nl_get_genl_fd(struct nlmon_nl_manager *mgr);

/**
 * Get file descriptor for NETLINK_SOCK_DIAG socket
 * 
 * @param mgr Netlink manager
 * @return File descriptor, or -1 if protocol not enabled
 */
int nlmon_nl_get_diag_fd(struct nlmon_nl_manager *mgr);

/**
 * Get file descriptor for NETLINK_NETFILTER socket
 * 
 * @param mgr Netlink manager
 * @return File descriptor, or -1 if protocol not enabled
 */
int nlmon_nl_get_nf_fd(struct nlmon_nl_manager *mgr);

/**
 * Process messages from NETLINK_ROUTE socket
 * 
 * Receives and processes messages using nl_recvmsgs().
 * Handles EAGAIN/EWOULDBLOCK for non-blocking operation.
 * 
 * @param mgr Netlink manager
 * @return 0 on success, negative error code on failure
 */
int nlmon_nl_process_route(struct nlmon_nl_manager *mgr);

/**
 * Process messages from NETLINK_GENERIC socket
 * 
 * Receives and processes messages using nl_recvmsgs().
 * Handles EAGAIN/EWOULDBLOCK for non-blocking operation.
 * 
 * @param mgr Netlink manager
 * @return 0 on success, negative error code on failure
 */
int nlmon_nl_process_genl(struct nlmon_nl_manager *mgr);

/**
 * Process messages from NETLINK_SOCK_DIAG socket
 * 
 * Receives and processes messages using nl_recvmsgs().
 * Handles EAGAIN/EWOULDBLOCK for non-blocking operation.
 * 
 * @param mgr Netlink manager
 * @return 0 on success, negative error code on failure
 */
int nlmon_nl_process_diag(struct nlmon_nl_manager *mgr);

/**
 * Process messages from NETLINK_NETFILTER socket
 * 
 * Receives and processes messages using nl_recvmsgs().
 * Handles EAGAIN/EWOULDBLOCK for non-blocking operation.
 * 
 * @param mgr Netlink manager
 * @return 0 on success, negative error code on failure
 */
int nlmon_nl_process_nf(struct nlmon_nl_manager *mgr);

/**
 * Set event callback for netlink events
 * 
 * Configures the callback function that will be invoked when
 * netlink messages are translated to nlmon events.
 * 
 * @param mgr Netlink manager
 * @param cb Callback function
 * @param user_data User data passed to callback
 */
void nlmon_nl_set_callback(struct nlmon_nl_manager *mgr,
                           void (*cb)(struct nlmon_event *, void *),
                           void *user_data);

/**
 * Apply configuration to netlink manager
 * 
 * Configures the netlink manager based on the provided configuration.
 * Enables/disables protocols, sets buffer sizes, initializes caches,
 * and subscribes to multicast groups as specified.
 * 
 * @param mgr Netlink manager
 * @param config Netlink configuration to apply
 * @return 0 on success, negative error code on failure
 */
int nlmon_nl_apply_config(struct nlmon_nl_manager *mgr,
                          const struct nlmon_netlink_config *config);

/**
 * Reconnect a netlink socket after connection loss
 * 
 * Attempts to reconnect a netlink socket that has been disconnected.
 * This function will close the existing socket and create a new one.
 * 
 * @param mgr Netlink manager
 * @param protocol Protocol to reconnect (NETLINK_ROUTE, NETLINK_GENERIC, etc.)
 * @return 0 on success, negative error code on failure
 */
int nlmon_nl_reconnect(struct nlmon_nl_manager *mgr, int protocol);

/**
 * Check if a netlink socket is still connected
 * 
 * Verifies that a netlink socket is still valid and connected.
 * 
 * @param mgr Netlink manager
 * @param protocol Protocol to check
 * @return 1 if connected, 0 if not connected, negative on error
 */
int nlmon_nl_is_connected(struct nlmon_nl_manager *mgr, int protocol);

/**
 * Handle message parse error
 * 
 * Called when a message parse error occurs. Logs the error and
 * optionally dumps the message for debugging.
 * 
 * @param mgr Netlink manager
 * @param msg Netlink message that failed to parse
 * @param error Error code from parse operation
 * @return 0 to continue processing, negative to abort
 */
int nlmon_nl_handle_parse_error(struct nlmon_nl_manager *mgr,
                                 struct nl_msg *msg,
                                 int error);

/**
 * Initialize link cache
 * 
 * Creates and populates a cache of network interfaces (links).
 * The cache is automatically updated when link events are received.
 * 
 * @param mgr Netlink manager
 * @return 0 on success, negative error code on failure
 */
int nlmon_nl_cache_init_link(struct nlmon_nl_manager *mgr);

/**
 * Initialize address cache
 * 
 * Creates and populates a cache of IP addresses.
 * The cache is automatically updated when address events are received.
 * 
 * @param mgr Netlink manager
 * @return 0 on success, negative error code on failure
 */
int nlmon_nl_cache_init_addr(struct nlmon_nl_manager *mgr);

/**
 * Initialize route cache (optional)
 * 
 * Creates and populates a cache of routing table entries.
 * The cache is automatically updated when route events are received.
 * 
 * @param mgr Netlink manager
 * @return 0 on success, negative error code on failure
 */
int nlmon_nl_cache_init_route(struct nlmon_nl_manager *mgr);

/**
 * Refresh all initialized caches
 * 
 * Requests a full dump from the kernel to refresh all active caches.
 * 
 * @param mgr Netlink manager
 * @return 0 on success, negative error code on failure
 */
int nlmon_nl_cache_refresh_all(struct nlmon_nl_manager *mgr);

/**
 * Update link cache with a netlink message
 * 
 * Called when RTM_NEWLINK or RTM_DELLINK messages are received.
 * 
 * @param mgr Netlink manager
 * @param msg Netlink message
 * @return 0 on success, negative error code on failure
 */
int nlmon_nl_cache_update_link(struct nlmon_nl_manager *mgr, struct nl_msg *msg);

/**
 * Update address cache with a netlink message
 * 
 * Called when RTM_NEWADDR or RTM_DELADDR messages are received.
 * 
 * @param mgr Netlink manager
 * @param msg Netlink message
 * @return 0 on success, negative error code on failure
 */
int nlmon_nl_cache_update_addr(struct nlmon_nl_manager *mgr, struct nl_msg *msg);

/**
 * Update route cache with a netlink message
 * 
 * Called when RTM_NEWROUTE or RTM_DELROUTE messages are received.
 * 
 * @param mgr Netlink manager
 * @param msg Netlink message
 * @return 0 on success, negative error code on failure
 */
int nlmon_nl_cache_update_route(struct nlmon_nl_manager *mgr, struct nl_msg *msg);

/**
 * Get link information by interface index
 * 
 * Searches the link cache for an interface with the specified index.
 * 
 * @param mgr Netlink manager
 * @param ifindex Interface index
 * @param ifname Buffer to store interface name (at least IFNAMSIZ bytes)
 * @return 0 on success, -ENOENT if not found, negative error code on failure
 */
int nlmon_nl_get_link_by_index(struct nlmon_nl_manager *mgr, int ifindex, char *ifname);

/**
 * Get link information by interface name
 * 
 * Searches the link cache for an interface with the specified name.
 * 
 * @param mgr Netlink manager
 * @param ifname Interface name
 * @param ifindex Pointer to store interface index
 * @return 0 on success, -ENOENT if not found, negative error code on failure
 */
int nlmon_nl_get_link_by_name(struct nlmon_nl_manager *mgr, const char *ifname, int *ifindex);

/**
 * Get number of items in link cache
 * 
 * @param mgr Netlink manager
 * @return Number of cached links, or -1 if cache not initialized
 */
int nlmon_nl_get_link_count(struct nlmon_nl_manager *mgr);

/**
 * Get number of items in address cache
 * 
 * @param mgr Netlink manager
 * @return Number of cached addresses, or -1 if cache not initialized
 */
int nlmon_nl_get_addr_count(struct nlmon_nl_manager *mgr);

/**
 * Get number of items in route cache
 * 
 * @param mgr Netlink manager
 * @return Number of cached routes, or -1 if cache not initialized
 */
int nlmon_nl_get_route_count(struct nlmon_nl_manager *mgr);

#ifdef __cplusplus
}
#endif

#endif /* NLMON_NETLINK_H */

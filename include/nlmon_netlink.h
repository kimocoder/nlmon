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

/* Forward declarations */
struct nlmon_event;

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

#ifdef __cplusplus
}
#endif

#endif /* NLMON_NETLINK_H */

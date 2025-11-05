#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/genetlink.h>

#include <netlink/netlink.h>
#include <netlink/socket.h>
#include <netlink/msg.h>
#include <netlink/handlers.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>

#include "nlmon_netlink.h"
#include "nlmon_nl_route.h"
#include "nlmon_nl_genl.h"
#include "nlmon_nl_diag.h"
#include "nlmon_nl_netfilter.h"

/* Forward declarations of message handlers */
extern int nlmon_route_msg_handler(struct nl_msg *msg, void *arg);
extern int nlmon_genl_msg_handler(struct nl_msg *msg, void *arg);
extern int nlmon_diag_msg_handler(struct nl_msg *msg, void *arg);
extern int nlmon_nf_msg_handler(struct nl_msg *msg, void *arg);

/**
 * Initialize netlink manager
 */
struct nlmon_nl_manager *nlmon_nl_manager_init(void)
{
	struct nlmon_nl_manager *mgr;
	
	mgr = calloc(1, sizeof(*mgr));
	if (!mgr) {
		fprintf(stderr, "Failed to allocate netlink manager\n");
		return NULL;
	}
	
	/* Initialize all sockets to NULL */
	mgr->route_sock = NULL;
	mgr->genl_sock = NULL;
	mgr->diag_sock = NULL;
	mgr->nf_sock = NULL;
	
	/* Initialize all callbacks to NULL */
	mgr->route_cb = NULL;
	mgr->genl_cb = NULL;
	mgr->diag_cb = NULL;
	mgr->nf_cb = NULL;
	
	/* Initialize family IDs to -1 (unresolved) */
	mgr->nl80211_id = -1;
	mgr->taskstats_id = -1;
	
	/* Initialize event callback */
	mgr->event_callback = NULL;
	mgr->user_data = NULL;
	
	/* All protocols disabled by default */
	mgr->enable_route = 0;
	mgr->enable_genl = 0;
	mgr->enable_diag = 0;
	mgr->enable_netfilter = 0;
	
	/* No caches by default */
	mgr->link_cache = NULL;
	mgr->addr_cache = NULL;
	mgr->route_cache = NULL;
	
	return mgr;
}

/**
 * Destroy netlink manager and free all resources
 */
void nlmon_nl_manager_destroy(struct nlmon_nl_manager *mgr)
{
	if (!mgr)
		return;
	
	/* Free caches */
	if (mgr->link_cache)
		nl_cache_free(mgr->link_cache);
	if (mgr->addr_cache)
		nl_cache_free(mgr->addr_cache);
	if (mgr->route_cache)
		nl_cache_free(mgr->route_cache);
	
	/* Free callbacks */
	if (mgr->route_cb)
		nl_cb_put(mgr->route_cb);
	if (mgr->genl_cb)
		nl_cb_put(mgr->genl_cb);
	if (mgr->diag_cb)
		nl_cb_put(mgr->diag_cb);
	if (mgr->nf_cb)
		nl_cb_put(mgr->nf_cb);
	
	/* Close and free sockets */
	if (mgr->route_sock) {
		nl_close(mgr->route_sock);
		nl_socket_free(mgr->route_sock);
	}
	if (mgr->genl_sock) {
		nl_close(mgr->genl_sock);
		nl_socket_free(mgr->genl_sock);
	}
	if (mgr->diag_sock) {
		nl_close(mgr->diag_sock);
		nl_socket_free(mgr->diag_sock);
	}
	if (mgr->nf_sock) {
		nl_close(mgr->nf_sock);
		nl_socket_free(mgr->nf_sock);
	}
	
	/* Free manager structure */
	free(mgr);
}

/**
 * Enable NETLINK_ROUTE protocol
 */
int nlmon_nl_enable_route(struct nlmon_nl_manager *mgr)
{
	int ret;
	
	if (!mgr)
		return -EINVAL;
	
	/* Already enabled */
	if (mgr->route_sock && mgr->enable_route)
		return 0;
	
	/* Allocate socket */
	mgr->route_sock = nl_socket_alloc();
	if (!mgr->route_sock) {
		fprintf(stderr, "Failed to allocate NETLINK_ROUTE socket\n");
		return -ENOMEM;
	}
	
	/* Disable sequence number checking for asynchronous events */
	nl_socket_disable_seq_check(mgr->route_sock);
	
	/* Connect to NETLINK_ROUTE */
	ret = nl_connect(mgr->route_sock, NETLINK_ROUTE);
	if (ret < 0) {
		fprintf(stderr, "Failed to connect to NETLINK_ROUTE: %s\n",
		        nl_geterror(ret));
		nl_socket_free(mgr->route_sock);
		mgr->route_sock = NULL;
		return ret;
	}
	
	/* Increase buffer sizes for high-traffic scenarios */
	ret = nl_socket_set_buffer_size(mgr->route_sock, 32768, 32768);
	if (ret < 0) {
		fprintf(stderr, "Warning: Failed to set NETLINK_ROUTE buffer size: %s\n",
		        nl_geterror(ret));
		/* Non-fatal, continue */
	}
	
	/* Set socket to non-blocking mode */
	ret = nl_socket_set_nonblocking(mgr->route_sock);
	if (ret < 0) {
		fprintf(stderr, "Failed to set NETLINK_ROUTE socket non-blocking: %s\n",
		        nl_geterror(ret));
		nl_close(mgr->route_sock);
		nl_socket_free(mgr->route_sock);
		mgr->route_sock = NULL;
		return ret;
	}
	
	/* Subscribe to multicast groups */
	ret = nl_socket_add_memberships(mgr->route_sock,
	                                 RTNLGRP_LINK,
	                                 RTNLGRP_NEIGH,
	                                 RTNLGRP_IPV4_IFADDR,
	                                 RTNLGRP_IPV4_ROUTE,
	                                 RTNLGRP_IPV4_RULE,
	                                 RTNLGRP_IPV6_IFADDR,
	                                 RTNLGRP_IPV6_ROUTE,
	                                 RTNLGRP_IPV6_RULE,
	                                 0);
	if (ret < 0) {
		fprintf(stderr, "Failed to join NETLINK_ROUTE multicast groups: %s\n",
		        nl_geterror(ret));
		nl_close(mgr->route_sock);
		nl_socket_free(mgr->route_sock);
		mgr->route_sock = NULL;
		return ret;
	}
	
	/* Allocate callback structure */
	mgr->route_cb = nl_cb_alloc(NL_CB_DEFAULT);
	if (!mgr->route_cb) {
		fprintf(stderr, "Failed to allocate NETLINK_ROUTE callback\n");
		nl_close(mgr->route_sock);
		nl_socket_free(mgr->route_sock);
		mgr->route_sock = NULL;
		return -ENOMEM;
	}
	
	/* Set up route message callback handler */
	nl_cb_set(mgr->route_cb, NL_CB_VALID, NL_CB_CUSTOM, nlmon_route_msg_handler, mgr);
	
	mgr->enable_route = 1;
	
	return 0;
}

/**
 * Enable NETLINK_GENERIC protocol
 */
int nlmon_nl_enable_generic(struct nlmon_nl_manager *mgr)
{
	int ret;
	
	if (!mgr)
		return -EINVAL;
	
	/* Already enabled */
	if (mgr->genl_sock && mgr->enable_genl)
		return 0;
	
	/* Allocate socket */
	mgr->genl_sock = nl_socket_alloc();
	if (!mgr->genl_sock) {
		fprintf(stderr, "Failed to allocate NETLINK_GENERIC socket\n");
		return -ENOMEM;
	}
	
	/* Disable sequence number checking */
	nl_socket_disable_seq_check(mgr->genl_sock);
	
	/* Connect to NETLINK_GENERIC */
	ret = genl_connect(mgr->genl_sock);
	if (ret < 0) {
		fprintf(stderr, "Failed to connect to NETLINK_GENERIC: %s\n",
		        nl_geterror(ret));
		nl_socket_free(mgr->genl_sock);
		mgr->genl_sock = NULL;
		return ret;
	}
	
	/* Increase buffer sizes */
	ret = nl_socket_set_buffer_size(mgr->genl_sock, 32768, 32768);
	if (ret < 0) {
		fprintf(stderr, "Warning: Failed to set NETLINK_GENERIC buffer size: %s\n",
		        nl_geterror(ret));
		/* Non-fatal, continue */
	}
	
	/* Set socket to non-blocking mode */
	ret = nl_socket_set_nonblocking(mgr->genl_sock);
	if (ret < 0) {
		fprintf(stderr, "Failed to set NETLINK_GENERIC socket non-blocking: %s\n",
		        nl_geterror(ret));
		nl_close(mgr->genl_sock);
		nl_socket_free(mgr->genl_sock);
		mgr->genl_sock = NULL;
		return ret;
	}
	
	/* Allocate callback structure */
	mgr->genl_cb = nl_cb_alloc(NL_CB_DEFAULT);
	if (!mgr->genl_cb) {
		fprintf(stderr, "Failed to allocate NETLINK_GENERIC callback\n");
		nl_close(mgr->genl_sock);
		nl_socket_free(mgr->genl_sock);
		mgr->genl_sock = NULL;
		return -ENOMEM;
	}
	
	/* Set up generic netlink message callback handler */
	nl_cb_set(mgr->genl_cb, NL_CB_VALID, NL_CB_CUSTOM, nlmon_genl_msg_handler, mgr);
	
	mgr->enable_genl = 1;
	
	return 0;
}

/**
 * Enable NETLINK_SOCK_DIAG protocol
 */
int nlmon_nl_enable_diag(struct nlmon_nl_manager *mgr)
{
	int ret;
	
	if (!mgr)
		return -EINVAL;
	
	/* Already enabled */
	if (mgr->diag_sock && mgr->enable_diag)
		return 0;
	
	/* Allocate socket */
	mgr->diag_sock = nl_socket_alloc();
	if (!mgr->diag_sock) {
		fprintf(stderr, "Failed to allocate NETLINK_SOCK_DIAG socket\n");
		return -ENOMEM;
	}
	
	/* Disable sequence number checking */
	nl_socket_disable_seq_check(mgr->diag_sock);
	
	/* Connect to NETLINK_SOCK_DIAG */
	ret = nl_connect(mgr->diag_sock, NETLINK_SOCK_DIAG);
	if (ret < 0) {
		fprintf(stderr, "Failed to connect to NETLINK_SOCK_DIAG: %s\n",
		        nl_geterror(ret));
		nl_socket_free(mgr->diag_sock);
		mgr->diag_sock = NULL;
		return ret;
	}
	
	/* Increase buffer sizes */
	ret = nl_socket_set_buffer_size(mgr->diag_sock, 32768, 32768);
	if (ret < 0) {
		fprintf(stderr, "Warning: Failed to set NETLINK_SOCK_DIAG buffer size: %s\n",
		        nl_geterror(ret));
		/* Non-fatal, continue */
	}
	
	/* Set socket to non-blocking mode */
	ret = nl_socket_set_nonblocking(mgr->diag_sock);
	if (ret < 0) {
		fprintf(stderr, "Failed to set NETLINK_SOCK_DIAG socket non-blocking: %s\n",
		        nl_geterror(ret));
		nl_close(mgr->diag_sock);
		nl_socket_free(mgr->diag_sock);
		mgr->diag_sock = NULL;
		return ret;
	}
	
	/* Allocate callback structure */
	mgr->diag_cb = nl_cb_alloc(NL_CB_DEFAULT);
	if (!mgr->diag_cb) {
		fprintf(stderr, "Failed to allocate NETLINK_SOCK_DIAG callback\n");
		nl_close(mgr->diag_sock);
		nl_socket_free(mgr->diag_sock);
		mgr->diag_sock = NULL;
		return -ENOMEM;
	}
	
	/* Set up socket diagnostics message callback handler */
	nl_cb_set(mgr->diag_cb, NL_CB_VALID, NL_CB_CUSTOM, nlmon_diag_msg_handler, mgr);
	
	mgr->enable_diag = 1;
	
	return 0;
}

/**
 * Enable NETLINK_NETFILTER protocol
 */
int nlmon_nl_enable_netfilter(struct nlmon_nl_manager *mgr)
{
	int ret;
	
	if (!mgr)
		return -EINVAL;
	
	/* Already enabled */
	if (mgr->nf_sock && mgr->enable_netfilter)
		return 0;
	
	/* Allocate socket */
	mgr->nf_sock = nl_socket_alloc();
	if (!mgr->nf_sock) {
		fprintf(stderr, "Failed to allocate NETLINK_NETFILTER socket\n");
		return -ENOMEM;
	}
	
	/* Disable sequence number checking */
	nl_socket_disable_seq_check(mgr->nf_sock);
	
	/* Connect to NETLINK_NETFILTER */
	ret = nl_connect(mgr->nf_sock, NETLINK_NETFILTER);
	if (ret < 0) {
		fprintf(stderr, "Failed to connect to NETLINK_NETFILTER: %s\n",
		        nl_geterror(ret));
		nl_socket_free(mgr->nf_sock);
		mgr->nf_sock = NULL;
		return ret;
	}
	
	/* Increase buffer sizes */
	ret = nl_socket_set_buffer_size(mgr->nf_sock, 32768, 32768);
	if (ret < 0) {
		fprintf(stderr, "Warning: Failed to set NETLINK_NETFILTER buffer size: %s\n",
		        nl_geterror(ret));
		/* Non-fatal, continue */
	}
	
	/* Set socket to non-blocking mode */
	ret = nl_socket_set_nonblocking(mgr->nf_sock);
	if (ret < 0) {
		fprintf(stderr, "Failed to set NETLINK_NETFILTER socket non-blocking: %s\n",
		        nl_geterror(ret));
		nl_close(mgr->nf_sock);
		nl_socket_free(mgr->nf_sock);
		mgr->nf_sock = NULL;
		return ret;
	}
	
	/* Allocate callback structure */
	mgr->nf_cb = nl_cb_alloc(NL_CB_DEFAULT);
	if (!mgr->nf_cb) {
		fprintf(stderr, "Failed to allocate NETLINK_NETFILTER callback\n");
		nl_close(mgr->nf_sock);
		nl_socket_free(mgr->nf_sock);
		mgr->nf_sock = NULL;
		return -ENOMEM;
	}
	
	/* Set up netfilter message callback handler */
	nl_cb_set(mgr->nf_cb, NL_CB_VALID, NL_CB_CUSTOM, nlmon_nf_msg_handler, mgr);
	
	mgr->enable_netfilter = 1;
	
	return 0;
}

/**
 * Resolve generic netlink family name to ID
 */
int nlmon_nl_resolve_family(struct nlmon_nl_manager *mgr, const char *name)
{
	int family_id;
	
	if (!mgr || !name)
		return -EINVAL;
	
	if (!mgr->genl_sock || !mgr->enable_genl)
		return -ENOTCONN;
	
	/* Resolve family name */
	family_id = genl_ctrl_resolve(mgr->genl_sock, name);
	if (family_id < 0) {
		fprintf(stderr, "Failed to resolve generic netlink family '%s': %s\n",
		        name, nl_geterror(family_id));
		return family_id;
	}
	
	/* Cache common family IDs */
	if (strcmp(name, "nl80211") == 0) {
		mgr->nl80211_id = family_id;
	} else if (strcmp(name, "taskstats") == 0) {
		mgr->taskstats_id = family_id;
	}
	
	return family_id;
}

/**
 * Get file descriptor for NETLINK_ROUTE socket
 */
int nlmon_nl_get_route_fd(struct nlmon_nl_manager *mgr)
{
	if (!mgr || !mgr->route_sock || !mgr->enable_route)
		return -1;
	
	return nl_socket_get_fd(mgr->route_sock);
}

/**
 * Get file descriptor for NETLINK_GENERIC socket
 */
int nlmon_nl_get_genl_fd(struct nlmon_nl_manager *mgr)
{
	if (!mgr || !mgr->genl_sock || !mgr->enable_genl)
		return -1;
	
	return nl_socket_get_fd(mgr->genl_sock);
}

/**
 * Get file descriptor for NETLINK_SOCK_DIAG socket
 */
int nlmon_nl_get_diag_fd(struct nlmon_nl_manager *mgr)
{
	if (!mgr || !mgr->diag_sock || !mgr->enable_diag)
		return -1;
	
	return nl_socket_get_fd(mgr->diag_sock);
}

/**
 * Get file descriptor for NETLINK_NETFILTER socket
 */
int nlmon_nl_get_nf_fd(struct nlmon_nl_manager *mgr)
{
	if (!mgr || !mgr->nf_sock || !mgr->enable_netfilter)
		return -1;
	
	return nl_socket_get_fd(mgr->nf_sock);
}

/**
 * Process messages from NETLINK_ROUTE socket
 */
int nlmon_nl_process_route(struct nlmon_nl_manager *mgr)
{
	int ret;
	
	if (!mgr || !mgr->route_sock || !mgr->enable_route)
		return -EINVAL;
	
	if (!mgr->route_cb)
		return -EINVAL;
	
	/* Receive and process messages using callbacks */
	ret = nl_recvmsgs(mgr->route_sock, mgr->route_cb);
	
	/* Handle non-blocking socket - no data available is not an error */
	if (ret < 0 && (ret == -NLE_AGAIN || errno == EAGAIN || errno == EWOULDBLOCK))
		return 0;
	
	if (ret < 0) {
		fprintf(stderr, "Error processing NETLINK_ROUTE messages: %s\n",
		        nl_geterror(ret));
		return ret;
	}
	
	return 0;
}

/**
 * Process messages from NETLINK_GENERIC socket
 */
int nlmon_nl_process_genl(struct nlmon_nl_manager *mgr)
{
	int ret;
	
	if (!mgr || !mgr->genl_sock || !mgr->enable_genl)
		return -EINVAL;
	
	if (!mgr->genl_cb)
		return -EINVAL;
	
	/* Receive and process messages using callbacks */
	ret = nl_recvmsgs(mgr->genl_sock, mgr->genl_cb);
	
	/* Handle non-blocking socket - no data available is not an error */
	if (ret < 0 && (ret == -NLE_AGAIN || errno == EAGAIN || errno == EWOULDBLOCK))
		return 0;
	
	if (ret < 0) {
		fprintf(stderr, "Error processing NETLINK_GENERIC messages: %s\n",
		        nl_geterror(ret));
		return ret;
	}
	
	return 0;
}

/**
 * Process messages from NETLINK_SOCK_DIAG socket
 */
int nlmon_nl_process_diag(struct nlmon_nl_manager *mgr)
{
	int ret;
	
	if (!mgr || !mgr->diag_sock || !mgr->enable_diag)
		return -EINVAL;
	
	if (!mgr->diag_cb)
		return -EINVAL;
	
	/* Receive and process messages using callbacks */
	ret = nl_recvmsgs(mgr->diag_sock, mgr->diag_cb);
	
	/* Handle non-blocking socket - no data available is not an error */
	if (ret < 0 && (ret == -NLE_AGAIN || errno == EAGAIN || errno == EWOULDBLOCK))
		return 0;
	
	if (ret < 0) {
		fprintf(stderr, "Error processing NETLINK_SOCK_DIAG messages: %s\n",
		        nl_geterror(ret));
		return ret;
	}
	
	return 0;
}

/**
 * Process messages from NETLINK_NETFILTER socket
 */
int nlmon_nl_process_nf(struct nlmon_nl_manager *mgr)
{
	int ret;
	
	if (!mgr || !mgr->nf_sock || !mgr->enable_netfilter)
		return -EINVAL;
	
	if (!mgr->nf_cb)
		return -EINVAL;
	
	/* Receive and process messages using callbacks */
	ret = nl_recvmsgs(mgr->nf_sock, mgr->nf_cb);
	
	/* Handle non-blocking socket - no data available is not an error */
	if (ret < 0 && (ret == -NLE_AGAIN || errno == EAGAIN || errno == EWOULDBLOCK))
		return 0;
	
	if (ret < 0) {
		fprintf(stderr, "Error processing NETLINK_NETFILTER messages: %s\n",
		        nl_geterror(ret));
		return ret;
	}
	
	return 0;
}

/**
 * Set event callback for netlink events
 */
void nlmon_nl_set_callback(struct nlmon_nl_manager *mgr,
                           void (*cb)(struct nlmon_event *, void *),
                           void *user_data)
{
	if (!mgr)
		return;
	
	mgr->event_callback = cb;
	mgr->user_data = user_data;
}

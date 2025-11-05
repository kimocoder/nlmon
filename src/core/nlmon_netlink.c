#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <net/if.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/genetlink.h>

#include <netlink/netlink.h>
#include <netlink/socket.h>
#include <netlink/msg.h>
#include <netlink/handlers.h>
#include <netlink/cache.h>
#include <netlink/cache-api.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>

#include "nlmon_netlink.h"
#include "nlmon_config.h"
#include "nlmon_nl_route.h"
#include "nlmon_nl_genl.h"
#include "nlmon_nl_diag.h"
#include "nlmon_nl_netfilter.h"
#include "nlmon_nl_error.h"

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
		nlmon_nl_log_error("Failed to allocate NETLINK_ROUTE socket", ENOMEM);
		return -ENOMEM;
	}
	
	/* Disable sequence number checking for asynchronous events */
	nl_socket_disable_seq_check(mgr->route_sock);
	
	/* Connect to NETLINK_ROUTE */
	ret = nl_connect(mgr->route_sock, NETLINK_ROUTE);
	if (ret < 0) {
		nlmon_nl_log_error("Failed to connect to NETLINK_ROUTE", ret);
		nl_socket_free(mgr->route_sock);
		mgr->route_sock = NULL;
		return ret;
	}
	
	/* Increase buffer sizes for high-traffic scenarios */
	ret = nl_socket_set_buffer_size(mgr->route_sock, 32768, 32768);
	if (ret < 0) {
		nlmon_nl_log_error("Failed to set NETLINK_ROUTE buffer size (non-fatal)", ret);
		/* Non-fatal, continue */
	}
	
	/* Set socket to non-blocking mode */
	ret = nl_socket_set_nonblocking(mgr->route_sock);
	if (ret < 0) {
		nlmon_nl_log_error("Failed to set NETLINK_ROUTE socket non-blocking", ret);
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
		nlmon_nl_log_error("Failed to join NETLINK_ROUTE multicast groups", ret);
		nl_close(mgr->route_sock);
		nl_socket_free(mgr->route_sock);
		mgr->route_sock = NULL;
		return ret;
	}
	
	/* Allocate callback structure */
	mgr->route_cb = nl_cb_alloc(NL_CB_DEFAULT);
	if (!mgr->route_cb) {
		nlmon_nl_log_error("Failed to allocate NETLINK_ROUTE callback", ENOMEM);
		nl_close(mgr->route_sock);
		nl_socket_free(mgr->route_sock);
		mgr->route_sock = NULL;
		return -ENOMEM;
	}
	
	/* Disable sequence number checking in callback for asynchronous events */
	nl_cb_set(mgr->route_cb, NL_CB_SEQ_CHECK, NL_CB_CUSTOM, NULL, NULL);
	
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
		nlmon_nl_log_error("Error processing NETLINK_ROUTE messages", ret);
		
		/* Check if this is a connection error */
		if (ret == -NLE_BAD_SOCK || errno == EBADF || errno == ENOTCONN) {
			/* Attempt to reconnect */
			nlmon_nl_log_error("Detected connection loss, attempting reconnection", 0);
			ret = nlmon_nl_reconnect(mgr, NETLINK_ROUTE);
			if (ret < 0) {
				nlmon_nl_log_error("Reconnection failed", ret);
				return ret;
			}
			/* Reconnection successful, return 0 to continue */
			return 0;
		}
		
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
		nlmon_nl_log_error("Error processing NETLINK_GENERIC messages", ret);
		
		/* Check if this is a connection error */
		if (ret == -NLE_BAD_SOCK || errno == EBADF || errno == ENOTCONN) {
			/* Attempt to reconnect */
			nlmon_nl_log_error("Detected connection loss, attempting reconnection", 0);
			ret = nlmon_nl_reconnect(mgr, NETLINK_GENERIC);
			if (ret < 0) {
				nlmon_nl_log_error("Reconnection failed", ret);
				return ret;
			}
			/* Reconnection successful, return 0 to continue */
			return 0;
		}
		
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
		nlmon_nl_log_error("Error processing NETLINK_SOCK_DIAG messages", ret);
		
		/* Check if this is a connection error */
		if (ret == -NLE_BAD_SOCK || errno == EBADF || errno == ENOTCONN) {
			/* Attempt to reconnect */
			nlmon_nl_log_error("Detected connection loss, attempting reconnection", 0);
			ret = nlmon_nl_reconnect(mgr, NETLINK_SOCK_DIAG);
			if (ret < 0) {
				nlmon_nl_log_error("Reconnection failed", ret);
				return ret;
			}
			/* Reconnection successful, return 0 to continue */
			return 0;
		}
		
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
		nlmon_nl_log_error("Error processing NETLINK_NETFILTER messages", ret);
		
		/* Check if this is a connection error */
		if (ret == -NLE_BAD_SOCK || errno == EBADF || errno == ENOTCONN) {
			/* Attempt to reconnect */
			nlmon_nl_log_error("Detected connection loss, attempting reconnection", 0);
			ret = nlmon_nl_reconnect(mgr, NETLINK_NETFILTER);
			if (ret < 0) {
				nlmon_nl_log_error("Reconnection failed", ret);
				return ret;
			}
			/* Reconnection successful, return 0 to continue */
			return 0;
		}
		
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

/**
 * Cache operations structure for link cache
 * 
 * Defines how to request and parse link (interface) information.
 */
static struct nl_cache_ops link_cache_ops = {
	.co_name = "route/link",
	.co_protocol = NETLINK_ROUTE,
	.co_hdrsize = sizeof(struct ifinfomsg),
	.co_request_update = NULL,  /* We'll handle this manually */
	.co_msg_parser = NULL,      /* We'll handle this manually */
	.co_obj_ops = NULL,
};

/**
 * Cache operations structure for address cache
 */
static struct nl_cache_ops addr_cache_ops = {
	.co_name = "route/addr",
	.co_protocol = NETLINK_ROUTE,
	.co_hdrsize = sizeof(struct ifaddrmsg),
	.co_request_update = NULL,
	.co_msg_parser = NULL,
	.co_obj_ops = NULL,
};

/**
 * Cache operations structure for route cache
 */
static struct nl_cache_ops route_cache_ops = {
	.co_name = "route/route",
	.co_protocol = NETLINK_ROUTE,
	.co_hdrsize = sizeof(struct rtmsg),
	.co_request_update = NULL,
	.co_msg_parser = NULL,
	.co_obj_ops = NULL,
};

/**
 * Request link dump from kernel
 */
static int request_link_dump(struct nl_sock *sk)
{
	struct {
		struct nlmsghdr nlh;
		struct ifinfomsg ifm;
	} req = {
		.nlh = {
			.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg)),
			.nlmsg_type = RTM_GETLINK,
			.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP,
		},
		.ifm = {
			.ifi_family = AF_UNSPEC,
		},
	};
	
	return nl_send_simple(sk, req.nlh.nlmsg_type, req.nlh.nlmsg_flags,
	                      &req.ifm, sizeof(req.ifm));
}

/**
 * Request address dump from kernel
 */
static int request_addr_dump(struct nl_sock *sk)
{
	struct {
		struct nlmsghdr nlh;
		struct ifaddrmsg ifa;
	} req = {
		.nlh = {
			.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifaddrmsg)),
			.nlmsg_type = RTM_GETADDR,
			.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP,
		},
		.ifa = {
			.ifa_family = AF_UNSPEC,
		},
	};
	
	return nl_send_simple(sk, req.nlh.nlmsg_type, req.nlh.nlmsg_flags,
	                      &req.ifa, sizeof(req.ifa));
}

/**
 * Request route dump from kernel
 */
static int request_route_dump(struct nl_sock *sk)
{
	struct {
		struct nlmsghdr nlh;
		struct rtmsg rtm;
	} req = {
		.nlh = {
			.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg)),
			.nlmsg_type = RTM_GETROUTE,
			.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP,
		},
		.rtm = {
			.rtm_family = AF_UNSPEC,
		},
	};
	
	return nl_send_simple(sk, req.nlh.nlmsg_type, req.nlh.nlmsg_flags,
	                      &req.rtm, sizeof(req.rtm));
}

/**
 * Initialize link cache
 */
int nlmon_nl_cache_init_link(struct nlmon_nl_manager *mgr)
{
	int ret;
	
	if (!mgr)
		return -EINVAL;
	
	/* Route protocol must be enabled */
	if (!mgr->route_sock || !mgr->enable_route) {
		nlmon_nl_log_error("Cannot initialize link cache: NETLINK_ROUTE not enabled", ENOTCONN);
		return -ENOTCONN;
	}
	
	/* Already initialized */
	if (mgr->link_cache)
		return 0;
	
	/* Allocate cache */
	mgr->link_cache = nl_cache_alloc(&link_cache_ops);
	if (!mgr->link_cache) {
		nlmon_nl_log_error("Failed to allocate link cache", ENOMEM);
		return -ENOMEM;
	}
	
	/* Request initial dump */
	ret = request_link_dump(mgr->route_sock);
	if (ret < 0) {
		nlmon_nl_log_error("Failed to request link dump", ret);
		nl_cache_free(mgr->link_cache);
		mgr->link_cache = NULL;
		return ret;
	}
	
	/* Note: The cache will be populated as messages arrive through the
	 * normal event processing path. The route message handler will
	 * update the cache when it sees RTM_NEWLINK messages. */
	
	return 0;
}

/**
 * Initialize address cache
 */
int nlmon_nl_cache_init_addr(struct nlmon_nl_manager *mgr)
{
	int ret;
	
	if (!mgr)
		return -EINVAL;
	
	/* Route protocol must be enabled */
	if (!mgr->route_sock || !mgr->enable_route) {
		fprintf(stderr, "Cannot initialize address cache: NETLINK_ROUTE not enabled\n");
		return -ENOTCONN;
	}
	
	/* Already initialized */
	if (mgr->addr_cache)
		return 0;
	
	/* Allocate cache */
	mgr->addr_cache = nl_cache_alloc(&addr_cache_ops);
	if (!mgr->addr_cache) {
		fprintf(stderr, "Failed to allocate address cache\n");
		return -ENOMEM;
	}
	
	/* Request initial dump */
	ret = request_addr_dump(mgr->route_sock);
	if (ret < 0) {
		fprintf(stderr, "Failed to request address dump: %s\n", nl_geterror(ret));
		nl_cache_free(mgr->addr_cache);
		mgr->addr_cache = NULL;
		return ret;
	}
	
	/* Cache will be populated through normal event processing */
	
	return 0;
}

/**
 * Initialize route cache (optional)
 */
int nlmon_nl_cache_init_route(struct nlmon_nl_manager *mgr)
{
	int ret;
	
	if (!mgr)
		return -EINVAL;
	
	/* Route protocol must be enabled */
	if (!mgr->route_sock || !mgr->enable_route) {
		fprintf(stderr, "Cannot initialize route cache: NETLINK_ROUTE not enabled\n");
		return -ENOTCONN;
	}
	
	/* Already initialized */
	if (mgr->route_cache)
		return 0;
	
	/* Allocate cache */
	mgr->route_cache = nl_cache_alloc(&route_cache_ops);
	if (!mgr->route_cache) {
		fprintf(stderr, "Failed to allocate route cache\n");
		return -ENOMEM;
	}
	
	/* Request initial dump */
	ret = request_route_dump(mgr->route_sock);
	if (ret < 0) {
		fprintf(stderr, "Failed to request route dump: %s\n", nl_geterror(ret));
		nl_cache_free(mgr->route_cache);
		mgr->route_cache = NULL;
		return ret;
	}
	
	/* Cache will be populated through normal event processing */
	
	return 0;
}

/**
 * Refresh all initialized caches
 */
int nlmon_nl_cache_refresh_all(struct nlmon_nl_manager *mgr)
{
	int ret;
	
	if (!mgr)
		return -EINVAL;
	
	/* Refresh link cache if initialized */
	if (mgr->link_cache) {
		nl_cache_clear(mgr->link_cache);
		ret = request_link_dump(mgr->route_sock);
		if (ret < 0) {
			fprintf(stderr, "Failed to refresh link cache: %s\n", nl_geterror(ret));
			return ret;
		}
	}
	
	/* Refresh address cache if initialized */
	if (mgr->addr_cache) {
		nl_cache_clear(mgr->addr_cache);
		ret = request_addr_dump(mgr->route_sock);
		if (ret < 0) {
			fprintf(stderr, "Failed to refresh address cache: %s\n", nl_geterror(ret));
			return ret;
		}
	}
	
	/* Refresh route cache if initialized */
	if (mgr->route_cache) {
		nl_cache_clear(mgr->route_cache);
		ret = request_route_dump(mgr->route_sock);
		if (ret < 0) {
			fprintf(stderr, "Failed to refresh route cache: %s\n", nl_geterror(ret));
			return ret;
		}
	}
	
	return 0;
}

/**
 * Update link cache with a netlink message
 * 
 * Called when RTM_NEWLINK or RTM_DELLINK messages are received.
 * 
 * @param mgr Netlink manager
 * @param msg Netlink message containing link information
 * @return 0 on success, negative error code on failure
 */
int nlmon_nl_cache_update_link(struct nlmon_nl_manager *mgr, struct nl_msg *msg)
{
	struct nlmsghdr *nlh;
	
	if (!mgr || !msg)
		return -EINVAL;
	
	/* Cache not initialized */
	if (!mgr->link_cache)
		return 0;
	
	nlh = nlmsg_hdr(msg);
	
	/* Parse and add to cache */
	if (nlh->nlmsg_type == RTM_NEWLINK) {
		/* For simplicity, we'll just parse and add the message to cache
		 * In a full implementation, we would create proper nl_object structures */
		return nl_cache_parse_and_add(mgr->link_cache, msg);
	} else if (nlh->nlmsg_type == RTM_DELLINK) {
		/* For deletion, we would need to find and remove the object
		 * This is simplified - in production we'd search by ifindex */
		return 0;
	}
	
	return 0;
}

/**
 * Update address cache with a netlink message
 * 
 * Called when RTM_NEWADDR or RTM_DELADDR messages are received.
 * 
 * @param mgr Netlink manager
 * @param msg Netlink message containing address information
 * @return 0 on success, negative error code on failure
 */
int nlmon_nl_cache_update_addr(struct nlmon_nl_manager *mgr, struct nl_msg *msg)
{
	struct nlmsghdr *nlh;
	
	if (!mgr || !msg)
		return -EINVAL;
	
	/* Cache not initialized */
	if (!mgr->addr_cache)
		return 0;
	
	nlh = nlmsg_hdr(msg);
	
	/* Parse and add to cache */
	if (nlh->nlmsg_type == RTM_NEWADDR) {
		return nl_cache_parse_and_add(mgr->addr_cache, msg);
	} else if (nlh->nlmsg_type == RTM_DELADDR) {
		/* For deletion, we would need to find and remove the object */
		return 0;
	}
	
	return 0;
}

/**
 * Update route cache with a netlink message
 * 
 * Called when RTM_NEWROUTE or RTM_DELROUTE messages are received.
 * 
 * @param mgr Netlink manager
 * @param msg Netlink message containing route information
 * @return 0 on success, negative error code on failure
 */
int nlmon_nl_cache_update_route(struct nlmon_nl_manager *mgr, struct nl_msg *msg)
{
	struct nlmsghdr *nlh;
	
	if (!mgr || !msg)
		return -EINVAL;
	
	/* Cache not initialized */
	if (!mgr->route_cache)
		return 0;
	
	nlh = nlmsg_hdr(msg);
	
	/* Parse and add to cache */
	if (nlh->nlmsg_type == RTM_NEWROUTE) {
		return nl_cache_parse_and_add(mgr->route_cache, msg);
	} else if (nlh->nlmsg_type == RTM_DELROUTE) {
		/* For deletion, we would need to find and remove the object */
		return 0;
	}
	
	return 0;
}

/**
 * Get link information by interface index
 * 
 * This is a simplified implementation that queries the kernel directly
 * rather than searching the cache, since we don't have full object structures.
 */
int nlmon_nl_get_link_by_index(struct nlmon_nl_manager *mgr, int ifindex, char *ifname)
{
	if (!mgr || !ifname || ifindex < 0)
		return -EINVAL;
	
	/* Use if_indextoname to get the interface name */
	if (if_indextoname(ifindex, ifname) == NULL) {
		return -ENOENT;
	}
	
	return 0;
}

/**
 * Get link information by interface name
 * 
 * This is a simplified implementation that queries the kernel directly.
 */
int nlmon_nl_get_link_by_name(struct nlmon_nl_manager *mgr, const char *ifname, int *ifindex)
{
	unsigned int idx;
	
	if (!mgr || !ifname || !ifindex)
		return -EINVAL;
	
	/* Use if_nametoindex to get the interface index */
	idx = if_nametoindex(ifname);
	if (idx == 0) {
		return -ENOENT;
	}
	
	*ifindex = (int)idx;
	return 0;
}

/**
 * Get number of items in link cache
 */
int nlmon_nl_get_link_count(struct nlmon_nl_manager *mgr)
{
	if (!mgr || !mgr->link_cache)
		return -1;
	
	return nl_cache_nitems(mgr->link_cache);
}

/**
 * Get number of items in address cache
 */
int nlmon_nl_get_addr_count(struct nlmon_nl_manager *mgr)
{
	if (!mgr || !mgr->addr_cache)
		return -1;
	
	return nl_cache_nitems(mgr->addr_cache);
}

/**
 * Get number of items in route cache
 */
int nlmon_nl_get_route_count(struct nlmon_nl_manager *mgr)
{
	if (!mgr || !mgr->route_cache)
		return -1;
	
	return nl_cache_nitems(mgr->route_cache);
}

/**
 * Apply configuration to netlink manager
 * 
 * Configures the netlink manager based on the provided configuration.
 * Enables/disables protocols, sets buffer sizes, initializes caches,
 * and subscribes to multicast groups as specified.
 */
int nlmon_nl_apply_config(struct nlmon_nl_manager *mgr,
                          const struct nlmon_netlink_config *config)
{
	int ret;
	
	if (!mgr || !config)
		return -EINVAL;
	
	/* Enable protocols based on configuration */
	if (config->protocols.route) {
		ret = nlmon_nl_enable_route(mgr);
		if (ret < 0) {
			fprintf(stderr, "Failed to enable NETLINK_ROUTE: %s\n",
			        nl_geterror(ret));
			return ret;
		}
		
		/* Set buffer sizes for route socket */
		if (mgr->route_sock) {
			ret = nl_socket_set_buffer_size(mgr->route_sock,
			                                 config->buffer_size.receive,
			                                 config->buffer_size.send);
			if (ret < 0) {
				fprintf(stderr, "Warning: Failed to set NETLINK_ROUTE buffer size: %s\n",
				        nl_geterror(ret));
				/* Non-fatal, continue */
			}
		}
	}
	
	if (config->protocols.generic) {
		ret = nlmon_nl_enable_generic(mgr);
		if (ret < 0) {
			fprintf(stderr, "Failed to enable NETLINK_GENERIC: %s\n",
			        nl_geterror(ret));
			return ret;
		}
		
		/* Set buffer sizes for generic socket */
		if (mgr->genl_sock) {
			ret = nl_socket_set_buffer_size(mgr->genl_sock,
			                                 config->buffer_size.receive,
			                                 config->buffer_size.send);
			if (ret < 0) {
				fprintf(stderr, "Warning: Failed to set NETLINK_GENERIC buffer size: %s\n",
				        nl_geterror(ret));
				/* Non-fatal, continue */
			}
		}
		
		/* Resolve generic netlink families */
		for (int i = 0; i < config->generic_families.family_count; i++) {
			const char *family_name = config->generic_families.families[i];
			ret = nlmon_nl_resolve_family(mgr, family_name);
			if (ret < 0) {
				fprintf(stderr, "Warning: Failed to resolve family '%s': %s\n",
				        family_name, nl_geterror(ret));
				/* Non-fatal, continue */
			} else {
				printf("Resolved generic netlink family '%s' to ID %d\n",
				       family_name, ret);
			}
		}
	}
	
	if (config->protocols.sock_diag) {
		ret = nlmon_nl_enable_diag(mgr);
		if (ret < 0) {
			fprintf(stderr, "Failed to enable NETLINK_SOCK_DIAG: %s\n",
			        nl_geterror(ret));
			return ret;
		}
		
		/* Set buffer sizes for diag socket */
		if (mgr->diag_sock) {
			ret = nl_socket_set_buffer_size(mgr->diag_sock,
			                                 config->buffer_size.receive,
			                                 config->buffer_size.send);
			if (ret < 0) {
				fprintf(stderr, "Warning: Failed to set NETLINK_SOCK_DIAG buffer size: %s\n",
				        nl_geterror(ret));
				/* Non-fatal, continue */
			}
		}
	}
	
	if (config->protocols.netfilter) {
		ret = nlmon_nl_enable_netfilter(mgr);
		if (ret < 0) {
			fprintf(stderr, "Failed to enable NETLINK_NETFILTER: %s\n",
			        nl_geterror(ret));
			return ret;
		}
		
		/* Set buffer sizes for netfilter socket */
		if (mgr->nf_sock) {
			ret = nl_socket_set_buffer_size(mgr->nf_sock,
			                                 config->buffer_size.receive,
			                                 config->buffer_size.send);
			if (ret < 0) {
				fprintf(stderr, "Warning: Failed to set NETLINK_NETFILTER buffer size: %s\n",
				        nl_geterror(ret));
				/* Non-fatal, continue */
			}
		}
	}
	
	/* Initialize caches if enabled */
	if (config->caching.enabled) {
		if (config->caching.link_cache) {
			ret = nlmon_nl_cache_init_link(mgr);
			if (ret < 0) {
				fprintf(stderr, "Warning: Failed to initialize link cache: %s\n",
				        nl_geterror(ret));
				/* Non-fatal, continue */
			} else {
				printf("Initialized link cache with %d entries\n",
				       nlmon_nl_get_link_count(mgr));
			}
		}
		
		if (config->caching.addr_cache) {
			ret = nlmon_nl_cache_init_addr(mgr);
			if (ret < 0) {
				fprintf(stderr, "Warning: Failed to initialize address cache: %s\n",
				        nl_geterror(ret));
				/* Non-fatal, continue */
			} else {
				printf("Initialized address cache with %d entries\n",
				       nlmon_nl_get_addr_count(mgr));
			}
		}
		
		if (config->caching.route_cache) {
			ret = nlmon_nl_cache_init_route(mgr);
			if (ret < 0) {
				fprintf(stderr, "Warning: Failed to initialize route cache: %s\n",
				        nl_geterror(ret));
				/* Non-fatal, continue */
			} else {
				printf("Initialized route cache with %d entries\n",
				       nlmon_nl_get_route_count(mgr));
			}
		}
	}
	
	/* Note: Multicast group subscription is handled in nlmon_nl_enable_route()
	 * The multicast_groups configuration is informational and could be used
	 * for selective subscription in future enhancements */
	
	return 0;
}

/**
 * Error recovery functions
 */

/**
 * Reconnect NETLINK_ROUTE socket
 */
static int reconnect_route_socket(struct nlmon_nl_manager *mgr)
{
	int ret;
	
	if (!mgr)
		return -EINVAL;
	
	nlmon_nl_log_error("Attempting to reconnect NETLINK_ROUTE socket", 0);
	
	/* Close existing socket if present */
	if (mgr->route_sock) {
		nl_close(mgr->route_sock);
		nl_socket_free(mgr->route_sock);
		mgr->route_sock = NULL;
	}
	
	/* Free existing callback */
	if (mgr->route_cb) {
		nl_cb_put(mgr->route_cb);
		mgr->route_cb = NULL;
	}
	
	/* Reset enable flag */
	mgr->enable_route = 0;
	
	/* Re-enable the protocol */
	ret = nlmon_nl_enable_route(mgr);
	if (ret < 0) {
		nlmon_nl_log_error("Failed to reconnect NETLINK_ROUTE socket", ret);
		return ret;
	}
	
	nlmon_nl_log_error("Successfully reconnected NETLINK_ROUTE socket", 0);
	return 0;
}

/**
 * Reconnect NETLINK_GENERIC socket
 */
static int reconnect_genl_socket(struct nlmon_nl_manager *mgr)
{
	int ret;
	
	if (!mgr)
		return -EINVAL;
	
	nlmon_nl_log_error("Attempting to reconnect NETLINK_GENERIC socket", 0);
	
	/* Close existing socket if present */
	if (mgr->genl_sock) {
		nl_close(mgr->genl_sock);
		nl_socket_free(mgr->genl_sock);
		mgr->genl_sock = NULL;
	}
	
	/* Free existing callback */
	if (mgr->genl_cb) {
		nl_cb_put(mgr->genl_cb);
		mgr->genl_cb = NULL;
	}
	
	/* Reset enable flag */
	mgr->enable_genl = 0;
	
	/* Reset family IDs */
	mgr->nl80211_id = -1;
	mgr->taskstats_id = -1;
	
	/* Re-enable the protocol */
	ret = nlmon_nl_enable_generic(mgr);
	if (ret < 0) {
		nlmon_nl_log_error("Failed to reconnect NETLINK_GENERIC socket", ret);
		return ret;
	}
	
	nlmon_nl_log_error("Successfully reconnected NETLINK_GENERIC socket", 0);
	return 0;
}

/**
 * Reconnect NETLINK_SOCK_DIAG socket
 */
static int reconnect_diag_socket(struct nlmon_nl_manager *mgr)
{
	int ret;
	
	if (!mgr)
		return -EINVAL;
	
	nlmon_nl_log_error("Attempting to reconnect NETLINK_SOCK_DIAG socket", 0);
	
	/* Close existing socket if present */
	if (mgr->diag_sock) {
		nl_close(mgr->diag_sock);
		nl_socket_free(mgr->diag_sock);
		mgr->diag_sock = NULL;
	}
	
	/* Free existing callback */
	if (mgr->diag_cb) {
		nl_cb_put(mgr->diag_cb);
		mgr->diag_cb = NULL;
	}
	
	/* Reset enable flag */
	mgr->enable_diag = 0;
	
	/* Re-enable the protocol */
	ret = nlmon_nl_enable_diag(mgr);
	if (ret < 0) {
		nlmon_nl_log_error("Failed to reconnect NETLINK_SOCK_DIAG socket", ret);
		return ret;
	}
	
	nlmon_nl_log_error("Successfully reconnected NETLINK_SOCK_DIAG socket", 0);
	return 0;
}

/**
 * Reconnect NETLINK_NETFILTER socket
 */
static int reconnect_nf_socket(struct nlmon_nl_manager *mgr)
{
	int ret;
	
	if (!mgr)
		return -EINVAL;
	
	nlmon_nl_log_error("Attempting to reconnect NETLINK_NETFILTER socket", 0);
	
	/* Close existing socket if present */
	if (mgr->nf_sock) {
		nl_close(mgr->nf_sock);
		nl_socket_free(mgr->nf_sock);
		mgr->nf_sock = NULL;
	}
	
	/* Free existing callback */
	if (mgr->nf_cb) {
		nl_cb_put(mgr->nf_cb);
		mgr->nf_cb = NULL;
	}
	
	/* Reset enable flag */
	mgr->enable_netfilter = 0;
	
	/* Re-enable the protocol */
	ret = nlmon_nl_enable_netfilter(mgr);
	if (ret < 0) {
		nlmon_nl_log_error("Failed to reconnect NETLINK_NETFILTER socket", ret);
		return ret;
	}
	
	nlmon_nl_log_error("Successfully reconnected NETLINK_NETFILTER socket", 0);
	return 0;
}

/**
 * Reconnect a netlink socket after connection loss
 */
int nlmon_nl_reconnect(struct nlmon_nl_manager *mgr, int protocol)
{
	if (!mgr)
		return -EINVAL;
	
	switch (protocol) {
	case NETLINK_ROUTE:
		return reconnect_route_socket(mgr);
	
	case NETLINK_GENERIC:
		return reconnect_genl_socket(mgr);
	
	case NETLINK_SOCK_DIAG:
		return reconnect_diag_socket(mgr);
	
	case NETLINK_NETFILTER:
		return reconnect_nf_socket(mgr);
	
	default:
		nlmon_nl_log_error("Unknown protocol for reconnection", EINVAL);
		return -EINVAL;
	}
}

/**
 * Check if a netlink socket is still connected
 */
int nlmon_nl_is_connected(struct nlmon_nl_manager *mgr, int protocol)
{
	struct nl_sock *sock = NULL;
	int fd;
	int error = 0;
	socklen_t len = sizeof(error);
	
	if (!mgr)
		return -EINVAL;
	
	/* Get the appropriate socket */
	switch (protocol) {
	case NETLINK_ROUTE:
		sock = mgr->route_sock;
		break;
	case NETLINK_GENERIC:
		sock = mgr->genl_sock;
		break;
	case NETLINK_SOCK_DIAG:
		sock = mgr->diag_sock;
		break;
	case NETLINK_NETFILTER:
		sock = mgr->nf_sock;
		break;
	default:
		return -EINVAL;
	}
	
	/* Socket not allocated */
	if (!sock)
		return 0;
	
	/* Get file descriptor */
	fd = nl_socket_get_fd(sock);
	if (fd < 0)
		return 0;
	
	/* Check socket error status */
	if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0)
		return 0;
	
	/* If there's an error on the socket, it's not connected */
	if (error != 0)
		return 0;
	
	return 1;
}

/**
 * Handle message parse error
 */
int nlmon_nl_handle_parse_error(struct nlmon_nl_manager *mgr,
                                 struct nl_msg *msg,
                                 int error)
{
	struct nlmsghdr *nlh;
	
	if (!mgr || !msg)
		return -EINVAL;
	
	nlh = nlmsg_hdr(msg);
	
	/* Log the parse error */
	nlmon_nl_log_error("Message parse error", error);
	
	/* Log message details */
	nlmon_nl_log_message(nlh, "PARSE_ERROR");
	
	/* Dump message if enabled */
	nlmon_nl_dump_message(nlh, nlh->nlmsg_len);
	
	/* Continue processing other messages (don't abort) */
	return 0;
}

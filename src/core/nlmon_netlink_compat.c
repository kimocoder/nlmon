#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#include "event_processor.h"
#include "nlmon_nl_event.h"
#include "nlmon_nl_route.h"
#include "nlmon_nl_genl.h"
#include "nlmon_nl_diag.h"
#include "nlmon_nl_netfilter.h"
#include "nlmon_netlink.h"
#include "nlmon_netlink_compat.h"

/**
 * Netlink Compatibility Layer Implementation
 * 
 * This file implements wrapper functions that provide the old
 * netlink_multi_protocol API while using the new libnl-based
 * implementation internally.
 */

/**
 * Initialize multi-protocol support (compatibility wrapper)
 */
struct nlmon_multi_protocol_ctx *nlmon_multi_protocol_init_compat(void)
{
	struct nlmon_multi_protocol_ctx_compat *compat_ctx;
	
	/* Allocate compatibility context */
	compat_ctx = calloc(1, sizeof(*compat_ctx));
	if (!compat_ctx) {
		fprintf(stderr, "Failed to allocate compatibility context\n");
		return NULL;
	}
	
	/* Initialize new netlink manager */
	compat_ctx->nl_mgr = nlmon_nl_manager_init();
	if (!compat_ctx->nl_mgr) {
		fprintf(stderr, "Failed to initialize netlink manager\n");
		free(compat_ctx);
		return NULL;
	}
	
	/* Initialize state */
	compat_ctx->event_callback = NULL;
	compat_ctx->user_data = NULL;
	compat_ctx->enable_route = 0;
	compat_ctx->enable_generic = 0;
	compat_ctx->enable_sock_diag = 0;
	compat_ctx->route_fd = -1;
	compat_ctx->generic_fd = -1;
	compat_ctx->sock_diag_fd = -1;
	
	/* Set up event translator callback */
	nlmon_nl_set_callback(compat_ctx->nl_mgr,
	                      nlmon_compat_event_translator,
	                      compat_ctx);
	
	/* Cast to old context type for compatibility */
	return (struct nlmon_multi_protocol_ctx *)compat_ctx;
}

/**
 * Enable specific protocol (compatibility wrapper)
 */
int nlmon_multi_protocol_enable_compat(struct nlmon_multi_protocol_ctx *ctx,
                                       nlmon_protocol_t proto)
{
	struct nlmon_multi_protocol_ctx_compat *compat_ctx;
	int ret;
	
	if (!ctx)
		return -EINVAL;
	
	compat_ctx = (struct nlmon_multi_protocol_ctx_compat *)ctx;
	
	if (!compat_ctx->nl_mgr)
		return -EINVAL;
	
	switch (proto) {
	case NLMON_PROTO_ROUTE:
		/* Already enabled */
		if (compat_ctx->enable_route)
			return 0;
		
		ret = nlmon_nl_enable_route(compat_ctx->nl_mgr);
		if (ret < 0) {
			fprintf(stderr, "Failed to enable NETLINK_ROUTE: %d\n", ret);
			return ret;
		}
		
		compat_ctx->enable_route = 1;
		compat_ctx->route_fd = nlmon_nl_get_route_fd(compat_ctx->nl_mgr);
		break;
		
	case NLMON_PROTO_GENERIC:
		/* Already enabled */
		if (compat_ctx->enable_generic)
			return 0;
		
		ret = nlmon_nl_enable_generic(compat_ctx->nl_mgr);
		if (ret < 0) {
			fprintf(stderr, "Failed to enable NETLINK_GENERIC: %d\n", ret);
			return ret;
		}
		
		compat_ctx->enable_generic = 1;
		compat_ctx->generic_fd = nlmon_nl_get_genl_fd(compat_ctx->nl_mgr);
		break;
		
	case NLMON_PROTO_SOCK_DIAG:
		/* Already enabled */
		if (compat_ctx->enable_sock_diag)
			return 0;
		
		ret = nlmon_nl_enable_diag(compat_ctx->nl_mgr);
		if (ret < 0) {
			fprintf(stderr, "Failed to enable NETLINK_SOCK_DIAG: %d\n", ret);
			return ret;
		}
		
		compat_ctx->enable_sock_diag = 1;
		compat_ctx->sock_diag_fd = nlmon_nl_get_diag_fd(compat_ctx->nl_mgr);
		break;
		
	default:
		fprintf(stderr, "Unknown protocol: %d\n", proto);
		return -EINVAL;
	}
	
	return 0;
}

/**
 * Disable specific protocol (compatibility wrapper)
 */
int nlmon_multi_protocol_disable_compat(struct nlmon_multi_protocol_ctx *ctx,
                                        nlmon_protocol_t proto)
{
	struct nlmon_multi_protocol_ctx_compat *compat_ctx;
	
	if (!ctx)
		return -EINVAL;
	
	compat_ctx = (struct nlmon_multi_protocol_ctx_compat *)ctx;
	
	/* Note: The new implementation doesn't support disabling protocols
	 * after they're enabled (sockets remain open). For compatibility,
	 * we just update the flags and return success. */
	
	switch (proto) {
	case NLMON_PROTO_ROUTE:
		compat_ctx->enable_route = 0;
		compat_ctx->route_fd = -1;
		break;
		
	case NLMON_PROTO_GENERIC:
		compat_ctx->enable_generic = 0;
		compat_ctx->generic_fd = -1;
		break;
		
	case NLMON_PROTO_SOCK_DIAG:
		compat_ctx->enable_sock_diag = 0;
		compat_ctx->sock_diag_fd = -1;
		break;
		
	default:
		return -EINVAL;
	}
	
	return 0;
}

/**
 * Set event callback (compatibility wrapper)
 */
void nlmon_multi_protocol_set_callback_compat(struct nlmon_multi_protocol_ctx *ctx,
                                              void (*callback)(nlmon_event_type_t, void *, void *),
                                              void *user_data)
{
	struct nlmon_multi_protocol_ctx_compat *compat_ctx;
	
	if (!ctx)
		return;
	
	compat_ctx = (struct nlmon_multi_protocol_ctx_compat *)ctx;
	
	/* Store old-style callback and user data */
	compat_ctx->event_callback = callback;
	compat_ctx->user_data = user_data;
	
	/* The event translator callback is already set up in init,
	 * it will use these stored values */
}

/**
 * Get file descriptor for event loop (compatibility wrapper)
 */
int nlmon_multi_protocol_get_fd_compat(struct nlmon_multi_protocol_ctx *ctx,
                                       nlmon_protocol_t proto)
{
	struct nlmon_multi_protocol_ctx_compat *compat_ctx;
	
	if (!ctx)
		return -1;
	
	compat_ctx = (struct nlmon_multi_protocol_ctx_compat *)ctx;
	
	if (!compat_ctx->nl_mgr)
		return -1;
	
	switch (proto) {
	case NLMON_PROTO_ROUTE:
		if (!compat_ctx->enable_route)
			return -1;
		return compat_ctx->route_fd;
		
	case NLMON_PROTO_GENERIC:
		if (!compat_ctx->enable_generic)
			return -1;
		return compat_ctx->generic_fd;
		
	case NLMON_PROTO_SOCK_DIAG:
		if (!compat_ctx->enable_sock_diag)
			return -1;
		return compat_ctx->sock_diag_fd;
		
	default:
		return -1;
	}
}

/**
 * Process messages from protocol (compatibility wrapper)
 */
int nlmon_multi_protocol_process_compat(struct nlmon_multi_protocol_ctx *ctx,
                                        nlmon_protocol_t proto)
{
	struct nlmon_multi_protocol_ctx_compat *compat_ctx;
	int ret;
	
	if (!ctx)
		return -EINVAL;
	
	compat_ctx = (struct nlmon_multi_protocol_ctx_compat *)ctx;
	
	if (!compat_ctx->nl_mgr)
		return -EINVAL;
	
	switch (proto) {
	case NLMON_PROTO_ROUTE:
		if (!compat_ctx->enable_route)
			return -EINVAL;
		ret = nlmon_nl_process_route(compat_ctx->nl_mgr);
		break;
		
	case NLMON_PROTO_GENERIC:
		if (!compat_ctx->enable_generic)
			return -EINVAL;
		ret = nlmon_nl_process_genl(compat_ctx->nl_mgr);
		break;
		
	case NLMON_PROTO_SOCK_DIAG:
		if (!compat_ctx->enable_sock_diag)
			return -EINVAL;
		ret = nlmon_nl_process_diag(compat_ctx->nl_mgr);
		break;
		
	default:
		return -EINVAL;
	}
	
	return ret;
}

/**
 * Cleanup and destroy context (compatibility wrapper)
 */
void nlmon_multi_protocol_destroy_compat(struct nlmon_multi_protocol_ctx *ctx)
{
	struct nlmon_multi_protocol_ctx_compat *compat_ctx;
	
	if (!ctx)
		return;
	
	compat_ctx = (struct nlmon_multi_protocol_ctx_compat *)ctx;
	
	/* Destroy new netlink manager */
	if (compat_ctx->nl_mgr) {
		nlmon_nl_manager_destroy(compat_ctx->nl_mgr);
		compat_ctx->nl_mgr = NULL;
	}
	
	/* Free compatibility context */
	free(compat_ctx);
}

/**
 * Helper function to convert new event format to old format
 * 
 * This is the bridge between the new libnl-based event system and
 * the old callback-based system. It translates nlmon_event structures
 * to the old format and invokes the old-style callback.
 */
void nlmon_compat_event_translator(struct nlmon_event *evt, void *user_data)
{
	struct nlmon_multi_protocol_ctx_compat *compat_ctx;
	nlmon_event_type_t old_event_type;
	void *old_event_data;
	struct nlmon_generic_msg generic_msg;
	struct nlmon_sock_diag diag_msg;
	
	if (!evt || !user_data)
		return;
	
	compat_ctx = (struct nlmon_multi_protocol_ctx_compat *)user_data;
	
	/* If no callback is set, nothing to do */
	if (!compat_ctx->event_callback)
		return;
	
	/* Translate event type and data based on netlink protocol */
	switch (evt->netlink.protocol) {
	case NETLINK_ROUTE:
		/* Determine old event type from message type */
		switch (evt->netlink.msg_type) {
		case RTM_NEWLINK:
		case RTM_DELLINK:
		case RTM_GETLINK:
		case RTM_SETLINK:
			old_event_type = NLMON_EVENT_LINK;
			break;
			
		case RTM_NEWADDR:
		case RTM_DELADDR:
		case RTM_GETADDR:
			old_event_type = NLMON_EVENT_ADDR;
			break;
			
		case RTM_NEWROUTE:
		case RTM_DELROUTE:
		case RTM_GETROUTE:
			old_event_type = NLMON_EVENT_ROUTE;
			break;
			
		case RTM_NEWNEIGH:
		case RTM_DELNEIGH:
		case RTM_GETNEIGH:
			old_event_type = NLMON_EVENT_NEIGH;
			break;
			
		case RTM_NEWRULE:
		case RTM_DELRULE:
		case RTM_GETRULE:
			old_event_type = NLMON_EVENT_RULE;
			break;
			
		default:
			/* Unknown route message type, skip */
			return;
		}
		
		/* For route protocol, pass the raw netlink header */
		old_event_data = evt->raw_msg;
		break;
		
	case NETLINK_GENERIC:
		old_event_type = NLMON_EVENT_GENERIC;
		
		/* Convert to old generic message format */
		generic_msg.cmd = evt->netlink.genl_cmd;
		generic_msg.version = evt->netlink.genl_version;
		generic_msg.family_id = evt->netlink.genl_family_id;
		strncpy(generic_msg.family_name, evt->netlink.genl_family_name,
		        sizeof(generic_msg.family_name) - 1);
		generic_msg.family_name[sizeof(generic_msg.family_name) - 1] = '\0';
		
		old_event_data = &generic_msg;
		break;
		
	case NETLINK_SOCK_DIAG:
		old_event_type = NLMON_EVENT_SOCK_DIAG;
		
		/* Convert to old socket diag format */
		if (evt->netlink.data.diag) {
			diag_msg.family = evt->netlink.data.diag->family;
			diag_msg.state = evt->netlink.data.diag->state;
			diag_msg.protocol = evt->netlink.data.diag->protocol;
			diag_msg.src_port = evt->netlink.data.diag->src_port;
			diag_msg.dst_port = evt->netlink.data.diag->dst_port;
			diag_msg.inode = evt->netlink.data.diag->inode;
			strncpy(diag_msg.src_addr, evt->netlink.data.diag->src_addr,
			        sizeof(diag_msg.src_addr) - 1);
			diag_msg.src_addr[sizeof(diag_msg.src_addr) - 1] = '\0';
			strncpy(diag_msg.dst_addr, evt->netlink.data.diag->dst_addr,
			        sizeof(diag_msg.dst_addr) - 1);
			diag_msg.dst_addr[sizeof(diag_msg.dst_addr) - 1] = '\0';
			
			old_event_data = &diag_msg;
		} else {
			/* No diag data available, skip */
			return;
		}
		break;
		
	default:
		/* Unknown protocol, skip */
		return;
	}
	
	/* Invoke old-style callback */
	compat_ctx->event_callback(old_event_type, old_event_data,
	                           compat_ctx->user_data);
}

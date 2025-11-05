#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nlmon_netlink_api.h"
#include "nlmon_netlink_compat.h"
#include "netlink_multi_protocol.h"

/**
 * Unified Netlink API Implementation
 * 
 * This file implements the unified API that can switch between the old
 * and new netlink implementations based on configuration.
 */

/**
 * Initialize netlink support with implementation selection
 */
struct nlmon_multi_protocol_ctx *nlmon_netlink_init(const struct nlmon_netlink_config *config)
{
	if (!config) {
		fprintf(stderr, "Invalid configuration\n");
		return NULL;
	}
	
	if (config->use_libnl) {
		/* Use new libnl-based implementation via compatibility layer */
		printf("Initializing netlink with libnl-based implementation\n");
		return nlmon_multi_protocol_init_compat();
	} else {
		/* Use old implementation */
		printf("Initializing netlink with legacy implementation\n");
		return nlmon_multi_protocol_init();
	}
}

/**
 * Enable specific protocol
 */
int nlmon_netlink_enable(struct nlmon_multi_protocol_ctx *ctx,
                         nlmon_protocol_t proto,
                         int use_libnl)
{
	if (!ctx)
		return -1;
	
	if (use_libnl) {
		return nlmon_multi_protocol_enable_compat(ctx, proto);
	} else {
		return nlmon_multi_protocol_enable(ctx, proto);
	}
}

/**
 * Disable specific protocol
 */
int nlmon_netlink_disable(struct nlmon_multi_protocol_ctx *ctx,
                          nlmon_protocol_t proto,
                          int use_libnl)
{
	if (!ctx)
		return -1;
	
	if (use_libnl) {
		return nlmon_multi_protocol_disable_compat(ctx, proto);
	} else {
		return nlmon_multi_protocol_disable(ctx, proto);
	}
}

/**
 * Set event callback
 */
void nlmon_netlink_set_callback(struct nlmon_multi_protocol_ctx *ctx,
                                void (*callback)(nlmon_event_type_t, void *, void *),
                                void *user_data,
                                int use_libnl)
{
	if (!ctx)
		return;
	
	if (use_libnl) {
		nlmon_multi_protocol_set_callback_compat(ctx, callback, user_data);
	} else {
		nlmon_multi_protocol_set_callback(ctx, callback, user_data);
	}
}

/**
 * Get file descriptor for event loop
 */
int nlmon_netlink_get_fd(struct nlmon_multi_protocol_ctx *ctx,
                         nlmon_protocol_t proto,
                         int use_libnl)
{
	if (!ctx)
		return -1;
	
	if (use_libnl) {
		return nlmon_multi_protocol_get_fd_compat(ctx, proto);
	} else {
		return nlmon_multi_protocol_get_fd(ctx, proto);
	}
}

/**
 * Process messages from protocol
 */
int nlmon_netlink_process(struct nlmon_multi_protocol_ctx *ctx,
                          nlmon_protocol_t proto,
                          int use_libnl)
{
	if (!ctx)
		return -1;
	
	if (use_libnl) {
		return nlmon_multi_protocol_process_compat(ctx, proto);
	} else {
		return nlmon_multi_protocol_process(ctx, proto);
	}
}

/**
 * Cleanup and destroy context
 */
void nlmon_netlink_destroy(struct nlmon_multi_protocol_ctx *ctx,
                           int use_libnl)
{
	if (!ctx)
		return;
	
	if (use_libnl) {
		nlmon_multi_protocol_destroy_compat(ctx);
	} else {
		nlmon_multi_protocol_destroy(ctx);
	}
}

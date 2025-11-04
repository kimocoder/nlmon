/* hot_reload.c - Configuration hot-reload implementation
 *
 * Copyright (C) 2025  nlmon contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/inotify.h>
#include "nlmon_config.h"

#define INOTIFY_EVENT_SIZE (sizeof(struct inotify_event))
#define INOTIFY_BUF_LEN (1024 * (INOTIFY_EVENT_SIZE + 16))

/* Initialize configuration file watching with inotify */
int nlmon_config_watch_init(struct nlmon_config_ctx *ctx)
{
	if (!ctx || !ctx->current)
		return NLMON_CONFIG_ERR_INVALID_VALUE;
	
	/* Check if config file is set */
	if (ctx->current->config_file[0] == '\0') {
		fprintf(stderr, "No configuration file specified for watching\n");
		return NLMON_CONFIG_ERR_INVALID_VALUE;
	}
	
	/* Initialize inotify */
	ctx->watch_fd = inotify_init1(IN_NONBLOCK);
	if (ctx->watch_fd < 0) {
		fprintf(stderr, "Failed to initialize inotify: %s\n", strerror(errno));
		return NLMON_CONFIG_ERR_NOMEM;
	}
	
	/* Add watch for configuration file */
	ctx->watch_wd = inotify_add_watch(ctx->watch_fd, 
	                                  ctx->current->config_file,
	                                  IN_MODIFY | IN_CLOSE_WRITE | IN_MOVE_SELF | IN_DELETE_SELF);
	
	if (ctx->watch_wd < 0) {
		fprintf(stderr, "Failed to add inotify watch for %s: %s\n",
		        ctx->current->config_file, strerror(errno));
		close(ctx->watch_fd);
		ctx->watch_fd = -1;
		return NLMON_CONFIG_ERR_FILE_NOT_FOUND;
	}
	
	fprintf(stderr, "Watching configuration file: %s\n", ctx->current->config_file);
	
	return NLMON_CONFIG_OK;
}

/* Check for configuration file changes */
bool nlmon_config_watch_check(struct nlmon_config_ctx *ctx)
{
	char buffer[INOTIFY_BUF_LEN];
	ssize_t len;
	bool changed = false;
	
	if (!ctx || ctx->watch_fd < 0)
		return false;
	
	/* Read inotify events */
	len = read(ctx->watch_fd, buffer, sizeof(buffer));
	
	if (len < 0) {
		if (errno != EAGAIN && errno != EWOULDBLOCK) {
			fprintf(stderr, "Error reading inotify events: %s\n", strerror(errno));
		}
		return false;
	}
	
	if (len == 0)
		return false;
	
	/* Process events */
	ssize_t i = 0;
	while (i < len) {
		struct inotify_event *event = (struct inotify_event *)&buffer[i];
		
		if (event->mask & (IN_MODIFY | IN_CLOSE_WRITE)) {
			fprintf(stderr, "Configuration file modified\n");
			changed = true;
		}
		
		if (event->mask & (IN_MOVE_SELF | IN_DELETE_SELF)) {
			fprintf(stderr, "Configuration file moved or deleted\n");
			/* Remove old watch */
			if (ctx->watch_wd >= 0) {
				inotify_rm_watch(ctx->watch_fd, ctx->watch_wd);
				ctx->watch_wd = -1;
			}
			
			/* Try to re-add watch (file might have been replaced) */
			sleep(1); /* Give time for file to be recreated */
			ctx->watch_wd = inotify_add_watch(ctx->watch_fd,
			                                  ctx->current->config_file,
			                                  IN_MODIFY | IN_CLOSE_WRITE | IN_MOVE_SELF | IN_DELETE_SELF);
			
			if (ctx->watch_wd >= 0) {
				fprintf(stderr, "Re-established watch on configuration file\n");
				changed = true;
			}
		}
		
		i += INOTIFY_EVENT_SIZE + event->len;
	}
	
	if (changed)
		ctx->reload_requested = true;
	
	return changed;
}

/* Reload configuration from file */
int nlmon_config_ctx_reload(struct nlmon_config_ctx *ctx)
{
	struct nlmon_config *new_config;
	struct nlmon_config *old_config;
	int ret;
	
	if (!ctx || !ctx->current)
		return NLMON_CONFIG_ERR_INVALID_VALUE;
	
	fprintf(stderr, "Reloading configuration from %s\n", ctx->current->config_file);
	
	/* Allocate new configuration */
	new_config = calloc(1, sizeof(struct nlmon_config));
	if (!new_config) {
		fprintf(stderr, "Failed to allocate memory for new configuration\n");
		return NLMON_CONFIG_ERR_NOMEM;
	}
	
	/* Initialize with defaults */
	ret = nlmon_config_init(new_config);
	if (ret != NLMON_CONFIG_OK) {
		fprintf(stderr, "Failed to initialize new configuration: %s\n",
		        nlmon_config_error_string(ret));
		free(new_config);
		return ret;
	}
	
	/* Copy config file path */
	strncpy(new_config->config_file, ctx->current->config_file,
	        sizeof(new_config->config_file) - 1);
	
	/* Load configuration from file */
	ret = nlmon_config_load(new_config, new_config->config_file);
	if (ret != NLMON_CONFIG_OK) {
		fprintf(stderr, "Failed to load new configuration: %s\n",
		        nlmon_config_error_string(ret));
		nlmon_config_free(new_config);
		free(new_config);
		return ret;
	}
	
	/* Apply environment variable overrides */
	nlmon_config_apply_env(new_config);
	
	/* Validate new configuration */
	ret = nlmon_config_validate(new_config);
	if (ret != NLMON_CONFIG_OK) {
		fprintf(stderr, "New configuration validation failed: %s\n",
		        nlmon_config_error_string(ret));
		nlmon_config_free(new_config);
		free(new_config);
		return ret;
	}
	
	/* Increment version number */
	new_config->version = ctx->current->version + 1;
	
	/* Atomic swap of configurations */
	pthread_mutex_lock(&ctx->swap_mutex);
	
	/* Store old config for cleanup */
	old_config = ctx->current;
	
	/* Swap to new config */
	ctx->current = new_config;
	
	/* Clear reload flag */
	ctx->reload_requested = false;
	
	pthread_mutex_unlock(&ctx->swap_mutex);
	
	fprintf(stderr, "Configuration reloaded successfully (version %lu)\n",
	        (unsigned long)new_config->version);
	
	/* Wait a bit to ensure no threads are using old config */
	/* In a real implementation, we'd use RCU or reference counting */
	usleep(100000); /* 100ms */
	
	/* Free old configuration */
	if (old_config) {
		nlmon_config_free(old_config);
		free(old_config);
	}
	
	return NLMON_CONFIG_OK;
}

/* Get file descriptor for select/poll integration */
int nlmon_config_watch_get_fd(struct nlmon_config_ctx *ctx)
{
	if (!ctx)
		return -1;
	
	return ctx->watch_fd;
}

/* Check if reload is requested */
bool nlmon_config_reload_requested(struct nlmon_config_ctx *ctx)
{
	if (!ctx)
		return false;
	
	return ctx->reload_requested;
}

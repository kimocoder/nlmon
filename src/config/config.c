/* config.c - Configuration management implementation
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
#include <errno.h>
#include <unistd.h>
#include <sys/inotify.h>
#include "nlmon_config.h"

/* Default configuration values */
#define DEFAULT_BUFFER_SIZE (320 * 1024)
#define DEFAULT_MAX_EVENTS 10000
#define DEFAULT_RATE_LIMIT 1000
#define DEFAULT_WORKER_THREADS 4
#define DEFAULT_CLI_REFRESH_MS 100
#define DEFAULT_CLI_MAX_HISTORY 1000
#define DEFAULT_WEB_PORT 8080
#define DEFAULT_METRICS_PORT 9090
#define DEFAULT_PCAP_ROTATE_SIZE (100 * 1024 * 1024)
#define DEFAULT_DB_RETENTION_DAYS 30

/* Initialize configuration with default values */
int nlmon_config_init(struct nlmon_config *config)
{
	if (!config)
		return NLMON_CONFIG_ERR_INVALID_VALUE;
	
	memset(config, 0, sizeof(*config));
	
	/* Initialize version */
	config->version = 1;
	
	/* Core defaults */
	config->core.buffer_size = DEFAULT_BUFFER_SIZE;
	config->core.max_events = DEFAULT_MAX_EVENTS;
	config->core.rate_limit = DEFAULT_RATE_LIMIT;
	config->core.worker_threads = DEFAULT_WORKER_THREADS;
	
	/* Monitoring defaults */
	config->monitoring.protocol_count = 1;
	config->monitoring.protocols[0] = 0; /* NETLINK_ROUTE */
	config->monitoring.namespaces_enabled = false;
	
	/* Output defaults */
	config->output.console.enabled = true;
	strncpy(config->output.console.format, "text", sizeof(config->output.console.format) - 1);
	
	config->output.pcap.enabled = false;
	config->output.pcap.rotate_size = DEFAULT_PCAP_ROTATE_SIZE;
	
	config->output.database.enabled = false;
	config->output.database.retention_days = DEFAULT_DB_RETENTION_DAYS;
	
	/* CLI defaults */
	config->cli.enabled = false;
	config->cli.refresh_rate_ms = DEFAULT_CLI_REFRESH_MS;
	config->cli.max_history = DEFAULT_CLI_MAX_HISTORY;
	
	/* Web defaults */
	config->web.enabled = false;
	config->web.port = DEFAULT_WEB_PORT;
	config->web.tls.enabled = false;
	
	/* Metrics defaults */
	config->metrics.enabled = false;
	config->metrics.port = DEFAULT_METRICS_PORT;
	strncpy(config->metrics.path, "/metrics", sizeof(config->metrics.path) - 1);
	
	/* Plugins defaults */
	strncpy(config->plugins.directory, "/usr/lib/nlmon/plugins", 
	        sizeof(config->plugins.directory) - 1);
	config->plugins.enabled_count = 0;
	
	/* Integration defaults */
	config->integration.kubernetes.enabled = false;
	config->integration.docker.enabled = false;
	strncpy(config->integration.docker.socket, "/var/run/docker.sock",
	        sizeof(config->integration.docker.socket) - 1);
	config->integration.syslog.enabled = false;
	config->integration.snmp.enabled = false;
	
	/* Initialize rwlock */
	if (pthread_rwlock_init(&config->lock, NULL) != 0)
		return NLMON_CONFIG_ERR_NOMEM;
	
	return NLMON_CONFIG_OK;
}

/* Free configuration resources */
void nlmon_config_free(struct nlmon_config *config)
{
	if (!config)
		return;
	
	pthread_rwlock_destroy(&config->lock);
}

/* Validate configuration values */
int nlmon_config_validate(const struct nlmon_config *config)
{
	if (!config)
		return NLMON_CONFIG_ERR_INVALID_VALUE;
	
	/* Validate core configuration */
	if (config->core.buffer_size < 1024 || config->core.buffer_size > (100 * 1024 * 1024)) {
		fprintf(stderr, "Invalid buffer_size: %zu (must be between 1KB and 100MB)\n",
		        config->core.buffer_size);
		return NLMON_CONFIG_ERR_VALIDATION;
	}
	
	if (config->core.max_events < 100 || config->core.max_events > 1000000) {
		fprintf(stderr, "Invalid max_events: %d (must be between 100 and 1000000)\n",
		        config->core.max_events);
		return NLMON_CONFIG_ERR_VALIDATION;
	}
	
	if (config->core.rate_limit < 0 || config->core.rate_limit > 100000) {
		fprintf(stderr, "Invalid rate_limit: %d (must be between 0 and 100000)\n",
		        config->core.rate_limit);
		return NLMON_CONFIG_ERR_VALIDATION;
	}
	
	if (config->core.worker_threads < 1 || config->core.worker_threads > 64) {
		fprintf(stderr, "Invalid worker_threads: %d (must be between 1 and 64)\n",
		        config->core.worker_threads);
		return NLMON_CONFIG_ERR_VALIDATION;
	}
	
	/* Validate monitoring configuration */
	if (config->monitoring.protocol_count < 0 || 
	    config->monitoring.protocol_count > 8) {
		fprintf(stderr, "Invalid protocol_count: %d\n", config->monitoring.protocol_count);
		return NLMON_CONFIG_ERR_VALIDATION;
	}
	
	/* Validate output configuration */
	if (config->output.pcap.enabled && config->output.pcap.file[0] == '\0') {
		fprintf(stderr, "PCAP output enabled but no file specified\n");
		return NLMON_CONFIG_ERR_VALIDATION;
	}
	
	if (config->output.database.enabled && config->output.database.path[0] == '\0') {
		fprintf(stderr, "Database output enabled but no path specified\n");
		return NLMON_CONFIG_ERR_VALIDATION;
	}
	
	if (config->output.database.retention_days < 1 || 
	    config->output.database.retention_days > 3650) {
		fprintf(stderr, "Invalid retention_days: %d (must be between 1 and 3650)\n",
		        config->output.database.retention_days);
		return NLMON_CONFIG_ERR_VALIDATION;
	}
	
	/* Validate CLI configuration */
	if (config->cli.refresh_rate_ms < 10 || config->cli.refresh_rate_ms > 10000) {
		fprintf(stderr, "Invalid CLI refresh_rate: %d (must be between 10 and 10000 ms)\n",
		        config->cli.refresh_rate_ms);
		return NLMON_CONFIG_ERR_VALIDATION;
	}
	
	/* Validate web configuration */
	if (config->web.enabled) {
		if (config->web.port < 1 || config->web.port > 65535) {
			fprintf(stderr, "Invalid web port: %d\n", config->web.port);
			return NLMON_CONFIG_ERR_VALIDATION;
		}
		
		if (config->web.tls.enabled) {
			if (config->web.tls.cert_file[0] == '\0' || 
			    config->web.tls.key_file[0] == '\0') {
				fprintf(stderr, "TLS enabled but cert or key file not specified\n");
				return NLMON_CONFIG_ERR_VALIDATION;
			}
		}
	}
	
	/* Validate metrics configuration */
	if (config->metrics.enabled) {
		if (config->metrics.port < 1 || config->metrics.port > 65535) {
			fprintf(stderr, "Invalid metrics port: %d\n", config->metrics.port);
			return NLMON_CONFIG_ERR_VALIDATION;
		}
	}
	
	return NLMON_CONFIG_OK;
}

/* Apply environment variable overrides */
void nlmon_config_apply_env(struct nlmon_config *config)
{
	char *env_val;
	
	if (!config)
		return;
	
	/* Core configuration overrides */
	env_val = getenv("NLMON_BUFFER_SIZE");
	if (env_val) {
		long val = strtol(env_val, NULL, 10);
		if (val > 0)
			config->core.buffer_size = (size_t)val;
	}
	
	env_val = getenv("NLMON_MAX_EVENTS");
	if (env_val) {
		long val = strtol(env_val, NULL, 10);
		if (val > 0)
			config->core.max_events = (int)val;
	}
	
	env_val = getenv("NLMON_RATE_LIMIT");
	if (env_val) {
		long val = strtol(env_val, NULL, 10);
		if (val >= 0)
			config->core.rate_limit = (int)val;
	}
	
	env_val = getenv("NLMON_WORKER_THREADS");
	if (env_val) {
		long val = strtol(env_val, NULL, 10);
		if (val > 0)
			config->core.worker_threads = (int)val;
	}
	
	/* CLI configuration overrides */
	env_val = getenv("NLMON_CLI_ENABLED");
	if (env_val) {
		config->cli.enabled = (strcmp(env_val, "1") == 0 || 
		                       strcmp(env_val, "true") == 0 ||
		                       strcmp(env_val, "yes") == 0);
	}
	
	/* Web configuration overrides */
	env_val = getenv("NLMON_WEB_ENABLED");
	if (env_val) {
		config->web.enabled = (strcmp(env_val, "1") == 0 || 
		                       strcmp(env_val, "true") == 0 ||
		                       strcmp(env_val, "yes") == 0);
	}
	
	env_val = getenv("NLMON_WEB_PORT");
	if (env_val) {
		long val = strtol(env_val, NULL, 10);
		if (val > 0 && val <= 65535)
			config->web.port = (int)val;
	}
	
	/* Metrics configuration overrides */
	env_val = getenv("NLMON_METRICS_ENABLED");
	if (env_val) {
		config->metrics.enabled = (strcmp(env_val, "1") == 0 || 
		                           strcmp(env_val, "true") == 0 ||
		                           strcmp(env_val, "yes") == 0);
	}
	
	env_val = getenv("NLMON_METRICS_PORT");
	if (env_val) {
		long val = strtol(env_val, NULL, 10);
		if (val > 0 && val <= 65535)
			config->metrics.port = (int)val;
	}
}

/* Type-safe accessor functions */

void nlmon_config_get_core(struct nlmon_config_ctx *ctx, 
                           struct nlmon_core_config *core)
{
	if (!ctx || !ctx->current || !core)
		return;
	
	pthread_rwlock_rdlock(&ctx->current->lock);
	memcpy(core, &ctx->current->core, sizeof(*core));
	pthread_rwlock_unlock(&ctx->current->lock);
}

void nlmon_config_get_monitoring(struct nlmon_config_ctx *ctx,
                                 struct nlmon_monitoring_config *monitoring)
{
	if (!ctx || !ctx->current || !monitoring)
		return;
	
	pthread_rwlock_rdlock(&ctx->current->lock);
	memcpy(monitoring, &ctx->current->monitoring, sizeof(*monitoring));
	pthread_rwlock_unlock(&ctx->current->lock);
}

void nlmon_config_get_output(struct nlmon_config_ctx *ctx,
                             struct nlmon_output_config *output)
{
	if (!ctx || !ctx->current || !output)
		return;
	
	pthread_rwlock_rdlock(&ctx->current->lock);
	memcpy(output, &ctx->current->output, sizeof(*output));
	pthread_rwlock_unlock(&ctx->current->lock);
}

void nlmon_config_get_cli(struct nlmon_config_ctx *ctx,
                          struct nlmon_cli_config *cli)
{
	if (!ctx || !ctx->current || !cli)
		return;
	
	pthread_rwlock_rdlock(&ctx->current->lock);
	memcpy(cli, &ctx->current->cli, sizeof(*cli));
	pthread_rwlock_unlock(&ctx->current->lock);
}

void nlmon_config_get_web(struct nlmon_config_ctx *ctx,
                          struct nlmon_web_config *web)
{
	if (!ctx || !ctx->current || !web)
		return;
	
	pthread_rwlock_rdlock(&ctx->current->lock);
	memcpy(web, &ctx->current->web, sizeof(*web));
	pthread_rwlock_unlock(&ctx->current->lock);
}

uint64_t nlmon_config_get_version(struct nlmon_config_ctx *ctx)
{
	uint64_t version;
	
	if (!ctx || !ctx->current)
		return 0;
	
	pthread_rwlock_rdlock(&ctx->current->lock);
	version = ctx->current->version;
	pthread_rwlock_unlock(&ctx->current->lock);
	
	return version;
}

/* Configuration context management */

int nlmon_config_ctx_init(struct nlmon_config_ctx *ctx, const char *config_file)
{
	int ret;
	
	if (!ctx)
		return NLMON_CONFIG_ERR_INVALID_VALUE;
	
	memset(ctx, 0, sizeof(*ctx));
	
	/* Allocate current configuration */
	ctx->current = calloc(1, sizeof(struct nlmon_config));
	if (!ctx->current)
		return NLMON_CONFIG_ERR_NOMEM;
	
	/* Initialize with defaults */
	ret = nlmon_config_init(ctx->current);
	if (ret != NLMON_CONFIG_OK) {
		free(ctx->current);
		ctx->current = NULL;
		return ret;
	}
	
	/* Load configuration from file if provided */
	if (config_file) {
		strncpy(ctx->current->config_file, config_file, 
		        sizeof(ctx->current->config_file) - 1);
		
		ret = nlmon_config_load(ctx->current, config_file);
		if (ret != NLMON_CONFIG_OK) {
			fprintf(stderr, "Warning: Failed to load config from %s, using defaults\n",
			        config_file);
		}
	}
	
	/* Apply environment variable overrides */
	nlmon_config_apply_env(ctx->current);
	
	/* Validate configuration */
	ret = nlmon_config_validate(ctx->current);
	if (ret != NLMON_CONFIG_OK) {
		nlmon_config_free(ctx->current);
		free(ctx->current);
		ctx->current = NULL;
		return ret;
	}
	
	/* Initialize swap mutex */
	if (pthread_mutex_init(&ctx->swap_mutex, NULL) != 0) {
		nlmon_config_free(ctx->current);
		free(ctx->current);
		ctx->current = NULL;
		return NLMON_CONFIG_ERR_NOMEM;
	}
	
	ctx->watch_fd = -1;
	ctx->watch_wd = -1;
	ctx->reload_requested = false;
	
	return NLMON_CONFIG_OK;
}

void nlmon_config_ctx_free(struct nlmon_config_ctx *ctx)
{
	if (!ctx)
		return;
	
	/* Stop watching */
	if (ctx->watch_fd >= 0) {
		if (ctx->watch_wd >= 0)
			inotify_rm_watch(ctx->watch_fd, ctx->watch_wd);
		close(ctx->watch_fd);
	}
	
	/* Free configurations */
	if (ctx->current) {
		nlmon_config_free(ctx->current);
		free(ctx->current);
	}
	
	if (ctx->pending) {
		nlmon_config_free(ctx->pending);
		free(ctx->pending);
	}
	
	pthread_mutex_destroy(&ctx->swap_mutex);
}

const char *nlmon_config_error_string(enum nlmon_config_error error)
{
	switch (error) {
	case NLMON_CONFIG_OK:
		return "Success";
	case NLMON_CONFIG_ERR_NOMEM:
		return "Out of memory";
	case NLMON_CONFIG_ERR_FILE_NOT_FOUND:
		return "Configuration file not found";
	case NLMON_CONFIG_ERR_PARSE_ERROR:
		return "Failed to parse configuration file";
	case NLMON_CONFIG_ERR_INVALID_VALUE:
		return "Invalid configuration value";
	case NLMON_CONFIG_ERR_MISSING_REQUIRED:
		return "Missing required configuration parameter";
	case NLMON_CONFIG_ERR_VALIDATION:
		return "Configuration validation failed";
	default:
		return "Unknown error";
	}
}

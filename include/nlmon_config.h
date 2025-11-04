/* nlmon_config.h - Configuration management structures and API
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

#ifndef NLMON_CONFIG_H
#define NLMON_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

/* Maximum string lengths */
#define NLMON_MAX_PATH 256
#define NLMON_MAX_NAME 64
#define NLMON_MAX_PATTERN 128
#define NLMON_MAX_EXPRESSION 512
#define NLMON_MAX_INTERFACES 32
#define NLMON_MAX_MSG_TYPES 32
#define NLMON_MAX_FILTERS 16
#define NLMON_MAX_ALERTS 32
#define NLMON_MAX_TRAP_RECEIVERS 8

/* Configuration error codes */
enum nlmon_config_error {
	NLMON_CONFIG_OK = 0,
	NLMON_CONFIG_ERR_NOMEM,
	NLMON_CONFIG_ERR_FILE_NOT_FOUND,
	NLMON_CONFIG_ERR_PARSE_ERROR,
	NLMON_CONFIG_ERR_INVALID_VALUE,
	NLMON_CONFIG_ERR_MISSING_REQUIRED,
	NLMON_CONFIG_ERR_VALIDATION
};

/* Core configuration */
struct nlmon_core_config {
	size_t buffer_size;           /* Event buffer size in bytes */
	int max_events;               /* Maximum events in memory buffer */
	int rate_limit;               /* Events per second limit */
	int worker_threads;           /* Number of worker threads */
};

/* Monitoring configuration */
struct nlmon_monitoring_config {
	int protocol_count;
	int protocols[8];             /* Netlink protocol families to monitor */
	
	int include_count;
	char include_patterns[NLMON_MAX_INTERFACES][NLMON_MAX_PATTERN];
	
	int exclude_count;
	char exclude_patterns[NLMON_MAX_INTERFACES][NLMON_MAX_PATTERN];
	
	int msg_type_count;
	int msg_types[NLMON_MAX_MSG_TYPES];
	
	bool namespaces_enabled;
};

/* Filter configuration */
struct nlmon_filter_config {
	char name[NLMON_MAX_NAME];
	char expression[NLMON_MAX_EXPRESSION];
	bool enabled;
};

/* Output console configuration */
struct nlmon_output_console_config {
	bool enabled;
	char format[16];              /* "text" or "json" */
};

/* Output PCAP configuration */
struct nlmon_output_pcap_config {
	bool enabled;
	char file[NLMON_MAX_PATH];
	size_t rotate_size;           /* Rotation size in bytes */
};

/* Output database configuration */
struct nlmon_output_db_config {
	bool enabled;
	char path[NLMON_MAX_PATH];
	int retention_days;
};

/* Output configuration */
struct nlmon_output_config {
	struct nlmon_output_console_config console;
	struct nlmon_output_pcap_config pcap;
	struct nlmon_output_db_config database;
};

/* CLI configuration */
struct nlmon_cli_config {
	bool enabled;
	int refresh_rate_ms;          /* Refresh rate in milliseconds */
	int max_history;              /* Maximum events in history */
};

/* TLS configuration */
struct nlmon_tls_config {
	bool enabled;
	char cert_file[NLMON_MAX_PATH];
	char key_file[NLMON_MAX_PATH];
};

/* Web dashboard configuration */
struct nlmon_web_config {
	bool enabled;
	int port;
	struct nlmon_tls_config tls;
};

/* Metrics configuration */
struct nlmon_metrics_config {
	bool enabled;
	int port;
	char path[NLMON_MAX_PATH];
};

/* Plugin configuration */
struct nlmon_plugins_config {
	char directory[NLMON_MAX_PATH];
	int enabled_count;
	char enabled_plugins[16][NLMON_MAX_NAME];
};

/* Alert action types */
enum nlmon_alert_action_type {
	NLMON_ALERT_ACTION_EXEC,
	NLMON_ALERT_ACTION_LOG,
	NLMON_ALERT_ACTION_WEBHOOK
};

/* Alert configuration */
struct nlmon_alert_config {
	char name[NLMON_MAX_NAME];
	char condition[NLMON_MAX_EXPRESSION];
	enum nlmon_alert_action_type action_type;
	char action_param[NLMON_MAX_PATH];
	bool enabled;
};

/* Kubernetes integration configuration */
struct nlmon_k8s_config {
	bool enabled;
	char kubeconfig[NLMON_MAX_PATH];
};

/* Docker integration configuration */
struct nlmon_docker_config {
	bool enabled;
	char socket[NLMON_MAX_PATH];
};

/* Syslog integration configuration */
struct nlmon_syslog_config {
	bool enabled;
	char server[256];
	char protocol[8];             /* "tcp" or "udp" */
};

/* SNMP integration configuration */
struct nlmon_snmp_config {
	bool enabled;
	int receiver_count;
	char trap_receivers[NLMON_MAX_TRAP_RECEIVERS][256];
};

/* Event hook configuration */
#define NLMON_MAX_HOOKS 32
struct nlmon_hook_config {
	char name[NLMON_MAX_NAME];
	char script[NLMON_MAX_PATH];
	char condition[NLMON_MAX_EXPRESSION];
	uint32_t timeout_ms;
	bool enabled;
	bool async;
};

/* Integration configuration */
struct nlmon_integration_config {
	struct nlmon_k8s_config kubernetes;
	struct nlmon_docker_config docker;
	struct nlmon_syslog_config syslog;
	struct nlmon_snmp_config snmp;
	
	/* Event hooks */
	int hook_count;
	struct nlmon_hook_config hooks[NLMON_MAX_HOOKS];
};

/* Main configuration structure */
struct nlmon_config {
	/* Configuration metadata */
	uint64_t version;             /* Configuration version for hot-reload */
	char config_file[NLMON_MAX_PATH];
	
	/* Configuration sections */
	struct nlmon_core_config core;
	struct nlmon_monitoring_config monitoring;
	
	int filter_count;
	struct nlmon_filter_config filters[NLMON_MAX_FILTERS];
	
	struct nlmon_output_config output;
	struct nlmon_cli_config cli;
	struct nlmon_web_config web;
	struct nlmon_metrics_config metrics;
	struct nlmon_plugins_config plugins;
	
	int alert_count;
	struct nlmon_alert_config alerts[NLMON_MAX_ALERTS];
	
	struct nlmon_integration_config integration;
	
	/* Thread safety */
	pthread_rwlock_t lock;
};

/* Configuration context for thread-safe access */
struct nlmon_config_ctx {
	struct nlmon_config *current;
	struct nlmon_config *pending;
	pthread_mutex_t swap_mutex;
	int watch_fd;                 /* inotify file descriptor */
	int watch_wd;                 /* inotify watch descriptor */
	bool reload_requested;
};

/* Configuration API */

/**
 * nlmon_config_init - Initialize configuration with defaults
 * @config: Configuration structure to initialize
 *
 * Returns: NLMON_CONFIG_OK on success, error code otherwise
 */
int nlmon_config_init(struct nlmon_config *config);

/**
 * nlmon_config_free - Free configuration resources
 * @config: Configuration structure to free
 */
void nlmon_config_free(struct nlmon_config *config);

/**
 * nlmon_config_load - Load configuration from file
 * @config: Configuration structure to populate
 * @filename: Path to configuration file
 *
 * Returns: NLMON_CONFIG_OK on success, error code otherwise
 */
int nlmon_config_load(struct nlmon_config *config, const char *filename);

/**
 * nlmon_config_validate - Validate configuration values
 * @config: Configuration structure to validate
 *
 * Returns: NLMON_CONFIG_OK if valid, error code otherwise
 */
int nlmon_config_validate(const struct nlmon_config *config);

/**
 * nlmon_config_apply_env - Apply environment variable overrides
 * @config: Configuration structure to modify
 *
 * Environment variables with NLMON_ prefix override config values
 */
void nlmon_config_apply_env(struct nlmon_config *config);

/* Type-safe accessor functions */

/**
 * nlmon_config_get_core - Get core configuration (thread-safe)
 * @ctx: Configuration context
 * @core: Output buffer for core configuration
 */
void nlmon_config_get_core(struct nlmon_config_ctx *ctx, 
                           struct nlmon_core_config *core);

/**
 * nlmon_config_get_monitoring - Get monitoring configuration (thread-safe)
 * @ctx: Configuration context
 * @monitoring: Output buffer for monitoring configuration
 */
void nlmon_config_get_monitoring(struct nlmon_config_ctx *ctx,
                                 struct nlmon_monitoring_config *monitoring);

/**
 * nlmon_config_get_output - Get output configuration (thread-safe)
 * @ctx: Configuration context
 * @output: Output buffer for output configuration
 */
void nlmon_config_get_output(struct nlmon_config_ctx *ctx,
                             struct nlmon_output_config *output);

/**
 * nlmon_config_get_cli - Get CLI configuration (thread-safe)
 * @ctx: Configuration context
 * @cli: Output buffer for CLI configuration
 */
void nlmon_config_get_cli(struct nlmon_config_ctx *ctx,
                          struct nlmon_cli_config *cli);

/**
 * nlmon_config_get_web - Get web configuration (thread-safe)
 * @ctx: Configuration context
 * @web: Output buffer for web configuration
 */
void nlmon_config_get_web(struct nlmon_config_ctx *ctx,
                          struct nlmon_web_config *web);

/**
 * nlmon_config_get_version - Get current configuration version
 * @ctx: Configuration context
 *
 * Returns: Current configuration version number
 */
uint64_t nlmon_config_get_version(struct nlmon_config_ctx *ctx);

/* Configuration context management */

/**
 * nlmon_config_ctx_init - Initialize configuration context
 * @ctx: Configuration context to initialize
 * @config_file: Path to configuration file
 *
 * Returns: NLMON_CONFIG_OK on success, error code otherwise
 */
int nlmon_config_ctx_init(struct nlmon_config_ctx *ctx, const char *config_file);

/**
 * nlmon_config_ctx_free - Free configuration context
 * @ctx: Configuration context to free
 */
void nlmon_config_ctx_free(struct nlmon_config_ctx *ctx);

/**
 * nlmon_config_ctx_reload - Reload configuration from file
 * @ctx: Configuration context
 *
 * Returns: NLMON_CONFIG_OK on success, error code otherwise
 */
int nlmon_config_ctx_reload(struct nlmon_config_ctx *ctx);

/**
 * nlmon_config_watch_init - Initialize configuration file watching
 * @ctx: Configuration context
 *
 * Returns: NLMON_CONFIG_OK on success, error code otherwise
 */
int nlmon_config_watch_init(struct nlmon_config_ctx *ctx);

/**
 * nlmon_config_watch_check - Check for configuration file changes
 * @ctx: Configuration context
 *
 * Returns: true if reload is needed, false otherwise
 */
bool nlmon_config_watch_check(struct nlmon_config_ctx *ctx);

/**
 * nlmon_config_error_string - Get error message for error code
 * @error: Error code
 *
 * Returns: Human-readable error message
 */
const char *nlmon_config_error_string(enum nlmon_config_error error);

/**
 * nlmon_config_watch_get_fd - Get inotify file descriptor
 * @ctx: Configuration context
 *
 * Returns: File descriptor for select/poll integration, or -1 if not initialized
 */
int nlmon_config_watch_get_fd(struct nlmon_config_ctx *ctx);

/**
 * nlmon_config_reload_requested - Check if reload is requested
 * @ctx: Configuration context
 *
 * Returns: true if reload is requested, false otherwise
 */
bool nlmon_config_reload_requested(struct nlmon_config_ctx *ctx);

#endif /* NLMON_CONFIG_H */

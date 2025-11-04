/* alert_manager.h - Alert system for network event monitoring
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

#ifndef ALERT_MANAGER_H
#define ALERT_MANAGER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "event_processor.h"

/* Maximum lengths */
#define ALERT_MAX_NAME 64
#define ALERT_MAX_CONDITION 512
#define ALERT_MAX_ACTION 256
#define ALERT_MAX_MESSAGE 1024
#define ALERT_MAX_WEBHOOK_URL 512

/* Alert severity levels */
enum alert_severity {
	ALERT_SEVERITY_INFO = 0,
	ALERT_SEVERITY_WARNING,
	ALERT_SEVERITY_ERROR,
	ALERT_SEVERITY_CRITICAL
};

/* Alert action types */
enum alert_action_type {
	ALERT_ACTION_EXEC,      /* Execute script */
	ALERT_ACTION_LOG,       /* Write to log file */
	ALERT_ACTION_WEBHOOK    /* HTTP POST to webhook */
};

/* Alert state */
enum alert_state {
	ALERT_STATE_INACTIVE = 0,
	ALERT_STATE_ACTIVE,
	ALERT_STATE_ACKNOWLEDGED,
	ALERT_STATE_SUPPRESSED
};

/* Alert action configuration */
struct alert_action {
	enum alert_action_type type;
	union {
		struct {
			char script[ALERT_MAX_ACTION];
			uint32_t timeout_ms;
		} exec;
		struct {
			char log_file[ALERT_MAX_ACTION];
			bool append;
		} log;
		struct {
			char url[ALERT_MAX_WEBHOOK_URL];
			char method[8];  /* GET, POST */
			uint32_t timeout_ms;
		} webhook;
	} params;
};

/* Alert rule configuration */
struct alert_rule {
	char name[ALERT_MAX_NAME];
	char condition[ALERT_MAX_CONDITION];
	enum alert_severity severity;
	struct alert_action action;
	bool enabled;
	
	/* Rate limiting */
	uint32_t rate_limit_count;    /* Max triggers in time window */
	uint32_t rate_limit_window_s; /* Time window in seconds */
	
	/* Suppression */
	uint32_t suppress_duration_s; /* Suppress for N seconds after trigger */
};

/* Alert instance (triggered alert) */
struct alert_instance {
	uint64_t id;
	char rule_name[ALERT_MAX_NAME];
	enum alert_severity severity;
	enum alert_state state;
	time_t triggered_at;
	time_t acknowledged_at;
	time_t resolved_at;
	uint64_t event_sequence;      /* Event that triggered alert */
	char message[ALERT_MAX_MESSAGE];
	char acknowledged_by[64];
};

/* Alert statistics */
struct alert_stats {
	unsigned long total_triggered;
	unsigned long total_executed;
	unsigned long total_failed;
	unsigned long total_suppressed;
	unsigned long total_rate_limited;
	unsigned long active_count;
	unsigned long acknowledged_count;
};

/* Alert manager structure (opaque) */
struct alert_manager;

/**
 * alert_manager_create() - Create alert manager
 * @max_rules: Maximum number of alert rules
 * @max_history: Maximum alert history entries
 *
 * Returns: Pointer to alert manager or NULL on error
 */
struct alert_manager *alert_manager_create(size_t max_rules, size_t max_history);

/**
 * alert_manager_destroy() - Destroy alert manager
 * @am: Alert manager
 */
void alert_manager_destroy(struct alert_manager *am);

/**
 * alert_manager_add_rule() - Add alert rule
 * @am: Alert manager
 * @rule: Alert rule configuration
 *
 * Returns: Rule ID or -1 on error
 */
int alert_manager_add_rule(struct alert_manager *am, const struct alert_rule *rule);

/**
 * alert_manager_remove_rule() - Remove alert rule
 * @am: Alert manager
 * @rule_id: Rule ID
 *
 * Returns: true on success
 */
bool alert_manager_remove_rule(struct alert_manager *am, int rule_id);

/**
 * alert_manager_enable_rule() - Enable alert rule
 * @am: Alert manager
 * @rule_id: Rule ID
 *
 * Returns: true on success
 */
bool alert_manager_enable_rule(struct alert_manager *am, int rule_id);

/**
 * alert_manager_disable_rule() - Disable alert rule
 * @am: Alert manager
 * @rule_id: Rule ID
 *
 * Returns: true on success
 */
bool alert_manager_disable_rule(struct alert_manager *am, int rule_id);

/**
 * alert_manager_evaluate() - Evaluate event against alert rules
 * @am: Alert manager
 * @event: Event to evaluate
 *
 * This function evaluates all enabled alert rules against the event
 * and triggers matching alerts.
 */
void alert_manager_evaluate(struct alert_manager *am, struct nlmon_event *event);

/**
 * alert_manager_acknowledge() - Acknowledge an alert
 * @am: Alert manager
 * @alert_id: Alert instance ID
 * @acknowledged_by: User/system acknowledging the alert
 *
 * Returns: true on success
 */
bool alert_manager_acknowledge(struct alert_manager *am, uint64_t alert_id,
                               const char *acknowledged_by);

/**
 * alert_manager_resolve() - Resolve an alert
 * @am: Alert manager
 * @alert_id: Alert instance ID
 *
 * Returns: true on success
 */
bool alert_manager_resolve(struct alert_manager *am, uint64_t alert_id);

/**
 * alert_manager_get_active() - Get active alerts
 * @am: Alert manager
 * @alerts: Output buffer for alert instances
 * @max_alerts: Maximum number of alerts to return
 *
 * Returns: Number of active alerts returned
 */
size_t alert_manager_get_active(struct alert_manager *am,
                                struct alert_instance *alerts,
                                size_t max_alerts);

/**
 * alert_manager_get_history() - Get alert history
 * @am: Alert manager
 * @alerts: Output buffer for alert instances
 * @max_alerts: Maximum number of alerts to return
 * @since: Only return alerts triggered after this time (0 for all)
 *
 * Returns: Number of alerts returned
 */
size_t alert_manager_get_history(struct alert_manager *am,
                                 struct alert_instance *alerts,
                                 size_t max_alerts,
                                 time_t since);

/**
 * alert_manager_get_stats() - Get alert statistics
 * @am: Alert manager
 * @stats: Output buffer for statistics
 *
 * Returns: true on success
 */
bool alert_manager_get_stats(struct alert_manager *am, struct alert_stats *stats);

/**
 * alert_manager_reset_stats() - Reset alert statistics
 * @am: Alert manager
 */
void alert_manager_reset_stats(struct alert_manager *am);

/**
 * alert_manager_list_rules() - List all alert rules
 * @am: Alert manager
 * @rule_ids: Output buffer for rule IDs
 * @max_rules: Maximum number of rule IDs to return
 *
 * Returns: Number of rules returned
 */
size_t alert_manager_list_rules(struct alert_manager *am, int *rule_ids,
                                size_t max_rules);

/**
 * alert_manager_get_rule() - Get alert rule configuration
 * @am: Alert manager
 * @rule_id: Rule ID
 * @rule: Output buffer for rule configuration
 *
 * Returns: true on success
 */
bool alert_manager_get_rule(struct alert_manager *am, int rule_id,
                            struct alert_rule *rule);

/**
 * alert_manager_suppress() - Suppress an alert rule temporarily
 * @am: Alert manager
 * @rule_id: Rule ID
 * @duration_s: Suppression duration in seconds
 *
 * Returns: true on success
 */
bool alert_manager_suppress(struct alert_manager *am, int rule_id,
                            uint32_t duration_s);

/**
 * alert_manager_unsuppress() - Remove suppression from alert rule
 * @am: Alert manager
 * @rule_id: Rule ID
 *
 * Returns: true on success
 */
bool alert_manager_unsuppress(struct alert_manager *am, int rule_id);

#endif /* ALERT_MANAGER_H */

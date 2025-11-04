/* alert_manager.c - Alert system implementation
 *
 * Implements alert rule engine with condition evaluation, action execution,
 * state management, rate limiting, and acknowledgment system.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <curl/curl.h>
#include "alert_manager.h"
#include "filter_parser.h"
#include "filter_compiler.h"
#include "filter_eval.h"

/* Alert rule entry */
struct alert_rule_entry {
	int id;
	struct alert_rule rule;
	struct filter_bytecode *filter;  /* Compiled condition */
	bool in_use;
	
	/* Rate limiting state */
	time_t *trigger_times;           /* Circular buffer of trigger times */
	size_t trigger_count;
	size_t trigger_index;
	
	/* Suppression state */
	bool suppressed;
	time_t suppress_until;
	
	/* Statistics */
	unsigned long triggered_count;
	unsigned long executed_count;
	unsigned long failed_count;
	unsigned long suppressed_count;
	unsigned long rate_limited_count;
};

/* Alert manager structure */
struct alert_manager {
	struct alert_rule_entry *rules;
	size_t max_rules;
	int next_rule_id;
	pthread_mutex_t rules_mutex;
	
	/* Alert history */
	struct alert_instance *history;
	size_t max_history;
	size_t history_count;
	size_t history_index;
	uint64_t next_alert_id;
	pthread_mutex_t history_mutex;
	
	/* Global statistics */
	struct alert_stats stats;
	pthread_mutex_t stats_mutex;
};

/* Helper: Get current time */
static time_t get_current_time(void)
{
	return time(NULL);
}

/* Helper: Execute script action */
static bool execute_script_action(const struct alert_action *action,
                                  struct nlmon_event *event,
                                  const char *alert_name)
{
	pid_t pid;
	int status;
	char **envp;
	int count = 0;
	char buf[256];
	time_t start_time;
	uint32_t timeout_ms = action->params.exec.timeout_ms;
	
	if (timeout_ms == 0)
		timeout_ms = 30000;  /* 30 seconds default */
	
	/* Build environment variables */
	envp = calloc(32, sizeof(char *));
	if (!envp)
		return false;
	
	snprintf(buf, sizeof(buf), "NLMON_ALERT_NAME=%s", alert_name);
	envp[count++] = strdup(buf);
	
	snprintf(buf, sizeof(buf), "NLMON_TIMESTAMP=%lu", event->timestamp);
	envp[count++] = strdup(buf);
	
	snprintf(buf, sizeof(buf), "NLMON_SEQUENCE=%lu", event->sequence);
	envp[count++] = strdup(buf);
	
	snprintf(buf, sizeof(buf), "NLMON_EVENT_TYPE=%u", event->event_type);
	envp[count++] = strdup(buf);
	
	snprintf(buf, sizeof(buf), "NLMON_MESSAGE_TYPE=%u", event->message_type);
	envp[count++] = strdup(buf);
	
	if (event->interface[0] != '\0') {
		snprintf(buf, sizeof(buf), "NLMON_INTERFACE=%s", event->interface);
		envp[count++] = strdup(buf);
	}
	
	envp[count++] = strdup("PATH=/usr/local/bin:/usr/bin:/bin");
	envp[count] = NULL;
	
	/* Fork and execute */
	start_time = time(NULL);
	pid = fork();
	
	if (pid < 0) {
		/* Fork failed */
		for (int i = 0; envp[i]; i++)
			free(envp[i]);
		free(envp);
		return false;
	}
	
	if (pid == 0) {
		/* Child process */
		int devnull = open("/dev/null", O_WRONLY);
		if (devnull >= 0) {
			dup2(devnull, STDOUT_FILENO);
			dup2(devnull, STDERR_FILENO);
			close(devnull);
		}
		
		execle("/bin/sh", "sh", "-c", action->params.exec.script, NULL, envp);
		_exit(127);
	}
	
	/* Parent process - wait with timeout */
	while (1) {
		pid_t result = waitpid(pid, &status, WNOHANG);
		
		if (result == pid) {
			/* Child exited */
			for (int i = 0; envp[i]; i++)
				free(envp[i]);
			free(envp);
			return WIFEXITED(status) && WEXITSTATUS(status) == 0;
		}
		
		if (result < 0) {
			if (errno == EINTR)
				continue;
			for (int i = 0; envp[i]; i++)
				free(envp[i]);
			free(envp);
			return false;
		}
		
		/* Check timeout */
		if ((time(NULL) - start_time) * 1000 >= timeout_ms) {
			kill(pid, SIGKILL);
			waitpid(pid, &status, 0);
			for (int i = 0; envp[i]; i++)
				free(envp[i]);
			free(envp);
			return false;
		}
		
		usleep(10000);  /* 10ms */
	}
}

/* Helper: Execute log action */
static bool execute_log_action(const struct alert_action *action,
                               struct nlmon_event *event,
                               const char *alert_name,
                               enum alert_severity severity)
{
	FILE *fp;
	const char *mode = action->params.log.append ? "a" : "w";
	const char *severity_str;
	time_t now = time(NULL);
	char time_buf[64];
	
	switch (severity) {
	case ALERT_SEVERITY_INFO:
		severity_str = "INFO";
		break;
	case ALERT_SEVERITY_WARNING:
		severity_str = "WARNING";
		break;
	case ALERT_SEVERITY_ERROR:
		severity_str = "ERROR";
		break;
	case ALERT_SEVERITY_CRITICAL:
		severity_str = "CRITICAL";
		break;
	default:
		severity_str = "UNKNOWN";
	}
	
	fp = fopen(action->params.log.log_file, mode);
	if (!fp)
		return false;
	
	strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S",
	         localtime(&now));
	
	fprintf(fp, "[%s] [%s] Alert '%s' triggered by event seq=%lu type=%u interface=%s\n",
	        time_buf, severity_str, alert_name, event->sequence,
	        event->message_type, event->interface);
	
	fclose(fp);
	return true;
}

/* Helper: Webhook response callback */
static size_t webhook_write_callback(void *contents, size_t size, size_t nmemb,
                                     void *userp)
{
	/* Discard response data */
	return size * nmemb;
}

/* Helper: Execute webhook action */
static bool execute_webhook_action(const struct alert_action *action,
                                   struct nlmon_event *event,
                                   const char *alert_name,
                                   enum alert_severity severity)
{
	CURL *curl;
	CURLcode res;
	char json_payload[2048];
	struct curl_slist *headers = NULL;
	const char *severity_str;
	bool success = false;
	
	switch (severity) {
	case ALERT_SEVERITY_INFO:
		severity_str = "info";
		break;
	case ALERT_SEVERITY_WARNING:
		severity_str = "warning";
		break;
	case ALERT_SEVERITY_ERROR:
		severity_str = "error";
		break;
	case ALERT_SEVERITY_CRITICAL:
		severity_str = "critical";
		break;
	default:
		severity_str = "unknown";
	}
	
	/* Build JSON payload */
	snprintf(json_payload, sizeof(json_payload),
	         "{"
	         "\"alert_name\":\"%s\","
	         "\"severity\":\"%s\","
	         "\"timestamp\":%lu,"
	         "\"event\":{"
	         "\"sequence\":%lu,"
	         "\"type\":%u,"
	         "\"message_type\":%u,"
	         "\"interface\":\"%s\""
	         "}"
	         "}",
	         alert_name, severity_str, (unsigned long)time(NULL),
	         event->sequence, event->event_type, event->message_type,
	         event->interface);
	
	curl = curl_easy_init();
	if (!curl)
		return false;
	
	headers = curl_slist_append(headers, "Content-Type: application/json");
	
	curl_easy_setopt(curl, CURLOPT_URL, action->params.webhook.url);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_payload);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, webhook_write_callback);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS,
	                 action->params.webhook.timeout_ms > 0 ?
	                 action->params.webhook.timeout_ms : 10000);
	
	res = curl_easy_perform(curl);
	success = (res == CURLE_OK);
	
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	
	return success;
}

/* Helper: Execute alert action */
static bool execute_alert_action(const struct alert_action *action,
                                struct nlmon_event *event,
                                const char *alert_name,
                                enum alert_severity severity)
{
	switch (action->type) {
	case ALERT_ACTION_EXEC:
		return execute_script_action(action, event, alert_name);
	case ALERT_ACTION_LOG:
		return execute_log_action(action, event, alert_name, severity);
	case ALERT_ACTION_WEBHOOK:
		return execute_webhook_action(action, event, alert_name, severity);
	default:
		return false;
	}
}

struct alert_manager *alert_manager_create(size_t max_rules, size_t max_history)
{
	struct alert_manager *am;
	
	if (max_rules == 0)
		max_rules = 32;
	if (max_history == 0)
		max_history = 1000;
	
	am = calloc(1, sizeof(*am));
	if (!am)
		return NULL;
	
	am->rules = calloc(max_rules, sizeof(struct alert_rule_entry));
	if (!am->rules) {
		free(am);
		return NULL;
	}
	
	am->history = calloc(max_history, sizeof(struct alert_instance));
	if (!am->history) {
		free(am->rules);
		free(am);
		return NULL;
	}
	
	am->max_rules = max_rules;
	am->max_history = max_history;
	am->next_rule_id = 1;
	am->next_alert_id = 1;
	
	if (pthread_mutex_init(&am->rules_mutex, NULL) != 0 ||
	    pthread_mutex_init(&am->history_mutex, NULL) != 0 ||
	    pthread_mutex_init(&am->stats_mutex, NULL) != 0) {
		free(am->history);
		free(am->rules);
		free(am);
		return NULL;
	}
	
	/* Initialize libcurl */
	curl_global_init(CURL_GLOBAL_DEFAULT);
	
	return am;
}

void alert_manager_destroy(struct alert_manager *am)
{
	size_t i;
	
	if (!am)
		return;
	
	/* Cleanup rules */
	for (i = 0; i < am->max_rules; i++) {
		if (am->rules[i].in_use) {
			if (am->rules[i].filter)
				filter_bytecode_free(am->rules[i].filter);
			if (am->rules[i].trigger_times)
				free(am->rules[i].trigger_times);
		}
	}
	
	free(am->rules);
	free(am->history);
	
	pthread_mutex_destroy(&am->rules_mutex);
	pthread_mutex_destroy(&am->history_mutex);
	pthread_mutex_destroy(&am->stats_mutex);
	
	curl_global_cleanup();
	
	free(am);
}

int alert_manager_add_rule(struct alert_manager *am, const struct alert_rule *rule)
{
	struct alert_rule_entry *entry = NULL;
	size_t i;
	int id;
	
	if (!am || !rule)
		return -1;
	
	/* Validate rule */
	if (rule->name[0] == '\0' || rule->condition[0] == '\0')
		return -1;
	
	pthread_mutex_lock(&am->rules_mutex);
	
	/* Find free slot */
	for (i = 0; i < am->max_rules; i++) {
		if (!am->rules[i].in_use) {
			entry = &am->rules[i];
			break;
		}
	}
	
	if (!entry) {
		pthread_mutex_unlock(&am->rules_mutex);
		return -1;
	}
	
	/* Initialize entry */
	memset(entry, 0, sizeof(*entry));
	id = am->next_rule_id++;
	entry->id = id;
	entry->rule = *rule;
	entry->in_use = true;
	
	/* Compile filter condition */
	struct filter_expr *expr = filter_parse(rule->condition);
	if (!expr || !expr->valid) {
		if (expr)
			filter_expr_free(expr);
		entry->in_use = false;
		pthread_mutex_unlock(&am->rules_mutex);
		return -1;
	}
	
	entry->filter = filter_compile(expr);
	filter_expr_free(expr);
	
	if (!entry->filter) {
		entry->in_use = false;
		pthread_mutex_unlock(&am->rules_mutex);
		return -1;
	}
	
	/* Initialize rate limiting buffer if needed */
	if (rule->rate_limit_count > 0 && rule->rate_limit_window_s > 0) {
		entry->trigger_times = calloc(rule->rate_limit_count, sizeof(time_t));
		if (!entry->trigger_times) {
			filter_bytecode_free(entry->filter);
			entry->in_use = false;
			pthread_mutex_unlock(&am->rules_mutex);
			return -1;
		}
	}
	
	pthread_mutex_unlock(&am->rules_mutex);
	
	return id;
}

bool alert_manager_remove_rule(struct alert_manager *am, int rule_id)
{
	struct alert_rule_entry *entry = NULL;
	size_t i;
	
	if (!am)
		return false;
	
	pthread_mutex_lock(&am->rules_mutex);
	
	for (i = 0; i < am->max_rules; i++) {
		if (am->rules[i].in_use && am->rules[i].id == rule_id) {
			entry = &am->rules[i];
			break;
		}
	}
	
	if (!entry) {
		pthread_mutex_unlock(&am->rules_mutex);
		return false;
	}
	
	/* Cleanup */
	if (entry->filter)
		filter_bytecode_free(entry->filter);
	if (entry->trigger_times)
		free(entry->trigger_times);
	
	entry->in_use = false;
	
	pthread_mutex_unlock(&am->rules_mutex);
	
	return true;
}

bool alert_manager_enable_rule(struct alert_manager *am, int rule_id)
{
	struct alert_rule_entry *entry = NULL;
	size_t i;
	
	if (!am)
		return false;
	
	pthread_mutex_lock(&am->rules_mutex);
	
	for (i = 0; i < am->max_rules; i++) {
		if (am->rules[i].in_use && am->rules[i].id == rule_id) {
			entry = &am->rules[i];
			break;
		}
	}
	
	if (entry)
		entry->rule.enabled = true;
	
	pthread_mutex_unlock(&am->rules_mutex);
	
	return entry != NULL;
}

bool alert_manager_disable_rule(struct alert_manager *am, int rule_id)
{
	struct alert_rule_entry *entry = NULL;
	size_t i;
	
	if (!am)
		return false;
	
	pthread_mutex_lock(&am->rules_mutex);
	
	for (i = 0; i < am->max_rules; i++) {
		if (am->rules[i].in_use && am->rules[i].id == rule_id) {
			entry = &am->rules[i];
			break;
		}
	}
	
	if (entry)
		entry->rule.enabled = false;
	
	pthread_mutex_unlock(&am->rules_mutex);
	
	return entry != NULL;
}

void alert_manager_evaluate(struct alert_manager *am, struct nlmon_event *event)
{
	size_t i;
	time_t now;
	
	if (!am || !event)
		return;
	
	now = get_current_time();
	
	pthread_mutex_lock(&am->rules_mutex);
	
	for (i = 0; i < am->max_rules; i++) {
		struct alert_rule_entry *entry = &am->rules[i];
		
		if (!entry->in_use || !entry->rule.enabled)
			continue;
		
		/* Check suppression */
		if (entry->suppressed && now < entry->suppress_until)
			continue;
		else if (entry->suppressed && now >= entry->suppress_until)
			entry->suppressed = false;
		
		/* Evaluate condition */
		if (!filter_eval(entry->filter, event, NULL))
			continue;
		
		/* Check rate limiting */
		if (entry->rule.rate_limit_count > 0 && entry->rule.rate_limit_window_s > 0) {
			time_t window_start = now - entry->rule.rate_limit_window_s;
			size_t triggers_in_window = 0;
			
			/* Count triggers in window */
			for (size_t j = 0; j < entry->trigger_count; j++) {
				if (entry->trigger_times[j] >= window_start)
					triggers_in_window++;
			}
			
			if (triggers_in_window >= entry->rule.rate_limit_count) {
				/* Rate limit exceeded */
				entry->rate_limited_count++;
				pthread_mutex_lock(&am->stats_mutex);
				am->stats.total_rate_limited++;
				pthread_mutex_unlock(&am->stats_mutex);
				continue;
			}
			
			/* Record trigger time */
			entry->trigger_times[entry->trigger_index] = now;
			entry->trigger_index = (entry->trigger_index + 1) % entry->rule.rate_limit_count;
			if (entry->trigger_count < entry->rule.rate_limit_count)
				entry->trigger_count++;
		}
		
		entry->triggered_count++;
		
		/* Execute action */
		bool action_success = execute_alert_action(&entry->rule.action, event,
		                                           entry->rule.name,
		                                           entry->rule.severity);
		
		if (action_success)
			entry->executed_count++;
		else
			entry->failed_count++;
		
		/* Update global statistics */
		pthread_mutex_lock(&am->stats_mutex);
		am->stats.total_triggered++;
		if (action_success)
			am->stats.total_executed++;
		else
			am->stats.total_failed++;
		pthread_mutex_unlock(&am->stats_mutex);
		
		/* Create alert instance */
		pthread_mutex_lock(&am->history_mutex);
		
		struct alert_instance *instance = &am->history[am->history_index];
		instance->id = am->next_alert_id++;
		strncpy(instance->rule_name, entry->rule.name, ALERT_MAX_NAME - 1);
		instance->severity = entry->rule.severity;
		instance->state = ALERT_STATE_ACTIVE;
		instance->triggered_at = now;
		instance->acknowledged_at = 0;
		instance->resolved_at = 0;
		instance->event_sequence = event->sequence;
		snprintf(instance->message, ALERT_MAX_MESSAGE,
		         "Alert triggered by event type=%u interface=%s",
		         event->message_type, event->interface);
		instance->acknowledged_by[0] = '\0';
		
		am->history_index = (am->history_index + 1) % am->max_history;
		if (am->history_count < am->max_history)
			am->history_count++;
		
		am->stats.active_count++;
		
		pthread_mutex_unlock(&am->history_mutex);
		
		/* Apply suppression if configured */
		if (entry->rule.suppress_duration_s > 0) {
			entry->suppressed = true;
			entry->suppress_until = now + entry->rule.suppress_duration_s;
			entry->suppressed_count++;
			
			pthread_mutex_lock(&am->stats_mutex);
			am->stats.total_suppressed++;
			pthread_mutex_unlock(&am->stats_mutex);
		}
	}
	
	pthread_mutex_unlock(&am->rules_mutex);
}

bool alert_manager_acknowledge(struct alert_manager *am, uint64_t alert_id,
                               const char *acknowledged_by)
{
	size_t i;
	bool found = false;
	
	if (!am || !acknowledged_by)
		return false;
	
	pthread_mutex_lock(&am->history_mutex);
	
	for (i = 0; i < am->history_count; i++) {
		struct alert_instance *instance = &am->history[i];
		
		if (instance->id == alert_id && instance->state == ALERT_STATE_ACTIVE) {
			instance->state = ALERT_STATE_ACKNOWLEDGED;
			instance->acknowledged_at = get_current_time();
			strncpy(instance->acknowledged_by, acknowledged_by, 63);
			instance->acknowledged_by[63] = '\0';
			
			pthread_mutex_lock(&am->stats_mutex);
			if (am->stats.active_count > 0)
				am->stats.active_count--;
			am->stats.acknowledged_count++;
			pthread_mutex_unlock(&am->stats_mutex);
			
			found = true;
			break;
		}
	}
	
	pthread_mutex_unlock(&am->history_mutex);
	
	return found;
}

bool alert_manager_resolve(struct alert_manager *am, uint64_t alert_id)
{
	size_t i;
	bool found = false;
	
	if (!am)
		return false;
	
	pthread_mutex_lock(&am->history_mutex);
	
	for (i = 0; i < am->history_count; i++) {
		struct alert_instance *instance = &am->history[i];
		
		if (instance->id == alert_id &&
		    (instance->state == ALERT_STATE_ACTIVE ||
		     instance->state == ALERT_STATE_ACKNOWLEDGED)) {
			enum alert_state old_state = instance->state;
			instance->state = ALERT_STATE_INACTIVE;
			instance->resolved_at = get_current_time();
			
			pthread_mutex_lock(&am->stats_mutex);
			if (old_state == ALERT_STATE_ACTIVE && am->stats.active_count > 0)
				am->stats.active_count--;
			else if (old_state == ALERT_STATE_ACKNOWLEDGED && am->stats.acknowledged_count > 0)
				am->stats.acknowledged_count--;
			pthread_mutex_unlock(&am->stats_mutex);
			
			found = true;
			break;
		}
	}
	
	pthread_mutex_unlock(&am->history_mutex);
	
	return found;
}

size_t alert_manager_get_active(struct alert_manager *am,
                                struct alert_instance *alerts,
                                size_t max_alerts)
{
	size_t count = 0;
	size_t i;
	
	if (!am || !alerts || max_alerts == 0)
		return 0;
	
	pthread_mutex_lock(&am->history_mutex);
	
	for (i = 0; i < am->history_count && count < max_alerts; i++) {
		if (am->history[i].state == ALERT_STATE_ACTIVE) {
			alerts[count++] = am->history[i];
		}
	}
	
	pthread_mutex_unlock(&am->history_mutex);
	
	return count;
}

size_t alert_manager_get_history(struct alert_manager *am,
                                 struct alert_instance *alerts,
                                 size_t max_alerts,
                                 time_t since)
{
	size_t count = 0;
	size_t i;
	
	if (!am || !alerts || max_alerts == 0)
		return 0;
	
	pthread_mutex_lock(&am->history_mutex);
	
	for (i = 0; i < am->history_count && count < max_alerts; i++) {
		if (since == 0 || am->history[i].triggered_at >= since) {
			alerts[count++] = am->history[i];
		}
	}
	
	pthread_mutex_unlock(&am->history_mutex);
	
	return count;
}

bool alert_manager_get_stats(struct alert_manager *am, struct alert_stats *stats)
{
	if (!am || !stats)
		return false;
	
	pthread_mutex_lock(&am->stats_mutex);
	*stats = am->stats;
	pthread_mutex_unlock(&am->stats_mutex);
	
	return true;
}

void alert_manager_reset_stats(struct alert_manager *am)
{
	if (!am)
		return;
	
	pthread_mutex_lock(&am->stats_mutex);
	memset(&am->stats, 0, sizeof(am->stats));
	pthread_mutex_unlock(&am->stats_mutex);
}

size_t alert_manager_list_rules(struct alert_manager *am, int *rule_ids,
                                size_t max_rules)
{
	size_t count = 0;
	size_t i;
	
	if (!am || !rule_ids || max_rules == 0)
		return 0;
	
	pthread_mutex_lock(&am->rules_mutex);
	
	for (i = 0; i < am->max_rules && count < max_rules; i++) {
		if (am->rules[i].in_use) {
			rule_ids[count++] = am->rules[i].id;
		}
	}
	
	pthread_mutex_unlock(&am->rules_mutex);
	
	return count;
}

bool alert_manager_get_rule(struct alert_manager *am, int rule_id,
                            struct alert_rule *rule)
{
	struct alert_rule_entry *entry = NULL;
	size_t i;
	
	if (!am || !rule)
		return false;
	
	pthread_mutex_lock(&am->rules_mutex);
	
	for (i = 0; i < am->max_rules; i++) {
		if (am->rules[i].in_use && am->rules[i].id == rule_id) {
			entry = &am->rules[i];
			break;
		}
	}
	
	if (entry)
		*rule = entry->rule;
	
	pthread_mutex_unlock(&am->rules_mutex);
	
	return entry != NULL;
}

bool alert_manager_suppress(struct alert_manager *am, int rule_id,
                            uint32_t duration_s)
{
	struct alert_rule_entry *entry = NULL;
	size_t i;
	
	if (!am || duration_s == 0)
		return false;
	
	pthread_mutex_lock(&am->rules_mutex);
	
	for (i = 0; i < am->max_rules; i++) {
		if (am->rules[i].in_use && am->rules[i].id == rule_id) {
			entry = &am->rules[i];
			break;
		}
	}
	
	if (entry) {
		entry->suppressed = true;
		entry->suppress_until = get_current_time() + duration_s;
	}
	
	pthread_mutex_unlock(&am->rules_mutex);
	
	return entry != NULL;
}

bool alert_manager_unsuppress(struct alert_manager *am, int rule_id)
{
	struct alert_rule_entry *entry = NULL;
	size_t i;
	
	if (!am)
		return false;
	
	pthread_mutex_lock(&am->rules_mutex);
	
	for (i = 0; i < am->max_rules; i++) {
		if (am->rules[i].in_use && am->rules[i].id == rule_id) {
			entry = &am->rules[i];
			break;
		}
	}
	
	if (entry) {
		entry->suppressed = false;
		entry->suppress_until = 0;
	}
	
	pthread_mutex_unlock(&am->rules_mutex);
	
	return entry != NULL;
}

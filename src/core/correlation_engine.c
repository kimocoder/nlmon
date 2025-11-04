/* correlation_engine.c - Event correlation rule engine
 *
 * Implements correlation rule parsing, evaluation, and correlation ID
 * generation for grouping related network events.
 */

#include "correlation_engine.h"
#include "event_processor.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>

/* Correlation rule structure */
struct correlation_rule {
	int id;
	struct correlation_rule_def def;
	struct time_window *window;
	bool active;
};

/* Correlation engine structure */
struct correlation_engine {
	struct correlation_rule *rules;
	size_t rule_count;
	size_t max_rules;
	struct time_window *global_window;
	time_t default_window_sec;
	unsigned long correlation_counter;
	pthread_mutex_t lock;
};

struct correlation_engine *correlation_engine_create(struct correlation_config *config)
{
	struct correlation_engine *engine;

	if (!config)
		return NULL;

	engine = calloc(1, sizeof(*engine));
	if (!engine)
		return NULL;

	engine->rules = calloc(config->max_rules, sizeof(struct correlation_rule));
	if (!engine->rules) {
		free(engine);
		return NULL;
	}

	engine->max_rules = config->max_rules;
	engine->default_window_sec = config->default_window_sec;
	engine->rule_count = 0;
	engine->correlation_counter = 0;

	/* Create global time window */
	engine->global_window = time_window_create(config->default_window_sec,
	                                           config->max_window_size);
	if (!engine->global_window) {
		free(engine->rules);
		free(engine);
		return NULL;
	}

	if (pthread_mutex_init(&engine->lock, NULL) != 0) {
		time_window_destroy(engine->global_window);
		free(engine->rules);
		free(engine);
		return NULL;
	}

	return engine;
}

void correlation_engine_destroy(struct correlation_engine *engine)
{
	size_t i;

	if (!engine)
		return;

	/* Destroy rule-specific windows */
	for (i = 0; i < engine->rule_count; i++) {
		if (engine->rules[i].window)
			time_window_destroy(engine->rules[i].window);
	}

	time_window_destroy(engine->global_window);
	pthread_mutex_destroy(&engine->lock);
	free(engine->rules);
	free(engine);
}

int correlation_engine_add_rule(struct correlation_engine *engine,
                                struct correlation_rule_def *rule_def)
{
	struct correlation_rule *rule;
	int rule_id;

	if (!engine || !rule_def)
		return -1;

	pthread_mutex_lock(&engine->lock);

	if (engine->rule_count >= engine->max_rules) {
		pthread_mutex_unlock(&engine->lock);
		return -1;
	}

	rule = &engine->rules[engine->rule_count];
	rule_id = engine->rule_count;

	/* Copy rule definition */
	memcpy(&rule->def, rule_def, sizeof(struct correlation_rule_def));
	rule->id = rule_id;
	rule->active = true;

	/* Create time window for this rule */
	time_t window_sec = rule_def->time_window_sec > 0 ?
	                    rule_def->time_window_sec : engine->default_window_sec;
	rule->window = time_window_create(window_sec, 1000);
	if (!rule->window) {
		pthread_mutex_unlock(&engine->lock);
		return -1;
	}

	engine->rule_count++;

	pthread_mutex_unlock(&engine->lock);
	return rule_id;
}

void correlation_engine_remove_rule(struct correlation_engine *engine, int rule_id)
{
	if (!engine || rule_id < 0 || (size_t)rule_id >= engine->rule_count)
		return;

	pthread_mutex_lock(&engine->lock);

	engine->rules[rule_id].active = false;
	if (engine->rules[rule_id].window) {
		time_window_destroy(engine->rules[rule_id].window);
		engine->rules[rule_id].window = NULL;
	}

	pthread_mutex_unlock(&engine->lock);
}

/* Check if event matches a condition */
static bool match_condition(struct nlmon_event *event,
                           struct correlation_rule_def *rule,
                           size_t cond_idx)
{
	if (cond_idx >= rule->condition_count)
		return false;

	switch (rule->conditions[cond_idx].type) {
	case CORR_COND_EVENT_TYPE:
		return event->event_type == rule->conditions[cond_idx].value.event_type;

	case CORR_COND_INTERFACE:
		return strcmp(event->interface, rule->conditions[cond_idx].value.interface) == 0;

	case CORR_COND_MESSAGE_TYPE:
		return event->message_type == rule->conditions[cond_idx].value.message_type;

	case CORR_COND_SAME_INTERFACE:
	case CORR_COND_COUNT:
	case CORR_COND_RATE:
		/* These conditions checked separately */
		return true;

	default:
		return false;
	}
}

/* Evaluate rule against events in window */
__attribute__((unused))
static bool evaluate_rule(struct correlation_rule *rule,
                         struct nlmon_event *new_event,
                         struct correlation_result *result)
{
	struct nlmon_event *window_events[1000];
	size_t window_count;
	size_t matched = 0;
	size_t i, j;

	/* Query events from rule's time window */
	window_count = time_window_query(rule->window, 0, NULL,
	                                 window_events, 1000);

	/* Check if new event matches any condition */
	bool new_event_matches = false;
	for (i = 0; i < rule->def.condition_count; i++) {
		if (match_condition(new_event, &rule->def, i)) {
			new_event_matches = true;
			break;
		}
	}

	if (!new_event_matches)
		return false;

	/* Count matching events in window */
	for (i = 0; i < window_count; i++) {
		for (j = 0; j < rule->def.condition_count; j++) {
			if (match_condition(window_events[i], &rule->def, j)) {
				matched++;
				break;
			}
		}
	}

	/* Add new event to matched count */
	matched++;

	/* Check if we have enough events for correlation */
	if (matched < rule->def.event_count)
		return false;

	/* Build correlation result */
	if (result) {
		strncpy(result->rule_name, rule->def.name, sizeof(result->rule_name) - 1);
		result->event_count = matched;
		result->first_timestamp = window_count > 0 ? window_events[0]->timestamp : new_event->timestamp;
		result->last_timestamp = new_event->timestamp;

		/* Note: events array not allocated to avoid memory management complexity
		 * Caller should query the time window directly if needed */
		result->events = NULL;
	}

	return true;
}

size_t correlation_engine_process(struct correlation_engine *engine,
                                  struct nlmon_event *event,
                                  struct correlation_result *results,
                                  size_t max_results)
{
	size_t found = 0;
	size_t i;
	time_t current_time;

	if (!engine || !event || !results || max_results == 0)
		return 0;

	current_time = event->timestamp;

	pthread_mutex_lock(&engine->lock);

	/* Add event to global window */
	time_window_add(engine->global_window, event);
	time_window_expire(engine->global_window, current_time);

	/* Evaluate each active rule */
	for (i = 0; i < engine->rule_count && found < max_results; i++) {
		struct correlation_rule *rule = &engine->rules[i];

		if (!rule->active || !rule->window)
			continue;

		/* Add event to rule's window */
		time_window_add(rule->window, event);
		time_window_expire(rule->window, current_time);

		/* Simple correlation: check if we have enough events in window */
		size_t window_count = time_window_count(rule->window);
		if (window_count >= rule->def.event_count) {
			/* Generate correlation result */
			strncpy(results[found].rule_name, rule->def.name,
			        sizeof(results[found].rule_name) - 1);
			results[found].event_count = window_count;
			results[found].first_timestamp = current_time - rule->def.time_window_sec;
			results[found].last_timestamp = current_time;
			results[found].events = NULL;

			/* Generate correlation ID */
			correlation_engine_generate_id(engine, rule->def.name,
			                              results[found].correlation_id,
			                              sizeof(results[found].correlation_id));
			found++;
		}
	}

	pthread_mutex_unlock(&engine->lock);
	return found;
}

bool correlation_engine_generate_id(struct correlation_engine *engine,
                                    const char *rule_name,
                                    char *id_buf, size_t buf_size)
{
	unsigned long counter;

	if (!engine || !rule_name || !id_buf || buf_size == 0)
		return false;

	pthread_mutex_lock(&engine->lock);
	counter = ++engine->correlation_counter;
	pthread_mutex_unlock(&engine->lock);

	snprintf(id_buf, buf_size, "%s-%lu", rule_name, counter);
	return true;
}

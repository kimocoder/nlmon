/* filter_manager.c - Filter management system implementation
 *
 * Manages named filters with persistence, validation, and statistics.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include "filter_manager.h"
#include "filter_parser.h"
#include "filter_compiler.h"
#include "filter_eval.h"
#include "event_processor.h"

/* Helper functions */
static struct filter_entry *find_filter(struct filter_manager *mgr, const char *name)
{
	struct filter_entry *entry;
	
	for (entry = mgr->filters; entry; entry = entry->next) {
		if (strcmp(entry->name, name) == 0)
			return entry;
	}
	
	return NULL;
}

static void filter_entry_free(struct filter_entry *entry)
{
	if (!entry)
		return;
	
	free(entry->name);
	free(entry->description);
	free(entry->expression);
	filter_expr_free(entry->parsed);
	filter_bytecode_free(entry->compiled);
	free(entry);
}

static struct filter_entry *filter_entry_create(const char *name,
                                                const char *expression,
                                                const char *description)
{
	struct filter_entry *entry;
	
	entry = calloc(1, sizeof(*entry));
	if (!entry)
		return NULL;
	
	entry->name = strdup(name);
	if (!entry->name) {
		free(entry);
		return NULL;
	}
	
	entry->expression = strdup(expression);
	if (!entry->expression) {
		free(entry->name);
		free(entry);
		return NULL;
	}
	
	if (description) {
		entry->description = strdup(description);
		if (!entry->description) {
			free(entry->expression);
			free(entry->name);
			free(entry);
			return NULL;
		}
	}
	
	/* Parse expression */
	entry->parsed = filter_parse(expression);
	if (!entry->parsed || !entry->parsed->valid) {
		filter_entry_free(entry);
		return NULL;
	}
	
	/* Compile to bytecode */
	entry->compiled = filter_compile(entry->parsed);
	if (!entry->compiled) {
		filter_entry_free(entry);
		return NULL;
	}
	
	/* Optimize bytecode */
	filter_bytecode_optimize(entry->compiled);
	
	entry->created = time(NULL);
	entry->modified = entry->created;
	entry->enabled = true;
	
	return entry;
}

/* Public API */
struct filter_manager *filter_manager_create(const char *storage_path)
{
	struct filter_manager *mgr;
	
	mgr = calloc(1, sizeof(*mgr));
	if (!mgr)
		return NULL;
	
	if (storage_path) {
		mgr->storage_path = strdup(storage_path);
		if (!mgr->storage_path) {
			free(mgr);
			return NULL;
		}
	}
	
	mgr->eval_ctx = filter_eval_context_create();
	if (!mgr->eval_ctx) {
		free(mgr->storage_path);
		free(mgr);
		return NULL;
	}
	
	mgr->auto_save = true;
	
	return mgr;
}

void filter_manager_destroy(struct filter_manager *mgr)
{
	struct filter_entry *entry, *next;
	
	if (!mgr)
		return;
	
	/* Free all filters */
	entry = mgr->filters;
	while (entry) {
		next = entry->next;
		filter_entry_free(entry);
		entry = next;
	}
	
	filter_eval_context_destroy(mgr->eval_ctx);
	free(mgr->storage_path);
	free(mgr);
}

bool filter_manager_add(struct filter_manager *mgr,
                        const char *name,
                        const char *expression,
                        const char *description)
{
	struct filter_entry *entry;
	
	if (!mgr || !name || !expression)
		return false;
	
	/* Check if filter already exists */
	if (find_filter(mgr, name))
		return false;
	
	/* Create filter entry */
	entry = filter_entry_create(name, expression, description);
	if (!entry)
		return false;
	
	/* Add to list */
	entry->next = mgr->filters;
	mgr->filters = entry;
	mgr->filter_count++;
	
	/* Auto-save if enabled */
	if (mgr->auto_save && mgr->storage_path)
		filter_manager_save(mgr);
	
	return true;
}

bool filter_manager_remove(struct filter_manager *mgr, const char *name)
{
	struct filter_entry *entry, *prev;
	
	if (!mgr || !name)
		return false;
	
	prev = NULL;
	for (entry = mgr->filters; entry; entry = entry->next) {
		if (strcmp(entry->name, name) == 0) {
			if (prev)
				prev->next = entry->next;
			else
				mgr->filters = entry->next;
			
			filter_entry_free(entry);
			mgr->filter_count--;
			
			/* Auto-save if enabled */
			if (mgr->auto_save && mgr->storage_path)
				filter_manager_save(mgr);
			
			return true;
		}
		prev = entry;
	}
	
	return false;
}

struct filter_entry *filter_manager_get(struct filter_manager *mgr,
                                        const char *name)
{
	if (!mgr || !name)
		return NULL;
	
	return find_filter(mgr, name);
}

bool filter_manager_update(struct filter_manager *mgr,
                           const char *name,
                           const char *expression)
{
	struct filter_entry *entry;
	struct filter_expr *parsed;
	struct filter_bytecode *compiled;
	
	if (!mgr || !name || !expression)
		return false;
	
	entry = find_filter(mgr, name);
	if (!entry)
		return false;
	
	/* Parse and compile new expression */
	parsed = filter_parse(expression);
	if (!parsed || !parsed->valid) {
		filter_expr_free(parsed);
		return false;
	}
	
	compiled = filter_compile(parsed);
	if (!compiled) {
		filter_expr_free(parsed);
		return false;
	}
	
	filter_bytecode_optimize(compiled);
	
	/* Update entry */
	free(entry->expression);
	filter_expr_free(entry->parsed);
	filter_bytecode_free(entry->compiled);
	
	entry->expression = strdup(expression);
	entry->parsed = parsed;
	entry->compiled = compiled;
	entry->modified = time(NULL);
	
	/* Reset statistics */
	entry->eval_count = 0;
	entry->match_count = 0;
	entry->total_time_ns = 0;
	
	/* Auto-save if enabled */
	if (mgr->auto_save && mgr->storage_path)
		filter_manager_save(mgr);
	
	return true;
}

bool filter_manager_enable(struct filter_manager *mgr, const char *name)
{
	struct filter_entry *entry;
	
	if (!mgr || !name)
		return false;
	
	entry = find_filter(mgr, name);
	if (!entry)
		return false;
	
	entry->enabled = true;
	
	if (mgr->auto_save && mgr->storage_path)
		filter_manager_save(mgr);
	
	return true;
}

bool filter_manager_disable(struct filter_manager *mgr, const char *name)
{
	struct filter_entry *entry;
	
	if (!mgr || !name)
		return false;
	
	entry = find_filter(mgr, name);
	if (!entry)
		return false;
	
	entry->enabled = false;
	
	if (mgr->auto_save && mgr->storage_path)
		filter_manager_save(mgr);
	
	return true;
}

bool filter_manager_eval(struct filter_manager *mgr,
                         const char *name,
                         struct nlmon_event *event)
{
	struct filter_entry *entry;
	uint64_t elapsed_ns;
	bool result;
	
	if (!mgr || !name || !event)
		return false;
	
	entry = find_filter(mgr, name);
	if (!entry || !entry->enabled)
		return false;
	
	result = filter_eval_with_profiling(entry->compiled, event,
	                                    mgr->eval_ctx, &elapsed_ns);
	
	/* Update statistics */
	entry->eval_count++;
	entry->total_time_ns += elapsed_ns;
	if (result)
		entry->match_count++;
	
	return result;
}

size_t filter_manager_eval_all(struct filter_manager *mgr,
                               struct nlmon_event *event,
                               const char **matches,
                               size_t max_matches)
{
	struct filter_entry *entry;
	size_t match_count = 0;
	
	if (!mgr || !event)
		return 0;
	
	for (entry = mgr->filters; entry; entry = entry->next) {
		if (!entry->enabled)
			continue;
		
		if (filter_manager_eval(mgr, entry->name, event)) {
			if (matches && match_count < max_matches)
				matches[match_count] = entry->name;
			match_count++;
		}
	}
	
	return match_count;
}

size_t filter_manager_list(struct filter_manager *mgr,
                           const char **names,
                           size_t max_count)
{
	struct filter_entry *entry;
	size_t count = 0;
	
	if (!mgr)
		return 0;
	
	for (entry = mgr->filters; entry; entry = entry->next) {
		if (names && count < max_count)
			names[count] = entry->name;
		count++;
	}
	
	return count;
}

bool filter_manager_save(struct filter_manager *mgr)
{
	FILE *fp;
	struct filter_entry *entry;
	
	if (!mgr || !mgr->storage_path)
		return false;
	
	fp = fopen(mgr->storage_path, "w");
	if (!fp)
		return false;
	
	/* Write header */
	fprintf(fp, "# nlmon filter configuration\n");
	fprintf(fp, "# Format: name|expression|description|enabled\n\n");
	
	/* Write each filter */
	for (entry = mgr->filters; entry; entry = entry->next) {
		fprintf(fp, "%s|%s|%s|%d\n",
		        entry->name,
		        entry->expression,
		        entry->description ? entry->description : "",
		        entry->enabled ? 1 : 0);
	}
	
	fclose(fp);
	return true;
}

int filter_manager_load(struct filter_manager *mgr)
{
	FILE *fp;
	char line[1024];
	int count = 0;
	
	if (!mgr || !mgr->storage_path)
		return -1;
	
	fp = fopen(mgr->storage_path, "r");
	if (!fp)
		return -1;
	
	while (fgets(line, sizeof(line), fp)) {
		char *name, *expression, *description, *enabled_str;
		bool enabled;
		
		/* Skip comments and empty lines */
		if (line[0] == '#' || line[0] == '\n')
			continue;
		
		/* Remove trailing newline */
		line[strcspn(line, "\n")] = '\0';
		
		/* Parse line */
		name = strtok(line, "|");
		if (!name)
			continue;
		
		expression = strtok(NULL, "|");
		if (!expression)
			continue;
		
		description = strtok(NULL, "|");
		enabled_str = strtok(NULL, "|");
		enabled = enabled_str && atoi(enabled_str) != 0;
		
		/* Add filter */
		if (filter_manager_add(mgr, name, expression,
		                      description && description[0] ? description : NULL)) {
			if (!enabled)
				filter_manager_disable(mgr, name);
			count++;
		}
	}
	
	fclose(fp);
	return count;
}

bool filter_manager_validate(struct filter_manager *mgr,
                             const char *expression,
                             char *error_msg,
                             size_t error_size)
{
	struct filter_parse_error error;
	bool valid;
	
	if (!mgr || !expression)
		return false;
	
	valid = filter_validate(expression, &error);
	
	if (!valid && error_msg && error_size > 0) {
		snprintf(error_msg, error_size, "%s (line %zu, column %zu)",
		         error.message, error.line, error.column);
	}
	
	return valid;
}

bool filter_manager_stats(struct filter_manager *mgr,
                         const char *name,
                         uint64_t *eval_count,
                         uint64_t *match_count,
                         uint64_t *avg_time_ns)
{
	struct filter_entry *entry;
	
	if (!mgr || !name)
		return false;
	
	entry = find_filter(mgr, name);
	if (!entry)
		return false;
	
	if (eval_count)
		*eval_count = entry->eval_count;
	if (match_count)
		*match_count = entry->match_count;
	if (avg_time_ns)
		*avg_time_ns = entry->eval_count > 0 ?
		               entry->total_time_ns / entry->eval_count : 0;
	
	return true;
}

void filter_manager_reset_stats(struct filter_manager *mgr, const char *name)
{
	struct filter_entry *entry;
	
	if (!mgr)
		return;
	
	if (name) {
		/* Reset specific filter */
		entry = find_filter(mgr, name);
		if (entry) {
			entry->eval_count = 0;
			entry->match_count = 0;
			entry->total_time_ns = 0;
		}
	} else {
		/* Reset all filters */
		for (entry = mgr->filters; entry; entry = entry->next) {
			entry->eval_count = 0;
			entry->match_count = 0;
			entry->total_time_ns = 0;
		}
	}
}

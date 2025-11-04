/* event_hooks.c - Event hook system implementation
 *
 * Implements script execution with fork/exec, event data passing via
 * environment variables, timeout mechanism, and output capture.
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
#include <time.h>
#include "event_hooks.h"
#include "filter_parser.h"
#include "filter_compiler.h"
#include "filter_eval.h"

/* Hook entry */
struct hook_entry {
	int id;
	struct hook_config config;
	struct filter_bytecode *filter;  /* Compiled condition */
	struct hook_stats stats;
	pthread_mutex_t stats_mutex;
	bool in_use;
};

/* Async execution context */
struct async_exec_ctx {
	struct hook_manager *hm;
	struct hook_entry *hook;
	struct nlmon_event *event;
	char **envp;
};

/* Hook manager structure */
struct hook_manager {
	struct hook_entry *hooks;
	size_t max_hooks;
	size_t max_concurrent;
	int next_hook_id;
	pthread_mutex_t hooks_mutex;
	
	/* Async execution tracking */
	size_t active_executions;
	pthread_mutex_t exec_mutex;
	pthread_cond_t exec_cond;
};

/* Helper: Get current time in milliseconds */
static uint64_t get_time_ms(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

/* Helper: Build environment variables from event */
static char **build_event_env(struct nlmon_event *event)
{
	char **envp;
	int count = 0;
	char buf[256];
	
	/* Allocate environment array (max 20 variables + NULL) */
	envp = calloc(21, sizeof(char *));
	if (!envp)
		return NULL;
	
	/* Add event fields as environment variables */
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
	
	/* Add PATH for script execution */
	envp[count++] = strdup("PATH=/usr/local/bin:/usr/bin:/bin");
	
	/* NULL terminate */
	envp[count] = NULL;
	
	return envp;
}

/* Helper: Free environment variables */
static void free_event_env(char **envp)
{
	int i;
	
	if (!envp)
		return;
	
	for (i = 0; envp[i]; i++)
		free(envp[i]);
	free(envp);
}

/* Helper: Execute script with timeout */
static enum hook_result execute_script(const char *script, char **envp,
                                       uint32_t timeout_ms,
                                       bool capture_output,
                                       char *output, size_t output_size)
{
	pid_t pid;
	int status;
	int pipefd[2] = {-1, -1};
	uint64_t start_time, elapsed;
	struct timespec ts;
	
	/* Create pipe for output capture if requested */
	if (capture_output && output && output_size > 0) {
		if (pipe(pipefd) < 0)
			return HOOK_RESULT_ERROR;
	}
	
	start_time = get_time_ms();
	
	/* Fork child process */
	pid = fork();
	if (pid < 0) {
		if (pipefd[0] >= 0) {
			close(pipefd[0]);
			close(pipefd[1]);
		}
		return HOOK_RESULT_ERROR;
	}
	
	if (pid == 0) {
		/* Child process */
		
		/* Redirect stdout/stderr to pipe if capturing */
		if (pipefd[1] >= 0) {
			dup2(pipefd[1], STDOUT_FILENO);
			dup2(pipefd[1], STDERR_FILENO);
			close(pipefd[0]);
			close(pipefd[1]);
		} else {
			/* Redirect to /dev/null if not capturing */
			int devnull = open("/dev/null", O_WRONLY);
			if (devnull >= 0) {
				dup2(devnull, STDOUT_FILENO);
				dup2(devnull, STDERR_FILENO);
				close(devnull);
			}
		}
		
		/* Execute script via shell */
		execle("/bin/sh", "sh", "-c", script, NULL, envp);
		
		/* If exec fails */
		_exit(127);
	}
	
	/* Parent process */
	if (pipefd[1] >= 0)
		close(pipefd[1]);
	
	/* Read output if capturing */
	if (pipefd[0] >= 0 && output && output_size > 0) {
		ssize_t nread;
		size_t total = 0;
		
		/* Set non-blocking */
		fcntl(pipefd[0], F_SETFL, O_NONBLOCK);
		
		while (total < output_size - 1) {
			nread = read(pipefd[0], output + total, output_size - total - 1);
			if (nread < 0) {
				if (errno == EAGAIN || errno == EWOULDBLOCK)
					break;
				break;
			}
			if (nread == 0)
				break;
			total += nread;
		}
		output[total] = '\0';
		close(pipefd[0]);
	}
	
	/* Wait for child with timeout */
	ts.tv_sec = timeout_ms / 1000;
	ts.tv_nsec = (timeout_ms % 1000) * 1000000;
	(void)ts; /* Reserved for future timeout implementation */
	
	while (1) {
		pid_t result = waitpid(pid, &status, WNOHANG);
		
		if (result == pid) {
			/* Child exited */
			if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
				return HOOK_RESULT_SUCCESS;
			return HOOK_RESULT_ERROR;
		}
		
		if (result < 0) {
			if (errno == EINTR)
				continue;
			return HOOK_RESULT_ERROR;
		}
		
		/* Check timeout */
		elapsed = get_time_ms() - start_time;
		if (elapsed >= timeout_ms) {
			/* Timeout - kill child */
			kill(pid, SIGKILL);
			waitpid(pid, &status, 0);
			return HOOK_RESULT_TIMEOUT;
		}
		
		/* Sleep briefly */
		usleep(10000);  /* 10ms */
	}
}

/* Async execution thread */
static void *async_exec_thread(void *arg)
{
	struct async_exec_ctx *ctx = arg;
	struct hook_manager *hm = ctx->hm;
	struct hook_entry *hook = ctx->hook;
	struct nlmon_event *event = ctx->event;
	char **envp = ctx->envp;
	enum hook_result result;
	uint64_t start_time, duration;
	
	start_time = get_time_ms();
	
	/* Execute script */
	result = execute_script(hook->config.script, envp,
	                       hook->config.timeout_ms,
	                       false, NULL, 0);
	
	duration = get_time_ms() - start_time;
	
	/* Update statistics */
	pthread_mutex_lock(&hook->stats_mutex);
	hook->stats.executions++;
	hook->stats.total_duration_ms += duration;
	
	if (hook->stats.min_duration_ms == 0 || duration < hook->stats.min_duration_ms)
		hook->stats.min_duration_ms = duration;
	if (duration > hook->stats.max_duration_ms)
		hook->stats.max_duration_ms = duration;
	
	switch (result) {
	case HOOK_RESULT_SUCCESS:
		hook->stats.successes++;
		break;
	case HOOK_RESULT_TIMEOUT:
		hook->stats.timeouts++;
		break;
	case HOOK_RESULT_ERROR:
		hook->stats.failures++;
		break;
	default:
		break;
	}
	pthread_mutex_unlock(&hook->stats_mutex);
	
	/* Cleanup */
	free_event_env(envp);
	free(event);
	free(ctx);
	
	/* Decrement active executions */
	pthread_mutex_lock(&hm->exec_mutex);
	hm->active_executions--;
	pthread_cond_signal(&hm->exec_cond);
	pthread_mutex_unlock(&hm->exec_mutex);
	
	return NULL;
}

struct hook_manager *hook_manager_create(size_t max_hooks, size_t max_concurrent)
{
	struct hook_manager *hm;
	size_t i;
	
	if (max_hooks == 0)
		max_hooks = 32;
	if (max_concurrent == 0)
		max_concurrent = 10;
	
	hm = calloc(1, sizeof(*hm));
	if (!hm)
		return NULL;
	
	hm->hooks = calloc(max_hooks, sizeof(struct hook_entry));
	if (!hm->hooks) {
		free(hm);
		return NULL;
	}
	
	hm->max_hooks = max_hooks;
	hm->max_concurrent = max_concurrent;
	hm->next_hook_id = 1;
	
	/* Initialize mutexes */
	if (pthread_mutex_init(&hm->hooks_mutex, NULL) != 0) {
		free(hm->hooks);
		free(hm);
		return NULL;
	}
	
	if (pthread_mutex_init(&hm->exec_mutex, NULL) != 0) {
		pthread_mutex_destroy(&hm->hooks_mutex);
		free(hm->hooks);
		free(hm);
		return NULL;
	}
	
	if (pthread_cond_init(&hm->exec_cond, NULL) != 0) {
		pthread_mutex_destroy(&hm->exec_mutex);
		pthread_mutex_destroy(&hm->hooks_mutex);
		free(hm->hooks);
		free(hm);
		return NULL;
	}
	
	/* Initialize hook entry mutexes */
	for (i = 0; i < max_hooks; i++) {
		if (pthread_mutex_init(&hm->hooks[i].stats_mutex, NULL) != 0) {
			/* Cleanup on failure */
			for (size_t j = 0; j < i; j++)
				pthread_mutex_destroy(&hm->hooks[j].stats_mutex);
			pthread_cond_destroy(&hm->exec_cond);
			pthread_mutex_destroy(&hm->exec_mutex);
			pthread_mutex_destroy(&hm->hooks_mutex);
			free(hm->hooks);
			free(hm);
			return NULL;
		}
	}
	
	return hm;
}

void hook_manager_destroy(struct hook_manager *hm, bool wait)
{
	size_t i;
	
	if (!hm)
		return;
	
	/* Wait for pending executions if requested */
	if (wait) {
		hook_manager_wait(hm);
	}
	
	/* Cleanup hooks */
	for (i = 0; i < hm->max_hooks; i++) {
		if (hm->hooks[i].in_use && hm->hooks[i].filter)
			filter_bytecode_free(hm->hooks[i].filter);
		pthread_mutex_destroy(&hm->hooks[i].stats_mutex);
	}
	
	free(hm->hooks);
	pthread_cond_destroy(&hm->exec_cond);
	pthread_mutex_destroy(&hm->exec_mutex);
	pthread_mutex_destroy(&hm->hooks_mutex);
	free(hm);
}

int hook_manager_register(struct hook_manager *hm, const struct hook_config *config)
{
	struct hook_entry *hook = NULL;
	size_t i;
	int id;
	
	if (!hm || !config)
		return -1;
	
	/* Validate configuration */
	if (config->name[0] == '\0' || config->script[0] == '\0')
		return -1;
	
	pthread_mutex_lock(&hm->hooks_mutex);
	
	/* Find free slot */
	for (i = 0; i < hm->max_hooks; i++) {
		if (!hm->hooks[i].in_use) {
			hook = &hm->hooks[i];
			break;
		}
	}
	
	if (!hook) {
		pthread_mutex_unlock(&hm->hooks_mutex);
		return -1;
	}
	
	/* Initialize hook */
	memset(hook, 0, sizeof(*hook));
	id = hm->next_hook_id++;
	hook->id = id;
	hook->config = *config;
	hook->in_use = true;
	
	/* Set default timeout if not specified */
	if (hook->config.timeout_ms == 0)
		hook->config.timeout_ms = 30000;  /* 30 seconds */
	
	/* Compile filter condition if specified */
	if (config->condition[0] != '\0') {
		struct filter_expr *expr = filter_parse(config->condition);
		if (!expr || !expr->valid) {
			if (expr)
				filter_expr_free(expr);
			hook->in_use = false;
			pthread_mutex_unlock(&hm->hooks_mutex);
			return -1;
		}
		
		hook->filter = filter_compile(expr);
		filter_expr_free(expr);
		
		if (!hook->filter) {
			hook->in_use = false;
			pthread_mutex_unlock(&hm->hooks_mutex);
			return -1;
		}
	}
	
	pthread_mutex_unlock(&hm->hooks_mutex);
	
	return id;
}

bool hook_manager_unregister(struct hook_manager *hm, int hook_id)
{
	struct hook_entry *hook = NULL;
	size_t i;
	
	if (!hm)
		return false;
	
	pthread_mutex_lock(&hm->hooks_mutex);
	
	/* Find hook */
	for (i = 0; i < hm->max_hooks; i++) {
		if (hm->hooks[i].in_use && hm->hooks[i].id == hook_id) {
			hook = &hm->hooks[i];
			break;
		}
	}
	
	if (!hook) {
		pthread_mutex_unlock(&hm->hooks_mutex);
		return false;
	}
	
	/* Cleanup */
	if (hook->filter)
		filter_bytecode_free(hook->filter);
	
	hook->in_use = false;
	
	pthread_mutex_unlock(&hm->hooks_mutex);
	
	return true;
}

bool hook_manager_enable(struct hook_manager *hm, int hook_id)
{
	struct hook_entry *hook = NULL;
	size_t i;
	
	if (!hm)
		return false;
	
	pthread_mutex_lock(&hm->hooks_mutex);
	
	for (i = 0; i < hm->max_hooks; i++) {
		if (hm->hooks[i].in_use && hm->hooks[i].id == hook_id) {
			hook = &hm->hooks[i];
			break;
		}
	}
	
	if (hook)
		hook->config.enabled = true;
	
	pthread_mutex_unlock(&hm->hooks_mutex);
	
	return hook != NULL;
}

bool hook_manager_disable(struct hook_manager *hm, int hook_id)
{
	struct hook_entry *hook = NULL;
	size_t i;
	
	if (!hm)
		return false;
	
	pthread_mutex_lock(&hm->hooks_mutex);
	
	for (i = 0; i < hm->max_hooks; i++) {
		if (hm->hooks[i].in_use && hm->hooks[i].id == hook_id) {
			hook = &hm->hooks[i];
			break;
		}
	}
	
	if (hook)
		hook->config.enabled = false;
	
	pthread_mutex_unlock(&hm->hooks_mutex);
	
	return hook != NULL;
}

void hook_manager_execute(struct hook_manager *hm, struct nlmon_event *event)
{
	size_t i;
	
	if (!hm || !event)
		return;
	
	pthread_mutex_lock(&hm->hooks_mutex);
	
	/* Iterate through all hooks */
	for (i = 0; i < hm->max_hooks; i++) {
		struct hook_entry *hook = &hm->hooks[i];
		
		if (!hook->in_use || !hook->config.enabled)
			continue;
		
		/* Evaluate condition if present */
		if (hook->filter) {
			if (!filter_eval(hook->filter, event, NULL))
				continue;
		}
		
		/* Check concurrent execution limit */
		pthread_mutex_lock(&hm->exec_mutex);
		while (hm->active_executions >= hm->max_concurrent) {
			pthread_cond_wait(&hm->exec_cond, &hm->exec_mutex);
		}
		hm->active_executions++;
		pthread_mutex_unlock(&hm->exec_mutex);
		
		/* Execute hook */
		if (hook->config.async) {
			/* Async execution */
			struct async_exec_ctx *ctx = malloc(sizeof(*ctx));
			if (ctx) {
				struct nlmon_event *event_copy = malloc(sizeof(*event));
				if (event_copy) {
					memcpy(event_copy, event, sizeof(*event));
					ctx->hm = hm;
					ctx->hook = hook;
					ctx->event = event_copy;
					ctx->envp = build_event_env(event);
					
					pthread_t thread;
					if (pthread_create(&thread, NULL, async_exec_thread, ctx) == 0) {
						pthread_detach(thread);
					} else {
						free(event_copy);
						free_event_env(ctx->envp);
						free(ctx);
						pthread_mutex_lock(&hm->exec_mutex);
						hm->active_executions--;
						pthread_cond_signal(&hm->exec_cond);
						pthread_mutex_unlock(&hm->exec_mutex);
					}
				} else {
					free(ctx);
					pthread_mutex_lock(&hm->exec_mutex);
					hm->active_executions--;
					pthread_cond_signal(&hm->exec_cond);
					pthread_mutex_unlock(&hm->exec_mutex);
				}
			} else {
				pthread_mutex_lock(&hm->exec_mutex);
				hm->active_executions--;
				pthread_cond_signal(&hm->exec_cond);
				pthread_mutex_unlock(&hm->exec_mutex);
			}
		} else {
			/* Synchronous execution */
			char **envp = build_event_env(event);
			enum hook_result result;
			uint64_t start_time, duration;
			
			start_time = get_time_ms();
			result = execute_script(hook->config.script, envp,
			                       hook->config.timeout_ms,
			                       false, NULL, 0);
			duration = get_time_ms() - start_time;
			
			/* Update statistics */
			pthread_mutex_lock(&hook->stats_mutex);
			hook->stats.executions++;
			hook->stats.total_duration_ms += duration;
			
			if (hook->stats.min_duration_ms == 0 || duration < hook->stats.min_duration_ms)
				hook->stats.min_duration_ms = duration;
			if (duration > hook->stats.max_duration_ms)
				hook->stats.max_duration_ms = duration;
			
			switch (result) {
			case HOOK_RESULT_SUCCESS:
				hook->stats.successes++;
				break;
			case HOOK_RESULT_TIMEOUT:
				hook->stats.timeouts++;
				break;
			case HOOK_RESULT_ERROR:
				hook->stats.failures++;
				break;
			default:
				break;
			}
			pthread_mutex_unlock(&hook->stats_mutex);
			
			free_event_env(envp);
			
			pthread_mutex_lock(&hm->exec_mutex);
			hm->active_executions--;
			pthread_cond_signal(&hm->exec_cond);
			pthread_mutex_unlock(&hm->exec_mutex);
		}
	}
	
	pthread_mutex_unlock(&hm->hooks_mutex);
}

bool hook_manager_get_stats(struct hook_manager *hm, int hook_id,
                            struct hook_stats *stats)
{
	struct hook_entry *hook = NULL;
	size_t i;
	
	if (!hm || !stats)
		return false;
	
	pthread_mutex_lock(&hm->hooks_mutex);
	
	for (i = 0; i < hm->max_hooks; i++) {
		if (hm->hooks[i].in_use && hm->hooks[i].id == hook_id) {
			hook = &hm->hooks[i];
			break;
		}
	}
	
	if (hook) {
		pthread_mutex_lock(&hook->stats_mutex);
		*stats = hook->stats;
		pthread_mutex_unlock(&hook->stats_mutex);
	}
	
	pthread_mutex_unlock(&hm->hooks_mutex);
	
	return hook != NULL;
}

void hook_manager_reset_stats(struct hook_manager *hm, int hook_id)
{
	size_t i;
	
	if (!hm)
		return;
	
	pthread_mutex_lock(&hm->hooks_mutex);
	
	for (i = 0; i < hm->max_hooks; i++) {
		if (!hm->hooks[i].in_use)
			continue;
		
		if (hook_id == -1 || hm->hooks[i].id == hook_id) {
			pthread_mutex_lock(&hm->hooks[i].stats_mutex);
			memset(&hm->hooks[i].stats, 0, sizeof(hm->hooks[i].stats));
			pthread_mutex_unlock(&hm->hooks[i].stats_mutex);
			
			if (hook_id != -1)
				break;
		}
	}
	
	pthread_mutex_unlock(&hm->hooks_mutex);
}

size_t hook_manager_list(struct hook_manager *hm, int *ids, size_t max_ids)
{
	size_t count = 0;
	size_t i;
	
	if (!hm || !ids || max_ids == 0)
		return 0;
	
	pthread_mutex_lock(&hm->hooks_mutex);
	
	for (i = 0; i < hm->max_hooks && count < max_ids; i++) {
		if (hm->hooks[i].in_use) {
			ids[count++] = hm->hooks[i].id;
		}
	}
	
	pthread_mutex_unlock(&hm->hooks_mutex);
	
	return count;
}

bool hook_manager_get_config(struct hook_manager *hm, int hook_id,
                             struct hook_config *config)
{
	struct hook_entry *hook = NULL;
	size_t i;
	
	if (!hm || !config)
		return false;
	
	pthread_mutex_lock(&hm->hooks_mutex);
	
	for (i = 0; i < hm->max_hooks; i++) {
		if (hm->hooks[i].in_use && hm->hooks[i].id == hook_id) {
			hook = &hm->hooks[i];
			break;
		}
	}
	
	if (hook)
		*config = hook->config;
	
	pthread_mutex_unlock(&hm->hooks_mutex);
	
	return hook != NULL;
}

void hook_manager_wait(struct hook_manager *hm)
{
	if (!hm)
		return;
	
	pthread_mutex_lock(&hm->exec_mutex);
	while (hm->active_executions > 0) {
		pthread_cond_wait(&hm->exec_cond, &hm->exec_mutex);
	}
	pthread_mutex_unlock(&hm->exec_mutex);
}

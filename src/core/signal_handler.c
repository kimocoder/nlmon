/* signal_handler.c - Signal handling implementation */

#include <stdlib.h>
#include <stdbool.h>
#include <signal.h>
#include <pthread.h>
#include <stdatomic.h>
#include "signal_handler.h"

/* Callback entry */
struct callback_entry {
	int id;
	shutdown_callback_fn callback;
	void *ctx;
	struct callback_entry *next;
};

/* Signal handler structure */
struct signal_handler {
	struct callback_entry *callbacks;
	pthread_mutex_t mutex;
	int next_id;
	atomic_bool exit_requested;
	
	/* Original signal handlers */
	struct sigaction old_sigint;
	struct sigaction old_sigterm;
	struct sigaction old_sighup;
};

/* Global signal handler instance (for signal handler) */
static struct signal_handler *g_handler = NULL;

/* Signal handler function */
static void signal_handler_func(int signo)
{
	struct callback_entry *entry;
	
	if (!g_handler)
		return;
	
	/* Set exit flag */
	atomic_store_explicit(&g_handler->exit_requested, true, memory_order_release);
	
	/* Call all registered callbacks */
	pthread_mutex_lock(&g_handler->mutex);
	for (entry = g_handler->callbacks; entry; entry = entry->next) {
		if (entry->callback)
			entry->callback(entry->ctx);
	}
	pthread_mutex_unlock(&g_handler->mutex);
}

struct signal_handler *signal_handler_init(void)
{
	struct signal_handler *handler;
	struct sigaction sa;
	
	handler = calloc(1, sizeof(*handler));
	if (!handler)
		return NULL;
	
	if (pthread_mutex_init(&handler->mutex, NULL) != 0) {
		free(handler);
		return NULL;
	}
	
	atomic_init(&handler->exit_requested, false);
	handler->next_id = 1;
	
	/* Set up signal handlers */
	sa.sa_handler = signal_handler_func;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	
	if (sigaction(SIGINT, &sa, &handler->old_sigint) < 0) {
		pthread_mutex_destroy(&handler->mutex);
		free(handler);
		return NULL;
	}
	
	if (sigaction(SIGTERM, &sa, &handler->old_sigterm) < 0) {
		sigaction(SIGINT, &handler->old_sigint, NULL);
		pthread_mutex_destroy(&handler->mutex);
		free(handler);
		return NULL;
	}
	
	if (sigaction(SIGHUP, &sa, &handler->old_sighup) < 0) {
		sigaction(SIGINT, &handler->old_sigint, NULL);
		sigaction(SIGTERM, &handler->old_sigterm, NULL);
		pthread_mutex_destroy(&handler->mutex);
		free(handler);
		return NULL;
	}
	
	g_handler = handler;
	return handler;
}

void signal_handler_cleanup(struct signal_handler *handler)
{
	struct callback_entry *entry, *next;
	
	if (!handler)
		return;
	
	/* Restore original signal handlers */
	sigaction(SIGINT, &handler->old_sigint, NULL);
	sigaction(SIGTERM, &handler->old_sigterm, NULL);
	sigaction(SIGHUP, &handler->old_sighup, NULL);
	
	/* Free callbacks */
	pthread_mutex_lock(&handler->mutex);
	entry = handler->callbacks;
	while (entry) {
		next = entry->next;
		free(entry);
		entry = next;
	}
	pthread_mutex_unlock(&handler->mutex);
	
	pthread_mutex_destroy(&handler->mutex);
	
	if (g_handler == handler)
		g_handler = NULL;
	
	free(handler);
}

int signal_handler_register_callback(struct signal_handler *handler,
                                     shutdown_callback_fn callback,
                                     void *ctx)
{
	struct callback_entry *entry;
	int id;
	
	if (!handler || !callback)
		return -1;
	
	entry = malloc(sizeof(*entry));
	if (!entry)
		return -1;
	
	pthread_mutex_lock(&handler->mutex);
	
	id = handler->next_id++;
	entry->id = id;
	entry->callback = callback;
	entry->ctx = ctx;
	entry->next = handler->callbacks;
	handler->callbacks = entry;
	
	pthread_mutex_unlock(&handler->mutex);
	
	return id;
}

void signal_handler_unregister_callback(struct signal_handler *handler,
                                        int callback_id)
{
	struct callback_entry *entry, *prev;
	
	if (!handler)
		return;
	
	pthread_mutex_lock(&handler->mutex);
	
	prev = NULL;
	for (entry = handler->callbacks; entry; entry = entry->next) {
		if (entry->id == callback_id) {
			if (prev)
				prev->next = entry->next;
			else
				handler->callbacks = entry->next;
			free(entry);
			break;
		}
		prev = entry;
	}
	
	pthread_mutex_unlock(&handler->mutex);
}

bool signal_handler_should_exit(struct signal_handler *handler)
{
	if (!handler)
		return false;
	
	return atomic_load_explicit(&handler->exit_requested, memory_order_acquire);
}

void signal_handler_wait_for_signal(struct signal_handler *handler)
{
	sigset_t mask;
	int sig;
	
	if (!handler)
		return;
	
	/* Wait for signals */
	sigemptyset(&mask);
	sigaddset(&mask, SIGINT);
	sigaddset(&mask, SIGTERM);
	sigaddset(&mask, SIGHUP);
	
	sigwait(&mask, &sig);
}

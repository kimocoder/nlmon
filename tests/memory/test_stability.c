/* test_stability.c - Long-running stability test */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include "event_processor.h"
#include "object_pool.h"
#include "ring_buffer.h"
#include "resource_tracker.h"

static volatile int g_running = 1;
static uint64_t g_events_processed = 0;

static void signal_handler(int sig)
{
	(void)sig;
	g_running = 0;
}

static void event_callback(struct nlmon_event *event, void *ctx)
{
	__sync_fetch_and_add(&g_events_processed, 1);
}

static void print_stats(struct resource_tracker *tracker, time_t start_time)
{
	struct resource_stats stats;
	time_t elapsed = time(NULL) - start_time;
	
	if (resource_tracker_get_stats(tracker, &stats)) {
		printf("\n=== Stability Test Statistics ===\n");
		printf("Elapsed time:      %ld seconds\n", elapsed);
		printf("Events processed:  %lu\n", g_events_processed);
		printf("Events/sec:        %.2f\n", (double)g_events_processed / elapsed);
		printf("Memory RSS:        %lu bytes (%.2f MB)\n",
		       stats.memory_rss_bytes,
		       (double)stats.memory_rss_bytes / (1024.0 * 1024.0));
		printf("Memory VMS:        %lu bytes (%.2f MB)\n",
		       stats.memory_vms_bytes,
		       (double)stats.memory_vms_bytes / (1024.0 * 1024.0));
		printf("CPU usage:         %.2f%%\n", stats.cpu_usage_percent);
	}
}

int main(int argc, char *argv[])
{
	struct event_processor *ep;
	struct event_processor_config ep_config = {0};
	struct resource_tracker *tracker;
	int duration_sec = 60; /* Default 1 minute */
	time_t start_time, last_report;
	
	if (argc > 1) {
		duration_sec = atoi(argv[1]);
		if (duration_sec <= 0) {
			duration_sec = 60;
		}
	}
	
	printf("nlmon Stability Test\n");
	printf("====================\n");
	printf("Duration: %d seconds\n", duration_sec);
	printf("Press Ctrl+C to stop early\n\n");
	
	/* Setup signal handler */
	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);
	
	/* Create event processor */
	ep_config.buffer_size = 1000;
	ep_config.worker_threads = 4;
	ep_config.rate_limit = 0;
	
	ep = event_processor_create(&ep_config);
	if (!ep) {
		fprintf(stderr, "Failed to create event processor\n");
		return 1;
	}
	
	event_processor_register_callback(ep, event_callback, NULL);
	
	if (event_processor_start(ep) != 0) {
		fprintf(stderr, "Failed to start event processor\n");
		event_processor_destroy(ep);
		return 1;
	}
	
	/* Create resource tracker */
	tracker = resource_tracker_create(0);
	if (!tracker) {
		fprintf(stderr, "Failed to create resource tracker\n");
		event_processor_stop(ep);
		event_processor_destroy(ep);
		return 1;
	}
	
	start_time = time(NULL);
	last_report = start_time;
	
	printf("Test started at %s", ctime(&start_time));
	printf("Submitting events...\n");
	
	/* Main test loop */
	while (g_running && (time(NULL) - start_time) < duration_sec) {
		struct nlmon_event event = {0};
		
		/* Create test event */
		event.message_type = 16;
		event.sequence = g_events_processed;
		snprintf(event.interface, sizeof(event.interface), "eth%lu",
		         g_events_processed % 10);
		event.event_type = 1;
		
		/* Submit event */
		event_processor_submit(ep, &event);
		
		/* Update resource metrics periodically */
		if (time(NULL) - last_report >= 10) {
			resource_tracker_update_system_metrics(tracker);
			print_stats(tracker, start_time);
			last_report = time(NULL);
		}
		
		/* Small delay to avoid overwhelming the system */
		usleep(100); /* 100 microseconds */
	}
	
	printf("\nTest stopping...\n");
	
	/* Final statistics */
	resource_tracker_update_system_metrics(tracker);
	print_stats(tracker, start_time);
	
	/* Cleanup */
	event_processor_stop(ep);
	event_processor_destroy(ep);
	resource_tracker_destroy(tracker);
	
	printf("\nStability test completed successfully!\n");
	
	return 0;
}

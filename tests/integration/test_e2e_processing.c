/* test_e2e_processing.c - End-to-end event processing test */

#include "../unit/test_framework.h"
#include "netlink_simulator.h"
#include "event_processor.h"
#include "filter_manager.h"
#include "storage_layer.h"
#include <unistd.h>

static int events_received = 0;

static void event_callback(struct nlmon_event *event, void *ctx)
{
	events_received++;
}

TEST(e2e_basic_processing)
{
	struct event_processor *ep;
	struct event_processor_config config = {0};
	
	config.buffer_size = 100;
	config.worker_threads = 2;
	config.rate_limit = 1000;
	
	ep = event_processor_create(&config);
	ASSERT_NOT_NULL(ep);
	
	/* Register callback */
	event_processor_register_callback(ep, event_callback, NULL);
	
	/* Start processor */
	ASSERT_EQ(event_processor_start(ep), 0);
	
	/* Simulate events */
	events_received = 0;
	
	struct sim_netlink_event *sim_event;
	struct nlmon_event nlmon_event;
	
	/* Create and process NEWLINK event */
	sim_event = sim_create_newlink("eth0", 2, IFF_UP | IFF_RUNNING);
	ASSERT_NOT_NULL(sim_event);
	
	sim_to_nlmon_event(sim_event, &nlmon_event);
	event_processor_submit(ep, &nlmon_event);
	sim_free_event(sim_event);
	
	/* Create and process NEWADDR event */
	sim_event = sim_create_newaddr("eth0", 2, "192.168.1.100", 24);
	ASSERT_NOT_NULL(sim_event);
	
	sim_to_nlmon_event(sim_event, &nlmon_event);
	event_processor_submit(ep, &nlmon_event);
	sim_free_event(sim_event);
	
	/* Wait for processing */
	usleep(100000); /* 100ms */
	
	/* Verify events were processed */
	ASSERT_EQ(events_received, 2);
	
	/* Stop and cleanup */
	event_processor_stop(ep);
	event_processor_destroy(ep);
}

TEST(e2e_filtering)
{
	struct event_processor *ep;
	struct filter_manager *fm;
	struct event_processor_config ep_config = {0};
	struct filter_manager_config fm_config = {0};
	
	ep_config.buffer_size = 100;
	ep_config.worker_threads = 2;
	
	fm_config.max_filters = 10;
	
	ep = event_processor_create(&ep_config);
	ASSERT_NOT_NULL(ep);
	
	fm = filter_manager_create(&fm_config);
	ASSERT_NOT_NULL(fm);
	
	/* Add filter for eth0 only */
	int filter_id = filter_manager_add(fm, "eth0_only", "interface == \"eth0\"");
	ASSERT_TRUE(filter_id >= 0);
	
	events_received = 0;
	
	/* Register callback that checks filter */
	event_processor_register_callback(ep, event_callback, NULL);
	event_processor_start(ep);
	
	/* Create events */
	struct sim_netlink_event *sim_event;
	struct nlmon_event nlmon_event;
	
	/* eth0 event - should match */
	sim_event = sim_create_newlink("eth0", 2, IFF_UP);
	sim_to_nlmon_event(sim_event, &nlmon_event);
	
	if (filter_manager_eval(fm, filter_id, &nlmon_event)) {
		event_processor_submit(ep, &nlmon_event);
	}
	sim_free_event(sim_event);
	
	/* eth1 event - should not match */
	sim_event = sim_create_newlink("eth1", 3, IFF_UP);
	sim_to_nlmon_event(sim_event, &nlmon_event);
	
	if (filter_manager_eval(fm, filter_id, &nlmon_event)) {
		event_processor_submit(ep, &nlmon_event);
	}
	sim_free_event(sim_event);
	
	usleep(100000);
	
	/* Only eth0 event should have been processed */
	ASSERT_EQ(events_received, 1);
	
	event_processor_stop(ep);
	event_processor_destroy(ep);
	filter_manager_destroy(fm);
}

TEST(e2e_storage)
{
	struct event_processor *ep;
	struct storage_layer *storage;
	struct event_processor_config ep_config = {0};
	struct storage_config storage_config = {0};
	
	ep_config.buffer_size = 100;
	ep_config.worker_threads = 2;
	
	storage_config.memory_buffer_size = 100;
	storage_config.enable_database = false; /* Use memory only for test */
	
	ep = event_processor_create(&ep_config);
	ASSERT_NOT_NULL(ep);
	
	storage = storage_layer_create(&storage_config);
	ASSERT_NOT_NULL(storage);
	
	event_processor_start(ep);
	
	/* Create and process events */
	struct sim_netlink_event *sim_event;
	struct nlmon_event nlmon_event;
	
	for (int i = 0; i < 5; i++) {
		char ifname[16];
		snprintf(ifname, sizeof(ifname), "eth%d", i);
		
		sim_event = sim_create_newlink(ifname, i + 2, IFF_UP);
		sim_to_nlmon_event(sim_event, &nlmon_event);
		
		/* Store event */
		storage_layer_store(storage, &nlmon_event);
		
		sim_free_event(sim_event);
	}
	
	usleep(50000);
	
	/* Query stored events */
	struct nlmon_event *events[10];
	size_t count = storage_layer_query(storage, NULL, events, 10);
	
	ASSERT_EQ(count, 5);
	
	event_processor_stop(ep);
	event_processor_destroy(ep);
	storage_layer_destroy(storage);
}

TEST(e2e_rate_limiting)
{
	struct event_processor *ep;
	struct event_processor_config config = {0};
	
	config.buffer_size = 100;
	config.worker_threads = 2;
	config.rate_limit = 10; /* 10 events per second */
	
	ep = event_processor_create(&config);
	ASSERT_NOT_NULL(ep);
	
	events_received = 0;
	event_processor_register_callback(ep, event_callback, NULL);
	event_processor_start(ep);
	
	/* Submit 50 events rapidly */
	struct sim_netlink_event *sim_event;
	struct nlmon_event nlmon_event;
	
	for (int i = 0; i < 50; i++) {
		sim_event = sim_create_newlink("eth0", 2, IFF_UP);
		sim_to_nlmon_event(sim_event, &nlmon_event);
		event_processor_submit(ep, &nlmon_event);
		sim_free_event(sim_event);
	}
	
	usleep(100000); /* 100ms */
	
	/* Should have processed only ~10 events due to rate limiting */
	ASSERT_TRUE(events_received <= 15); /* Allow some tolerance */
	
	event_processor_stop(ep);
	event_processor_destroy(ep);
}

TEST_SUITE_BEGIN("End-to-End Processing")
	RUN_TEST(e2e_basic_processing);
	RUN_TEST(e2e_filtering);
	RUN_TEST(e2e_storage);
	RUN_TEST(e2e_rate_limiting);
TEST_SUITE_END()

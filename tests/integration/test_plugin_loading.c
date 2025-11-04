/* test_plugin_loading.c - Integration test for plugin system */

#include "../unit/test_framework.h"
#include "plugin_api.h"
#include <dlfcn.h>
#include <unistd.h>

#ifdef ENABLE_PLUGINS

TEST(plugin_load_example)
{
	void *handle;
	nlmon_plugin_register_fn register_fn;
	nlmon_plugin_t *plugin;
	
	/* Load example plugin */
	handle = dlopen("test_plugins/example_plugin.so", RTLD_NOW);
	ASSERT_NOT_NULL(handle);
	
	/* Get register function */
	register_fn = (nlmon_plugin_register_fn)dlsym(handle, "nlmon_plugin_register");
	ASSERT_NOT_NULL(register_fn);
	
	/* Register plugin */
	plugin = register_fn();
	ASSERT_NOT_NULL(plugin);
	
	/* Verify plugin info */
	ASSERT_NOT_NULL(plugin->name);
	ASSERT_NOT_NULL(plugin->version);
	ASSERT_EQ(plugin->api_version, NLMON_PLUGIN_API_VERSION);
	
	/* Initialize plugin */
	if (plugin->init) {
		ASSERT_EQ(plugin->init(NULL), 0);
	}
	
	/* Cleanup plugin */
	if (plugin->cleanup) {
		plugin->cleanup();
	}
	
	dlclose(handle);
}

TEST(plugin_api_version_check)
{
	void *handle;
	nlmon_plugin_register_fn register_fn;
	nlmon_plugin_t *plugin;
	
	handle = dlopen("test_plugins/example_plugin.so", RTLD_NOW);
	ASSERT_NOT_NULL(handle);
	
	register_fn = (nlmon_plugin_register_fn)dlsym(handle, "nlmon_plugin_register");
	ASSERT_NOT_NULL(register_fn);
	
	plugin = register_fn();
	ASSERT_NOT_NULL(plugin);
	
	/* Verify API version matches */
	ASSERT_EQ(plugin->api_version, NLMON_PLUGIN_API_VERSION);
	
	dlclose(handle);
}

TEST(plugin_event_callback)
{
	void *handle;
	nlmon_plugin_register_fn register_fn;
	nlmon_plugin_t *plugin;
	struct nlmon_event event = {0};
	
	handle = dlopen("test_plugins/example_plugin.so", RTLD_NOW);
	ASSERT_NOT_NULL(handle);
	
	register_fn = (nlmon_plugin_register_fn)dlsym(handle, "nlmon_plugin_register");
	plugin = register_fn();
	ASSERT_NOT_NULL(plugin);
	
	if (plugin->init) {
		plugin->init(NULL);
	}
	
	/* Test event callback */
	if (plugin->on_event) {
		event.message_type = 16; /* RTM_NEWLINK */
		strncpy(event.interface, "eth0", sizeof(event.interface) - 1);
		
		int result = plugin->on_event(&event);
		ASSERT_EQ(result, 0);
	}
	
	if (plugin->cleanup) {
		plugin->cleanup();
	}
	
	dlclose(handle);
}

TEST(plugin_command_callback)
{
	void *handle;
	nlmon_plugin_register_fn register_fn;
	nlmon_plugin_t *plugin;
	char response[256];
	
	handle = dlopen("test_plugins/example_plugin.so", RTLD_NOW);
	ASSERT_NOT_NULL(handle);
	
	register_fn = (nlmon_plugin_register_fn)dlsym(handle, "nlmon_plugin_register");
	plugin = register_fn();
	ASSERT_NOT_NULL(plugin);
	
	if (plugin->init) {
		plugin->init(NULL);
	}
	
	/* Test command callback */
	if (plugin->on_command) {
		int result = plugin->on_command("status", response, sizeof(response));
		ASSERT_EQ(result, 0);
		ASSERT_TRUE(strlen(response) > 0);
	}
	
	if (plugin->cleanup) {
		plugin->cleanup();
	}
	
	dlclose(handle);
}

TEST(plugin_lifecycle)
{
	void *handle;
	nlmon_plugin_register_fn register_fn;
	nlmon_plugin_t *plugin;
	
	handle = dlopen("test_plugins/example_plugin.so", RTLD_NOW);
	ASSERT_NOT_NULL(handle);
	
	register_fn = (nlmon_plugin_register_fn)dlsym(handle, "nlmon_plugin_register");
	plugin = register_fn();
	ASSERT_NOT_NULL(plugin);
	
	/* Test init */
	if (plugin->init) {
		ASSERT_EQ(plugin->init(NULL), 0);
	}
	
	/* Process some events */
	if (plugin->on_event) {
		struct nlmon_event event = {0};
		for (int i = 0; i < 10; i++) {
			event.sequence = i;
			plugin->on_event(&event);
		}
	}
	
	/* Test cleanup */
	if (plugin->cleanup) {
		plugin->cleanup();
	}
	
	dlclose(handle);
}

#else

TEST(plugin_disabled)
{
	/* Plugin system not enabled */
	ASSERT_TRUE(true);
}

#endif /* ENABLE_PLUGINS */

TEST_SUITE_BEGIN("Plugin Loading")
#ifdef ENABLE_PLUGINS
	RUN_TEST(plugin_load_example);
	RUN_TEST(plugin_api_version_check);
	RUN_TEST(plugin_event_callback);
	RUN_TEST(plugin_command_callback);
	RUN_TEST(plugin_lifecycle);
#else
	RUN_TEST(plugin_disabled);
#endif
TEST_SUITE_END()

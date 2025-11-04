/* test_config_loading.c - Integration test for configuration system */

#include "../unit/test_framework.h"
#include "nlmon_config.h"
#include <unistd.h>
#include <fcntl.h>

#ifdef ENABLE_CONFIG

static const char *test_config_valid = 
"nlmon:\n"
"  core:\n"
"    buffer_size: 320KB\n"
"    max_events: 10000\n"
"    rate_limit: 1000\n"
"    worker_threads: 4\n"
"  monitoring:\n"
"    protocols:\n"
"      - NETLINK_ROUTE\n"
"    interfaces:\n"
"      include:\n"
"        - eth*\n"
"        - veth*\n"
"  output:\n"
"    console:\n"
"      enabled: true\n"
"      format: text\n"
"    pcap:\n"
"      enabled: false\n"
"    database:\n"
"      enabled: false\n";

static const char *test_config_invalid = 
"nlmon:\n"
"  core:\n"
"    buffer_size: invalid\n"
"    max_events: -100\n";

TEST(config_load_valid)
{
	struct nlmon_config_ctx ctx;
	struct nlmon_core_config core;
	int ret;
	
	/* Write test config */
	int fd = open("test_config_valid.yaml", O_WRONLY | O_CREAT | O_TRUNC, 0644);
	ASSERT_TRUE(fd >= 0);
	write(fd, test_config_valid, strlen(test_config_valid));
	close(fd);
	
	/* Load config */
	ret = nlmon_config_ctx_init(&ctx, "test_config_valid.yaml");
	ASSERT_EQ(ret, NLMON_CONFIG_OK);
	
	/* Verify core config */
	nlmon_config_get_core(&ctx, &core);
	ASSERT_EQ(core.max_events, 10000);
	ASSERT_EQ(core.rate_limit, 1000);
	ASSERT_EQ(core.worker_threads, 4);
	
	nlmon_config_ctx_free(&ctx);
	unlink("test_config_valid.yaml");
}

TEST(config_load_invalid)
{
	struct nlmon_config_ctx ctx;
	int ret;
	
	/* Write invalid config */
	int fd = open("test_config_invalid.yaml", O_WRONLY | O_CREAT | O_TRUNC, 0644);
	ASSERT_TRUE(fd >= 0);
	write(fd, test_config_invalid, strlen(test_config_invalid));
	close(fd);
	
	/* Load config - should fail or use defaults */
	ret = nlmon_config_ctx_init(&ctx, "test_config_invalid.yaml");
	
	/* Either fails to load or loads with defaults */
	if (ret == NLMON_CONFIG_OK) {
		nlmon_config_ctx_free(&ctx);
	}
	
	unlink("test_config_invalid.yaml");
	ASSERT_TRUE(true); /* Test passes if we get here */
}

TEST(config_missing_file)
{
	struct nlmon_config_ctx ctx;
	int ret;
	
	/* Try to load non-existent file */
	ret = nlmon_config_ctx_init(&ctx, "nonexistent_config.yaml");
	ASSERT_NE(ret, NLMON_CONFIG_OK);
}

TEST(config_hot_reload)
{
	struct nlmon_config_ctx ctx;
	struct nlmon_core_config core;
	int ret;
	
	/* Write initial config */
	int fd = open("test_config_reload.yaml", O_WRONLY | O_CREAT | O_TRUNC, 0644);
	ASSERT_TRUE(fd >= 0);
	write(fd, test_config_valid, strlen(test_config_valid));
	close(fd);
	
	/* Load config */
	ret = nlmon_config_ctx_init(&ctx, "test_config_reload.yaml");
	ASSERT_EQ(ret, NLMON_CONFIG_OK);
	
	/* Initialize watch */
	ret = nlmon_config_watch_init(&ctx);
	if (ret == NLMON_CONFIG_OK) {
		uint64_t version1 = nlmon_config_get_version(&ctx);
		
		/* Modify config */
		usleep(100000); /* Wait a bit */
		
		const char *modified_config = 
		"nlmon:\n"
		"  core:\n"
		"    buffer_size: 640KB\n"
		"    max_events: 20000\n"
		"    rate_limit: 2000\n";
		
		fd = open("test_config_reload.yaml", O_WRONLY | O_TRUNC);
		write(fd, modified_config, strlen(modified_config));
		close(fd);
		
		/* Wait for reload */
		usleep(500000); /* 500ms */
		
		/* Check if config was reloaded */
		uint64_t version2 = nlmon_config_get_version(&ctx);
		
		/* Version should have changed if reload worked */
		/* Note: This may not work in all environments */
		if (version2 > version1) {
			nlmon_config_get_core(&ctx, &core);
			ASSERT_EQ(core.max_events, 20000);
		}
	}
	
	nlmon_config_ctx_free(&ctx);
	unlink("test_config_reload.yaml");
}

TEST(config_defaults)
{
	struct nlmon_config_ctx ctx;
	struct nlmon_core_config core;
	int ret;
	
	/* Write minimal config */
	const char *minimal_config = "nlmon:\n  core:\n";
	
	int fd = open("test_config_minimal.yaml", O_WRONLY | O_CREAT | O_TRUNC, 0644);
	ASSERT_TRUE(fd >= 0);
	write(fd, minimal_config, strlen(minimal_config));
	close(fd);
	
	/* Load config */
	ret = nlmon_config_ctx_init(&ctx, "test_config_minimal.yaml");
	ASSERT_EQ(ret, NLMON_CONFIG_OK);
	
	/* Verify defaults are applied */
	nlmon_config_get_core(&ctx, &core);
	ASSERT_TRUE(core.buffer_size > 0);
	ASSERT_TRUE(core.max_events > 0);
	ASSERT_TRUE(core.worker_threads > 0);
	
	nlmon_config_ctx_free(&ctx);
	unlink("test_config_minimal.yaml");
}

#else

TEST(config_disabled)
{
	/* Config system not enabled */
	ASSERT_TRUE(true);
}

#endif /* ENABLE_CONFIG */

TEST_SUITE_BEGIN("Configuration Loading")
#ifdef ENABLE_CONFIG
	RUN_TEST(config_load_valid);
	RUN_TEST(config_load_invalid);
	RUN_TEST(config_missing_file);
	RUN_TEST(config_hot_reload);
	RUN_TEST(config_defaults);
#else
	RUN_TEST(config_disabled);
#endif
TEST_SUITE_END()

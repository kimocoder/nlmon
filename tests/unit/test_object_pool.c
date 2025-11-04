/* test_object_pool.c - Unit tests for object pool */

#include "test_framework.h"
#include "object_pool.h"
#include <pthread.h>

TEST(object_pool_create_destroy)
{
	struct object_pool *pool = object_pool_create(10, 64, NULL, NULL);
	ASSERT_NOT_NULL(pool);
	
	size_t capacity, available, allocated;
	object_pool_stats(pool, &capacity, &available, &allocated);
	
	ASSERT_EQ(capacity, 10);
	ASSERT_EQ(available, 10);
	ASSERT_EQ(allocated, 0);
	
	object_pool_destroy(pool);
}

TEST(object_pool_alloc_free)
{
	struct object_pool *pool = object_pool_create(5, 64, NULL, NULL);
	ASSERT_NOT_NULL(pool);
	
	void *obj = object_pool_alloc(pool);
	ASSERT_NOT_NULL(obj);
	
	size_t capacity, available, allocated;
	object_pool_stats(pool, &capacity, &available, &allocated);
	ASSERT_EQ(available, 4);
	ASSERT_EQ(allocated, 1);
	
	object_pool_free(pool, obj);
	
	object_pool_stats(pool, &capacity, &available, &allocated);
	ASSERT_EQ(available, 5);
	ASSERT_EQ(allocated, 0);
	
	object_pool_destroy(pool);
}

TEST(object_pool_exhaustion)
{
	struct object_pool *pool = object_pool_create(3, 64, NULL, NULL);
	ASSERT_NOT_NULL(pool);
	
	void *objs[3];
	
	/* Allocate all objects */
	for (int i = 0; i < 3; i++) {
		objs[i] = object_pool_alloc(pool);
		ASSERT_NOT_NULL(objs[i]);
	}
	
	/* Pool should be exhausted */
	void *extra = object_pool_alloc(pool);
	ASSERT_NULL(extra);
	
	/* Free one and try again */
	object_pool_free(pool, objs[0]);
	extra = object_pool_alloc(pool);
	ASSERT_NOT_NULL(extra);
	
	/* Cleanup */
	object_pool_free(pool, extra);
	object_pool_free(pool, objs[1]);
	object_pool_free(pool, objs[2]);
	
	object_pool_destroy(pool);
}

/* Init/cleanup callbacks for testing */
static int init_count = 0;
static int cleanup_count = 0;

static void test_init(void *obj)
{
	init_count++;
	*(int *)obj = 42;
}

static void test_cleanup(void *obj)
{
	cleanup_count++;
	*(int *)obj = 0;
}

TEST(object_pool_callbacks)
{
	init_count = 0;
	cleanup_count = 0;
	
	struct object_pool *pool = object_pool_create(5, sizeof(int),
	                                              test_init, test_cleanup);
	ASSERT_NOT_NULL(pool);
	
	/* Init should be called for all objects */
	ASSERT_EQ(init_count, 5);
	
	int *obj = (int *)object_pool_alloc(pool);
	ASSERT_NOT_NULL(obj);
	ASSERT_EQ(*obj, 42);
	
	object_pool_free(pool, obj);
	ASSERT_EQ(cleanup_count, 1);
	
	object_pool_destroy(pool);
	/* Cleanup should be called for remaining objects */
	ASSERT_EQ(cleanup_count, 5);
}

TEST(object_pool_reuse)
{
	struct object_pool *pool = object_pool_create(2, sizeof(int), NULL, NULL);
	ASSERT_NOT_NULL(pool);
	
	int *obj1 = (int *)object_pool_alloc(pool);
	ASSERT_NOT_NULL(obj1);
	*obj1 = 123;
	
	void *addr1 = obj1;
	object_pool_free(pool, obj1);
	
	int *obj2 = (int *)object_pool_alloc(pool);
	ASSERT_NOT_NULL(obj2);
	
	/* Should reuse the same memory */
	ASSERT_EQ((void *)obj2, addr1);
	
	object_pool_free(pool, obj2);
	object_pool_destroy(pool);
}

/* Thread function for concurrent test */
static void *pool_worker_thread(void *arg)
{
	struct object_pool *pool = (struct object_pool *)arg;
	
	for (int i = 0; i < 100; i++) {
		void *obj = object_pool_alloc(pool);
		if (obj) {
			/* Do some work */
			*(int *)obj = i;
			/* Return to pool */
			object_pool_free(pool, obj);
		}
	}
	
	return NULL;
}

TEST(object_pool_concurrent)
{
	struct object_pool *pool = object_pool_create(10, sizeof(int), NULL, NULL);
	ASSERT_NOT_NULL(pool);
	
	pthread_t threads[4];
	
	for (int i = 0; i < 4; i++) {
		pthread_create(&threads[i], NULL, pool_worker_thread, pool);
	}
	
	for (int i = 0; i < 4; i++) {
		pthread_join(threads[i], NULL);
	}
	
	/* All objects should be back in pool */
	size_t capacity, available, allocated;
	object_pool_stats(pool, &capacity, &available, &allocated);
	ASSERT_EQ(available, 10);
	ASSERT_EQ(allocated, 0);
	
	object_pool_destroy(pool);
}

TEST_SUITE_BEGIN("Object Pool")
	RUN_TEST(object_pool_create_destroy);
	RUN_TEST(object_pool_alloc_free);
	RUN_TEST(object_pool_exhaustion);
	RUN_TEST(object_pool_callbacks);
	RUN_TEST(object_pool_reuse);
	RUN_TEST(object_pool_concurrent);
TEST_SUITE_END()

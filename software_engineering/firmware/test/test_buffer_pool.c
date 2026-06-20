/**
 * @file    test_buffer_pool.c
 * @brief   buffer_pool 计数缓冲池单元测试（宿主机 Mock 测试）
 *
 * @details 测试引用计数缓冲池的分配/回收/引用计数逻辑：
 *            - init 参数校验
 *            - alloc 分配空闲块（ref=1）
 *            - ref/unref 引用计数增减，归零回收
 *            - 多消费者共享（ref 后各自 unref，最后回收）
 *            - available/capacity 查询
 *          osal_sem/mutex 用 FFF fake（take 默认返回 true 模拟有空闲）。
 *          池空阻塞语义留集成测试。
 *
 * @author  xLumina
 * @version 1.0
 */
#include "fff.h"
#include "unity.h"

/* ---- Mock 头 ---- */
#include "esp_err.h"
#include "osal_mutex.h"
#include "osal_semaphore.h"

/* ---- 被测模块 ---- */
#include "buffer_pool_manager.h"

/* ================================================================
 *  测试用静态池
 * ================================================================ */
#define TEST_BLOCK_SIZE  64
#define TEST_CAPACITY    3
BUFFER_POOL_DEFINE(test_pool, TEST_BLOCK_SIZE, TEST_CAPACITY, 4);

/* ================================================================
 *  setUp / tearDown
 * ================================================================ */
void setUp(void)
{
    RESET_FAKE(osal_sem_create_binary);
    RESET_FAKE(osal_sem_create_counting);
    RESET_FAKE(osal_sem_take);
    RESET_FAKE(osal_sem_give);
    RESET_FAKE(osal_sem_delete);

    /* sem 创建返回非 NULL；take 默认成功（有空闲）。
     * osal_mutex 为 inline 空操作 mock，无需 RESET_FAKE。 */
    osal_sem_create_counting_fake.return_val = (osal_sem_t)1;
    osal_sem_take_fake.return_val            = true;

    /* 重置池的 inited 标志（跨用例） */
    test_pool.inited  = false;
    test_pool.freeTop = 0;
    TEST_ASSERT_EQUAL(BP_OK,
                      buffer_pool_init(&test_pool, test_pool_storage,
                                       TEST_BLOCK_SIZE, TEST_CAPACITY,
                                       test_pool_slots, test_pool_freestk, 4));
}

void tearDown(void)
{
    buffer_pool_deinit(&test_pool);
}

/* ================================================================
 *  init
 * ================================================================ */
void test_pool_init_maps_slots_and_fills_free_stack(void)
{
    TEST_ASSERT_TRUE(test_pool.inited);
    TEST_ASSERT_EQUAL(TEST_CAPACITY, test_pool.capacity);
    TEST_ASSERT_EQUAL(TEST_CAPACITY, buffer_pool_available(&test_pool));
    TEST_ASSERT_EQUAL(TEST_CAPACITY, buffer_pool_capacity(&test_pool));
    /* 每个槽位数据指针非空且属于本池 */
    for (uint16_t i = 0; i < TEST_CAPACITY; i++) {
        TEST_ASSERT_NOT_NULL(test_pool.slots[i].buffer);
        TEST_ASSERT_EQUAL_PTR(&test_pool, test_pool.slots[i].pool);
        TEST_ASSERT_EQUAL(0, test_pool.slots[i].ref);
    }
}

void test_pool_init_invalid_args_should_fail(void)
{
    buffer_pool_t p = {0};
    TEST_ASSERT_EQUAL(BP_ERR_PARAM, buffer_pool_init(NULL, NULL, 0, 0, NULL, NULL, 0));
    TEST_ASSERT_EQUAL(BP_ERR_PARAM,
                      buffer_pool_init(&p, (uint8_t[1]){0}, 0, 1, NULL, NULL, 4));
}

void test_pool_init_duplicate_should_fail(void)
{
    /* 传合法参数，使 inited 检查触发（非参数错误） */
    TEST_ASSERT_EQUAL(BP_ERR_STATE,
                      buffer_pool_init(&test_pool, test_pool_storage,
                                       TEST_BLOCK_SIZE, TEST_CAPACITY,
                                       test_pool_slots, test_pool_freestk, 4));
}

/* ================================================================
 *  alloc
 * ================================================================ */
void test_alloc_returns_buf_with_ref_one(void)
{
    buffer_pool_buf_t *buf = buffer_pool_alloc(&test_pool, 100);
    TEST_ASSERT_NOT_NULL(buf);
    TEST_ASSERT_EQUAL(1, buf->ref);
    TEST_ASSERT_EQUAL(TEST_CAPACITY - 1, buffer_pool_available(&test_pool));
    buffer_pool_unref(buf);
}

void test_alloc_exhausts_pool(void)
{
    buffer_pool_buf_t *b[TEST_CAPACITY];
    for (int i = 0; i < TEST_CAPACITY; i++) {
        b[i] = buffer_pool_alloc(&test_pool, 100);
        TEST_ASSERT_NOT_NULL(b[i]);
    }
    TEST_ASSERT_EQUAL(0, buffer_pool_available(&test_pool));
    /* 池空：take 仍返回 true（mock），但 freeTop=0 应返回 NULL 并归还名额 */
    buffer_pool_buf_t *extra = buffer_pool_alloc(&test_pool, 100);
    TEST_ASSERT_NULL(extra);
    for (int i = 0; i < TEST_CAPACITY; i++) buffer_pool_unref(b[i]);
}

void test_alloc_timeout_returns_null(void)
{
    osal_sem_take_fake.return_val = false;   /* 模拟超时 */
    TEST_ASSERT_NULL(buffer_pool_alloc(&test_pool, 100));
}

void test_alloc_null_pool_returns_null(void)
{
    TEST_ASSERT_NULL(buffer_pool_alloc(NULL, 100));
}

/* ================================================================
 *  ref / unref
 * ================================================================ */
void test_ref_increments_count(void)
{
    buffer_pool_buf_t *buf = buffer_pool_alloc(&test_pool, 100);
    TEST_ASSERT_EQUAL(2, buffer_pool_ref(buf));
    TEST_ASSERT_EQUAL(3, buffer_pool_ref(buf));
    buffer_pool_unref(buf);
    buffer_pool_unref(buf);
    buffer_pool_unref(buf);
}

void test_unref_to_zero_recycles(void)
{
    TEST_ASSERT_EQUAL(TEST_CAPACITY, buffer_pool_available(&test_pool));
    buffer_pool_buf_t *buf = buffer_pool_alloc(&test_pool, 100);
    TEST_ASSERT_EQUAL(TEST_CAPACITY - 1, buffer_pool_available(&test_pool));
    TEST_ASSERT_EQUAL(0, buffer_pool_unref(buf));   /* 归零回收 */
    TEST_ASSERT_EQUAL(TEST_CAPACITY, buffer_pool_available(&test_pool));  /* 恢复 */
}

void test_multi_consumer_sharing(void)
{
    /* 生产者 alloc，两个消费者各 ref，各自 unref，最后生产者 unref 回收 */
    buffer_pool_buf_t *buf = buffer_pool_alloc(&test_pool, 100);
    TEST_ASSERT_EQUAL(1, buf->ref);

    buffer_pool_ref(buf);   /* UI 接管 */
    buffer_pool_ref(buf);   /* face 接管 */
    TEST_ASSERT_EQUAL(3, buf->ref);

    TEST_ASSERT_EQUAL(2, buffer_pool_unref(buf));   /* UI 释放 */
    TEST_ASSERT_EQUAL(1, buffer_pool_unref(buf));   /* face 释放 */
    TEST_ASSERT_EQUAL(0, buffer_pool_unref(buf));   /* 生产者释放，回收 */
    TEST_ASSERT_EQUAL(TEST_CAPACITY, buffer_pool_available(&test_pool));
}

void test_unref_null_returns_invalid(void)
{
    TEST_ASSERT_EQUAL(0xFFFFFFFFu, buffer_pool_unref(NULL));
}

void test_ref_null_returns_zero(void)
{
    TEST_ASSERT_EQUAL(0, buffer_pool_ref(NULL));
}

/* ================================================================
 *  重复释放自洽
 * ================================================================ */
void test_double_unref_stays_consistent(void)
{
    buffer_pool_buf_t *buf = buffer_pool_alloc(&test_pool, 100);
    TEST_ASSERT_EQUAL(0, buffer_pool_unref(buf));   /* 回收 */
    TEST_ASSERT_EQUAL(0, buffer_pool_unref(buf));   /* 重复释放，不回收 */
    /* 可用块数应仍为满（不会因重复释放多增） */
    TEST_ASSERT_EQUAL(TEST_CAPACITY, buffer_pool_available(&test_pool));
}

/* ================================================================
 *  测试运行入口
 * ================================================================ */
int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_pool_init_maps_slots_and_fills_free_stack);
    RUN_TEST(test_pool_init_invalid_args_should_fail);
    RUN_TEST(test_pool_init_duplicate_should_fail);

    RUN_TEST(test_alloc_returns_buf_with_ref_one);
    RUN_TEST(test_alloc_exhausts_pool);
    RUN_TEST(test_alloc_timeout_returns_null);
    RUN_TEST(test_alloc_null_pool_returns_null);

    RUN_TEST(test_ref_increments_count);
    RUN_TEST(test_unref_to_zero_recycles);
    RUN_TEST(test_multi_consumer_sharing);
    RUN_TEST(test_unref_null_returns_invalid);
    RUN_TEST(test_ref_null_returns_zero);

    RUN_TEST(test_double_unref_stays_consistent);

    return UNITY_END();
}

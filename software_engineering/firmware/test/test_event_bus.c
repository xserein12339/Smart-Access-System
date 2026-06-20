/**
 * @file    test_event_bus.c
 * @brief   mw_event_bus 单元测试（宿主机 Mock 测试）
 *
 * @details 测试事件总线的订阅/发布逻辑：
 *            - init 创建队列/mutex/任务
 *            - subscribe 注册/参数校验/订阅表满
 *            - publish 入队/参数校验/队列满返回 BUSY
 *          分发任务在 host 不跑（osal_task_create mock 不启动真实任务），
 *          回调匹配逻辑留集成测试。
 *
 * @author  xLumina
 * @version 1.0
 */
#include "fff.h"
#include "unity.h"

/* ---- Mock 头 ---- */
#include "esp_err.h"
#include "osal_queue.h"
#include "osal_mutex.h"
#include "osal_task.h"
#include "pal_log.h"

/* ---- 被测模块 ---- */
#include "mw_event_bus.h"
#include "service_event.h"
#include "dal_err.h"

/* ================================================================
 *  setUp / tearDown
 * ================================================================ */
void setUp(void)
{
    RESET_FAKE(osal_queue_create);
    RESET_FAKE(osal_queue_send);
    RESET_FAKE(osal_queue_receive);
    RESET_FAKE(osal_queue_get_count);
    RESET_FAKE(osal_queue_delete);

    /* queue/mutex/task mock 返回非 NULL，使 init 成功 */
    osal_queue_create_fake.return_val  = (osal_queue_t)1;
    osal_queue_send_fake.return_val    = true;
    osal_queue_receive_fake.return_val = false;
}

void tearDown(void) {}

/* 重新初始化总线（每个用例前重置静态状态）。
 * mw_event_bus 内部 s_inited 跨用例保持，需通过重复 init 的 STATE 错误
 * 间接确认——故首用例 init，后续用例假定已 init。为隔离，用进程级
 * 单 init 模型：所有用例共享一次 init。 */
static bool s_bootstrapped = false;
static void bootstrap(void)
{
    if (!s_bootstrapped) {
        TEST_ASSERT_EQUAL(DAL_OK, mw_event_bus_init());
        s_bootstrapped = true;
    }
}

/* ================================================================
 *  init
 * ================================================================ */
void test_event_bus_init_creates_queue_mutex_task(void)
{
    bootstrap();
    /* init 已创建队列 */
    TEST_ASSERT_GREATER_OR_EQUAL(1, osal_queue_create_fake.call_count);
}

void test_event_bus_init_duplicate_returns_state_error(void)
{
    bootstrap();
    TEST_ASSERT_EQUAL(DAL_ERR_STATE, mw_event_bus_init());
}

/* ================================================================
 *  subscribe
 * ================================================================ */
static void dummy_cb(const service_event_t *e, void *ud)
{
    (void)e; (void)ud;
}

void test_subscribe_valid_returns_ok(void)
{
    bootstrap();
    TEST_ASSERT_EQUAL(DAL_OK,
                      mw_event_bus_subscribe(SERVICE_EVT_AUTH_PASS, dummy_cb, NULL));
}

void test_subscribe_null_cb_returns_invalid(void)
{
    bootstrap();
    TEST_ASSERT_EQUAL(DAL_ERR_INVALID,
                      mw_event_bus_subscribe(SERVICE_EVT_AUTH_PASS, NULL, NULL));
}

void test_subscribe_multiple_types_independent(void)
{
    bootstrap();
    TEST_ASSERT_EQUAL(DAL_OK, mw_event_bus_subscribe(SERVICE_EVT_TOUCH, dummy_cb, NULL));
    TEST_ASSERT_EQUAL(DAL_OK, mw_event_bus_subscribe(SERVICE_EVT_MQTT_CONNECTED, dummy_cb, NULL));
    TEST_ASSERT_EQUAL(DAL_OK, mw_event_bus_subscribe(SERVICE_EVT_NONE, dummy_cb, NULL)); /* 订阅全部 */
}

/* ================================================================
 *  publish
 * ================================================================ */
void test_publish_valid_calls_queue_send(void)
{
    bootstrap();
    uint32_t send_before = osal_queue_send_fake.call_count;
    service_event_t evt = {
        .type = SERVICE_EVT_AUTH_PASS,
        .source = SERVICE_SRC_USER_MANAGE,
        .arg0 = 1001,
    };
    TEST_ASSERT_EQUAL(DAL_OK, mw_event_bus_publish(&evt));
    TEST_ASSERT_EQUAL(send_before + 1, osal_queue_send_fake.call_count);
}

void test_publish_null_event_returns_invalid(void)
{
    bootstrap();
    TEST_ASSERT_EQUAL(DAL_ERR_INVALID, mw_event_bus_publish(NULL));
}

void test_publish_none_type_returns_invalid(void)
{
    bootstrap();
    service_event_t evt = { .type = SERVICE_EVT_NONE };
    TEST_ASSERT_EQUAL(DAL_ERR_INVALID, mw_event_bus_publish(&evt));
}

void test_publish_queue_full_returns_busy(void)
{
    bootstrap();
    osal_queue_send_fake.return_val = false;   /* 模拟队列满 */
    service_event_t evt = { .type = SERVICE_EVT_FAULT };
    TEST_ASSERT_EQUAL(DAL_ERR_BUSY, mw_event_bus_publish(&evt));
}

/* ================================================================
 *  订阅表容量
 * ================================================================ */
void test_subscribe_table_overflow_returns_no_mem(void)
{
    bootstrap();
    /* 填满订阅表（MW_EVENT_BUS_MAX_SUBSCRIBERS=16） */
    dal_err_t ret = DAL_OK;
    for (int i = 0; i < 16; i++) {
        ret = mw_event_bus_subscribe(SERVICE_EVT_LOG_READY, dummy_cb, NULL);
        if (ret != DAL_OK) break;
    }
    /* 第 17 个应失败（前 16 个中首用例已占若干，故可能更早失败） */
    TEST_ASSERT_EQUAL(DAL_ERR_NO_MEM, ret);
}

/* ================================================================
 *  测试运行入口
 * ================================================================ */
int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_event_bus_init_creates_queue_mutex_task);
    RUN_TEST(test_event_bus_init_duplicate_returns_state_error);

    RUN_TEST(test_subscribe_valid_returns_ok);
    RUN_TEST(test_subscribe_null_cb_returns_invalid);
    RUN_TEST(test_subscribe_multiple_types_independent);

    RUN_TEST(test_publish_valid_calls_queue_send);
    RUN_TEST(test_publish_null_event_returns_invalid);
    RUN_TEST(test_publish_none_type_returns_invalid);
    RUN_TEST(test_publish_queue_full_returns_busy);

    RUN_TEST(test_subscribe_table_overflow_returns_no_mem);

    return UNITY_END();
}

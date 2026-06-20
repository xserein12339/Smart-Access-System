/**
 * @file    test_service_camera.c
 * @brief   service_camera 单元测试（宿主机 Mock 测试）
 *
 * @details 测试摄像服务的帧分发逻辑（适配 dal_camera 回调模型）：
 *            - init 创建缓冲池
 *            - start_stream 注册帧回调
 *            - 帧回调：alloc 池帧 + memcpy + return_frame + 分发订阅通道
 *            - 队列满丢帧计数
 *            - 多通道共享引用计数
 *          dal_camera ops 用自写 fake（start_stream 保存回调供测试手动触发）；
 *          buffer_pool 真实（mock osal_sem/mutex）；osal_queue FFF fake。
 *
 * @author  xLumina
 * @version 1.0
 */
#include "fff.h"
#include "unity.h"

/* ---- Mock 头 ---- */
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "osal_mutex.h"
#include "osal_semaphore.h"
#include "osal_queue.h"
#include "osal_task.h"
#include "pal_log.h"
#include "mw_event_bus.h"
#include "osal_timer.h"

/* ---- 被测模块 ---- */
#include "service_camera.h"
#include "dal_camera_interface.h"
#include "dal_err.h"
#include "buffer_pool_manager.h"

#include <string.h>

/* ================================================================
 *  dal_camera fake ops（自写，start_stream 保存回调）
 * ================================================================ */
static dal_camera_frame_cb_t s_frame_cb = NULL;
static void                 *s_frame_cb_ud = NULL;
static int                   s_return_frame_calls = 0;

static dal_err_t fake_init(void *ctx, const dal_camera_config_t *cfg) { (void)ctx; (void)cfg; return DAL_OK; }
static dal_err_t fake_start_stream(void *ctx, dal_camera_frame_cb_t cb, void *ud)
{
    (void)ctx;
    s_frame_cb = cb;
    s_frame_cb_ud = ud;
    return DAL_OK;
}
static dal_err_t fake_stop_stream(void *ctx) { (void)ctx; s_frame_cb = NULL; return DAL_OK; }
static dal_err_t fake_return_frame(void *ctx, const dal_camera_frame_t *frame)
{
    (void)ctx; (void)frame;
    s_return_frame_calls++;
    return DAL_OK;
}
static dal_err_t fake_deinit(void *ctx) { (void)ctx; return DAL_OK; }

static const dal_camera_ops_t s_fake_ops = {
    .init         = fake_init,
    .start_stream = fake_start_stream,
    .stop_stream  = fake_stop_stream,
    .return_frame = fake_return_frame,
    .deinit       = fake_deinit,
};

/* ================================================================
 *  setUp / tearDown
 * ================================================================ */
#define SC_FRAME_BYTES  64
#define SC_POOL_CAP     2

/* 保存离开定时器回调，供测试模拟超时 */
static osal_timer_cb_t s_leave_cb = NULL;
static void            *s_leave_cb_arg = NULL;
static int              s_leave_start_count = 0;
static int              s_leave_stop_count = 0;

static osal_timer_t leave_timer_create(const char *name, osal_timer_cb_t cb,
                                       void *arg, uint32_t delay)
{
    (void)name; (void)delay;
    s_leave_cb = cb;
    s_leave_cb_arg = arg;
    return (osal_timer_t)1;
}
static bool leave_timer_start(osal_timer_t t) { (void)t; s_leave_start_count++; return true; }
static bool leave_timer_stop(osal_timer_t t)  { (void)t; s_leave_stop_count++; return true; }

void setUp(void)
{
    RESET_FAKE(osal_sem_create_counting);
    RESET_FAKE(osal_sem_take);
    RESET_FAKE(osal_sem_give);
    RESET_FAKE(osal_sem_delete);
    RESET_FAKE(osal_queue_create);
    RESET_FAKE(osal_queue_send);
    RESET_FAKE(osal_queue_receive);
    RESET_FAKE(osal_queue_delete);
    RESET_FAKE(osal_timer_create_one_shot);
    RESET_FAKE(osal_timer_start);
    RESET_FAKE(osal_timer_stop);
    RESET_FAKE(osal_timer_delete);

    osal_sem_create_counting_fake.return_val = (osal_sem_t)1;
    osal_sem_take_fake.return_val            = true;
    osal_queue_send_fake.return_val          = true;
    osal_timer_create_one_shot_fake.custom_fake = leave_timer_create;
    osal_timer_start_fake.custom_fake = leave_timer_start;
    osal_timer_stop_fake.custom_fake  = leave_timer_stop;

    s_frame_cb = NULL;
    s_frame_cb_ud = NULL;
    s_return_frame_calls = 0;
    s_leave_cb = NULL;
    s_leave_cb_arg = NULL;
    s_leave_start_count = 0;
    s_leave_stop_count = 0;
}

void tearDown(void)
{
    service_camera_deinit();
}

/** 触发一帧回调（模拟 dal_camera 推帧） */
static void emit_frame(const uint8_t *data, size_t len, uint16_t w, uint16_t h)
{
    dal_camera_frame_t f = {
        .buf = (void *)data,
        .len = len,
        .width = w,
        .height = h,
        .format = DAL_CAMERA_FMT_RGB565,
        .timestamp_us = 12345,
        .frame_handle = (void *)1,
    };
    TEST_ASSERT_NOT_NULL(s_frame_cb);
    s_frame_cb(s_frame_cb_ud, &f);
}

/* ================================================================
 *  init
 * ================================================================ */
void test_sc_init_null_cfg_returns_invalid(void)
{
    TEST_ASSERT_EQUAL(DAL_ERR_INVALID, service_camera_init(NULL));
}

void test_sc_init_null_ops_returns_invalid(void)
{
    service_camera_config_t cfg = { .camera_ops = NULL };
    TEST_ASSERT_EQUAL(DAL_ERR_INVALID, service_camera_init(&cfg));
}

void test_sc_init_creates_pool(void)
{
    service_camera_config_t cfg = {
        .camera_ops = &s_fake_ops,
        .frame_bytes = SC_FRAME_BYTES,
        .pool_capacity = SC_POOL_CAP,
    };
    TEST_ASSERT_EQUAL(DAL_OK, service_camera_init(&cfg));
}

void test_sc_init_duplicate_returns_state(void)
{
    service_camera_config_t cfg = {
        .camera_ops = &s_fake_ops,
        .frame_bytes = SC_FRAME_BYTES,
        .pool_capacity = SC_POOL_CAP,
    };
    service_camera_init(&cfg);
    TEST_ASSERT_EQUAL(DAL_ERR_STATE, service_camera_init(&cfg));
}

/* ================================================================
 *  start / stop
 * ================================================================ */
void test_sc_start_registers_frame_callback(void)
{
    service_camera_config_t cfg = {
        .camera_ops = &s_fake_ops, .frame_bytes = SC_FRAME_BYTES, .pool_capacity = SC_POOL_CAP,
    };
    service_camera_init(&cfg);
    TEST_ASSERT_NULL(s_frame_cb);
    TEST_ASSERT_EQUAL(DAL_OK, service_camera_start());
    TEST_ASSERT_NOT_NULL(s_frame_cb);
}

void test_sc_stop_clears_callback(void)
{
    service_camera_config_t cfg = {
        .camera_ops = &s_fake_ops, .frame_bytes = SC_FRAME_BYTES, .pool_capacity = SC_POOL_CAP,
    };
    service_camera_init(&cfg);
    service_camera_start();
    service_camera_stop();
    TEST_ASSERT_NULL(s_frame_cb);
}

/* ================================================================
 *  帧分发
 * ================================================================ */
void test_frame_callback_returns_dal_frame_immediately(void)
{
    service_camera_config_t cfg = {
        .camera_ops = &s_fake_ops, .frame_bytes = SC_FRAME_BYTES, .pool_capacity = SC_POOL_CAP,
    };
    service_camera_init(&cfg);
    service_camera_start();

    uint8_t data[SC_FRAME_BYTES] = {0};
    int before = s_return_frame_calls;
    emit_frame(data, sizeof(data), 8, 4);
    /* 帧回调应立即归还 DAL 帧（避免占用 DMA buffer） */
    TEST_ASSERT_EQUAL(before + 1, s_return_frame_calls);
}

void test_frame_callback_with_no_subscribers_recycles_pool_buf(void)
{
    service_camera_config_t cfg = {
        .camera_ops = &s_fake_ops, .frame_bytes = SC_FRAME_BYTES, .pool_capacity = SC_POOL_CAP,
    };
    service_camera_init(&cfg);
    service_camera_start();

    uint8_t data[SC_FRAME_BYTES] = {0};
    /* 连续发多帧（超过池容量），无订阅者应每帧立即回收，不阻塞 */
    for (int i = 0; i < SC_POOL_CAP * 3; i++) {
        emit_frame(data, sizeof(data), 8, 4);
    }
    /* 全部成功（池帧每次回收） */
    service_camera_status_t st;
    service_camera_get_status(&st);
    TEST_ASSERT_EQUAL(SC_POOL_CAP * 3, st.captured_count);
}

void test_frame_callback_distributes_to_subscribed_channel(void)
{
    service_camera_config_t cfg = {
        .camera_ops = &s_fake_ops, .frame_bytes = SC_FRAME_BYTES, .pool_capacity = SC_POOL_CAP,
    };
    service_camera_init(&cfg);
    /* 订阅 UI 通道（用 fake 队列句柄） */
    osal_queue_t q = (osal_queue_t)1;
    TEST_ASSERT_EQUAL(DAL_OK, service_camera_subscribe(SERVICE_CAMERA_CH_UI, q));
    service_camera_start();

    uint8_t data[SC_FRAME_BYTES] = {0};
    uint32_t send_before = osal_queue_send_fake.call_count;
    emit_frame(data, sizeof(data), 8, 4);
    /* 应向 UI 队列发送一帧消息 */
    TEST_ASSERT_EQUAL(send_before + 1, osal_queue_send_fake.call_count);
}

void test_frame_callback_queue_full_drops_and_counts(void)
{
    service_camera_config_t cfg = {
        .camera_ops = &s_fake_ops, .frame_bytes = SC_FRAME_BYTES, .pool_capacity = SC_POOL_CAP,
    };
    service_camera_init(&cfg);
    osal_queue_t q = (osal_queue_t)1;
    service_camera_subscribe(SERVICE_CAMERA_CH_UI, q);
    service_camera_start();

    osal_queue_send_fake.return_val = false;   /* 模拟队列满 */
    uint8_t data[SC_FRAME_BYTES] = {0};
    emit_frame(data, sizeof(data), 8, 4);

    service_camera_status_t st;
    service_camera_get_status(&st);
    TEST_ASSERT_GREATER_THAN(0, st.dropped_ui_count);
}

void test_frame_callback_two_channels_share_ref(void)
{
    service_camera_config_t cfg = {
        .camera_ops = &s_fake_ops, .frame_bytes = SC_FRAME_BYTES, .pool_capacity = SC_POOL_CAP,
    };
    service_camera_init(&cfg);
    service_camera_subscribe(SERVICE_CAMERA_CH_UI, (osal_queue_t)1);
    service_camera_subscribe(SERVICE_CAMERA_CH_FACE, (osal_queue_t)2);
    service_camera_start();

    uint8_t data[SC_FRAME_BYTES] = {0};
    uint32_t send_before = osal_queue_send_fake.call_count;
    emit_frame(data, sizeof(data), 8, 4);
    /* 两通道各发一次 */
    TEST_ASSERT_EQUAL(send_before + 2, osal_queue_send_fake.call_count);
}

/* ================================================================
 *  subscribe 校验
 * ================================================================ */
void test_subscribe_invalid_args_returns_invalid(void)
{
    service_camera_config_t cfg = {
        .camera_ops = &s_fake_ops, .frame_bytes = SC_FRAME_BYTES, .pool_capacity = SC_POOL_CAP,
    };
    service_camera_init(&cfg);
    TEST_ASSERT_EQUAL(DAL_ERR_INVALID, service_camera_subscribe(SERVICE_CAMERA_CH_MAX, (osal_queue_t)1));
    TEST_ASSERT_EQUAL(DAL_ERR_INVALID, service_camera_subscribe(SERVICE_CAMERA_CH_UI, NULL));
}

/* ================================================================
 *  status
 * ================================================================ */
void test_get_status_null_returns_invalid(void)
{
    TEST_ASSERT_EQUAL(DAL_ERR_INVALID, service_camera_get_status(NULL));
}

/* ================================================================
 *  PIR 状态机
 * ================================================================ */
static void sc_init_default(void)
{
    service_camera_config_t cfg = {
        .camera_ops = &s_fake_ops, .frame_bytes = SC_FRAME_BYTES, .pool_capacity = SC_POOL_CAP,
    };
    service_camera_init(&cfg);
}

void test_pir_motion_present_starts_capture(void)
{
    sc_init_default();
    TEST_ASSERT_EQUAL(SC_CAM_STATE_STOPPED, service_camera_get_state());
    TEST_ASSERT_NULL(s_frame_cb);   /* 未 start */
    TEST_ASSERT_EQUAL(DAL_OK, service_camera_on_pir_motion(true));
    TEST_ASSERT_EQUAL(SC_CAM_STATE_RUNNING, service_camera_get_state());
    TEST_ASSERT_NOT_NULL(s_frame_cb);   /* 已 start */
}

void test_pir_leave_starts_confirm_timer(void)
{
    sc_init_default();
    service_camera_on_pir_motion(true);   /* 到场启动 */
    int starts_before = s_leave_start_count;
    service_camera_on_pir_motion(false);  /* 离开启动定时器 */
    TEST_ASSERT_EQUAL(starts_before + 1, s_leave_start_count);
    /* 仍运行，定时器未触发前不停 */
    TEST_ASSERT_EQUAL(SC_CAM_STATE_RUNNING, service_camera_get_state());
}

void test_pir_leave_timeout_stops_capture(void)
{
    sc_init_default();
    service_camera_on_pir_motion(true);
    service_camera_on_pir_motion(false);  /* 启动离开定时器 */
    TEST_ASSERT_NOT_NULL(s_leave_cb);
    /* 模拟定时器超时触发 */
    s_leave_cb(s_leave_cb_arg);
    TEST_ASSERT_EQUAL(SC_CAM_STATE_STOPPED, service_camera_get_state());
    TEST_ASSERT_NULL(s_frame_cb);
}

void test_pir_reentry_cancels_leave_timer(void)
{
    sc_init_default();
    service_camera_on_pir_motion(true);
    service_camera_on_pir_motion(false);  /* 启动离开定时器 */
    int stops_before = s_leave_stop_count;
    service_camera_on_pir_motion(true);   /* 再次到场，取消定时器 */
    TEST_ASSERT_EQUAL(stops_before + 1, s_leave_stop_count);
    TEST_ASSERT_EQUAL(SC_CAM_STATE_RUNNING, service_camera_get_state());
}

void test_pir_motion_publishes_event(void)
{
    sc_init_default();
    /* mw_event_bus 为 mock 空操作，此处仅验证不崩溃且返回 OK */
    TEST_ASSERT_EQUAL(DAL_OK, service_camera_on_pir_motion(true));
    TEST_ASSERT_EQUAL(DAL_OK, service_camera_on_pir_motion(false));
}

void test_pir_get_state_after_stop(void)
{
    sc_init_default();
    service_camera_on_pir_motion(true);
    service_camera_on_pir_motion(false);
    s_leave_cb(s_leave_cb_arg);   /* 超时停 */
    TEST_ASSERT_EQUAL(SC_CAM_STATE_STOPPED, service_camera_get_state());
}

/* ================================================================
 *  测试运行入口
 * ================================================================ */
int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_sc_init_null_cfg_returns_invalid);
    RUN_TEST(test_sc_init_null_ops_returns_invalid);
    RUN_TEST(test_sc_init_creates_pool);
    RUN_TEST(test_sc_init_duplicate_returns_state);

    RUN_TEST(test_sc_start_registers_frame_callback);
    RUN_TEST(test_sc_stop_clears_callback);

    RUN_TEST(test_frame_callback_returns_dal_frame_immediately);
    RUN_TEST(test_frame_callback_with_no_subscribers_recycles_pool_buf);
    RUN_TEST(test_frame_callback_distributes_to_subscribed_channel);
    RUN_TEST(test_frame_callback_queue_full_drops_and_counts);
    RUN_TEST(test_frame_callback_two_channels_share_ref);

    RUN_TEST(test_subscribe_invalid_args_returns_invalid);
    RUN_TEST(test_get_status_null_returns_invalid);

    RUN_TEST(test_pir_motion_present_starts_capture);
    RUN_TEST(test_pir_leave_starts_confirm_timer);
    RUN_TEST(test_pir_leave_timeout_stops_capture);
    RUN_TEST(test_pir_reentry_cancels_leave_timer);
    RUN_TEST(test_pir_motion_publishes_event);
    RUN_TEST(test_pir_get_state_after_stop);

    return UNITY_END();
}

/**
 * @file    svc_ui.c
 * @brief   人机界面服务 — LVGL 移植 + 状态机 + 摄像头预览
 *
 * @details LVGL v9 显示驱动：通过 lv_display_set_color_format(RGB888)
 *          使 flush 回调直接输出 RGB888，匹配 TC358762 帧缓冲格式，
 *          零格式转换开销。
 *
 *          触摸输入：lv_indev_read_cb 从 g_q_touch_to_ui 接收 svc_touch
 *          服务的触摸事件，UI 不再直接依赖 DAL touch 接口。
 *
 *          摄像头预览：svc_camera_acquire_frame 取共享帧 → lv_image_dsc_t 更新
 *          → lv_timer_handler 渲染 → svc_camera_release_frame 归还引用。
 *
 *          状态机：
 *          IDLE → PREVIEW（摄像头启动时自动切换）
 *          PREVIEW → IDLE（无摄像头帧超时）
 *          PREVIEW → PERM_PASSWORD（触摸触发）
 *          PERM_PASSWORD → ADMIN（密码正确，默认 "123"，存 db_config "admin.pwd"）
 *          PERM_PASSWORD/ADMIN → PREVIEW（Back 按钮）
 *          ADMIN：注册/删除/日志/改密 模态浮层（直连 db_store）
 *
 * @author  xiamu
 * @version 1.0
 */

#include "svc_ui.h"
#include "svc_camera.h"
#include "svc_perm_manager.h"
#include "db_store.h"
#include "esp_heap_caps.h"
#include "face_engine.h"   /* FACE_MAX_BOXES / face_box_t，用于 detect 框叠加 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"

#define SVC_UI_TAG "SVC_UI"

/* ----- 显示参数（与 board_v1_config.h 一致）----- */
#define UI_DISP_W   800
#define UI_DISP_H   480

/* ----- 摄像头预览超时切回 IDLE（ms）----- */
#define PREVIEW_TIMEOUT_MS  3000

/* ================================================================
 *  淡雅配色方案（提取自设计图 + 用户偏好）
 *
 *  浅绿底色贯穿所有界面，按钮/状态栏用不同深浅绿区分层级。
 *  time.png 青绿渐变保留用做装饰元素。
 * ================================================================ */
#define CLR_BG            0xDAF6FF  /**< 浅青背景（time.png 亮部色） */
#define CLR_CARD          0xFFFFFF  /**< 卡片白（浮层/输入框） */
#define CLR_PRIMARY       0x81C784  /**< 中绿（主按钮） */
#define CLR_PRIMARY_DARK  0x2E7D32  /**< 深绿（顶栏/标题/强调） */
#define CLR_PRIMARY_LIGHT 0xB2EBF2  /**< 浅青（hover/装饰） */
#define CLR_TEAL          0x114757  /**< 深青（time.png 暗部色） */
#define CLR_TEXT          0x333333  /**< 深灰文字 */
#define CLR_TEXT_LIGHT    0x808080  /**< 中灰（提示/辅助文字） */
#define CLR_DANGER        0xE57373  /**< 淡红（删除按钮） */
#define CLR_BORDER        0x80DEEA  /**< 青边框（微深于底色） */

/* ----- 管理员密码 ----- */
#define ADMIN_PWD_KEY    "admin.pwd"   /**< db_config 键 */
#define ADMIN_PWD_DEF    "123"         /**< 默认密码 */
#define PWD_MAX_LEN      6             /**< 密码最大位数 */

/* ----- 人员列表查询上限（db_person_t 含 4KB 特征，限数量省内存）----- */
#define DEL_LIST_MAX     32
#define LOG_LIST_MAX     32

/** @brief UI 状态 */
typedef enum {
    UI_STA_IDLE          = 0,   /**< 待机 */
    UI_STA_PREVIEW       = 1,   /**< 摄像头预览 */
    UI_STA_PERM_PASSWORD = 2,   /**< 管理员密码输入 */
    UI_STA_ADMIN         = 3,   /**< 管理员菜单 */
    UI_STA_ENROLL        = 4,   /**< 人脸注册（预览+进度条） */
} ui_state_t;

/* ================================================================
 *  静态变量
 * ================================================================ */

static svc_ui_deps_t  s_deps;                            /**< 依赖注入副本 */
static TaskHandle_t    s_worker = NULL;                   /**< UI 任务句柄 */
static bool            s_inited = false;                  /**< 初始化标志 */

/* LVGL draw buffer 直接用 DPI panel 的 framebuffer（DIRECT 模式，双缓冲），
 * 不再单独分配——LVGL 渲染进 fb，flush_cb 触发 vsync swap 免撕裂。 */
static void *s_fb0 = NULL;   /**< DPI framebuffer 0 */
static void *s_fb1 = NULL;   /**< DPI framebuffer 1 */

/* LVGL 对象 */
static lv_display_t  *s_disp = NULL;                      /**< LVGL 显示句柄 */
static lv_indev_t    *s_indev = NULL;                     /**< LVGL 触摸输入句柄 */
static lv_obj_t      *s_scr_idle = NULL;                  /**< 待机屏 */
static lv_obj_t      *s_scr_preview = NULL;               /**< 预览屏 */
static lv_obj_t      *s_scr_password = NULL;              /**< 密码屏 */
static lv_obj_t      *s_scr_admin = NULL;                 /**< 管理员屏 */
static lv_obj_t      *s_scr_enroll = NULL;                /**< 注册屏 */
static lv_obj_t      *s_enroll_img = NULL;                /**< 圆形人脸预览 */
static lv_obj_t      *s_enroll_fb_label = NULL;           /**< 注册反馈文字 */
static lv_obj_t      *s_preview_img = NULL;               /**< 预览图像控件 */
static lv_obj_t      *s_recog_popup = NULL;               /**< 识别反馈弹框 */
static lv_obj_t      *s_recog_label = NULL;               /**< 识别反馈文字 */
static lv_timer_t    *s_recog_timer = NULL;               /**< 弹框自动隐藏定时器 */
static bool           s_recog_showing = false;            /**< 弹框是否显示中 */
static lv_obj_t      *s_clock_label = NULL;               /**< 预览屏时钟标签 */
static lv_timer_t    *s_clock_timer = NULL;               /**< 时钟更新定时器 */

/* 密码输入 */
static char           s_pwd_input[PWD_MAX_LEN + 1] = {0}; /**< 当前密码输入缓冲 */
static lv_obj_t      *s_pwd_label = NULL;                 /**< 密码掩码显示 */

/* 管理员模态浮层 */
static lv_obj_t      *s_modal = NULL;                     /**< 当前模态浮层（ADMIN 屏上 overlay） */
static db_person_t   *s_del_persons = NULL;               /**< 删除列表人员缓冲（PSRAM） */
static uint16_t       s_del_count = 0;                    /**< 删除列表人员数 */
static lv_obj_t      *s_del_search_ta = NULL;             /**< 删除搜索框 */
static lv_obj_t      *s_del_list = NULL;                  /**< 删除列表（按搜索过滤重建） */
static lv_obj_t      *s_chgpwd_label = NULL;              /**< 改密掩码显示 */

/* 摄像头预览帧（共享 framebuffer，计数式 acquire/release） */
static svc_frame_t       s_preview_fb = {0};             /**< 当前预览帧（持引用，LVGL 异步引用须保活） */
static uint32_t          s_last_cam_seq = 0;             /**< 上次消费的 camera 帧序号 */
static TickType_t        s_last_frame_tick = 0;          /**< 上次收到帧的时间 */

/* 人脸检测框叠加（预览屏上的角标容器，8 段/框） */
#define CORNER_LEN  20   /**< 角标线段长度 */
#define CORNER_W    2    /**< 角标线段宽度 */
static lv_obj_t *s_face_boxes[FACE_MAX_BOXES] = {0};  /**< 框容器对象数组（删容器自动清子段） */

/* ================================================================
 *  LVGL 显示刷新回调
 * ================================================================ */

/**
 * @brief LVGL flush 回调 — DIRECT 模式，仅最后一帧 flush 时 swap framebuffer
 *
 * @param disp   LVGL 显示句柄
 * @param area   脏区域
 * @param px_map draw buffer（= DPI framebuffer 之一，LVGL 已渲染进去）
 *
 * @note DIRECT 双缓冲：LVGL 每个脏区域调一次 flush_cb，但只在 last 时
 *       交换 buf_act。故仅 last 调 draw_bitmap（传 fb 指针 → DPI panel
 *       cache writeback + vsync swap），非 last 跳过（避免每块 swap 花屏）。
 *       传全屏区域确保整个 fb 的脏区域都 writeback 后再 swap。
 */
static void display_flush_cb(lv_display_t *disp, const lv_area_t *area,
                             uint8_t *px_map)
{
    (void)area;
    if (lv_display_flush_is_last(disp)) {
        s_deps.disp_ops->draw_bitmap(s_deps.disp_ctx,
                                      0, 0, UI_DISP_W, UI_DISP_H, px_map);
    }
    lv_display_flush_ready(disp);
}

/* ================================================================
 *  LVGL 触摸读取回调
 * ================================================================ */

/**
 * @brief LVGL indev read_cb — 从触摸队列读取坐标
 *
 * @note 由 LVGL timer 周期调用（~33ms）。非阻塞读取 g_q_touch_to_ui，
 *       svc_touch 任务以 20ms 周期向队列推送触摸事件。
 *       坐标已由 BSP 校准至显示屏坐标系 (0~799, 0~479)。
 */
static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;

    /* 排空队列，保留最新事件；无事件时保持上次状态（按住期间不会误判释放）。
     * 旧实现队列空时返回 RELEASED，导致按住中 LVGL 误触发/不触发 CLICKED。 */
    static lv_indev_state_t s_last_state = LV_INDEV_STATE_RELEASED;
    static int32_t s_last_x = 0;
    static int32_t s_last_y = 0;
    static uint32_t s_log_cnt = 0;

    touch_msg_t msg;
    while (xQueueReceive(g_q_touch_to_ui, &msg, 0) == pdTRUE) {
        s_last_x = (int32_t)msg.point.x;
        s_last_y = (int32_t)msg.point.y;
        s_last_state = (msg.point.event == DAL_TOUCH_EVENT_UP)
                       ? LV_INDEV_STATE_RELEASED
                       : LV_INDEV_STATE_PRESSED;
        if (s_log_cnt < 5) {
            s_log_cnt++;
            ESP_LOGI(SVC_UI_TAG, "indev[%u]: ev=%d x=%ld y=%ld",
                     (unsigned)s_log_cnt, (int)msg.point.event,
                     (long)s_last_x, (long)s_last_y);
        }
    }
    data->point.x = s_last_x;
    data->point.y = s_last_y;
    data->state   = s_last_state;
}

/* ================================================================
 *  前向声明
 * ================================================================ */
static void switch_screen(ui_state_t new_state);
static void clock_timer_cb(lv_timer_t *timer);
static void preview_screen_click_cb(lv_event_t *e);
static void pwd_keypad_cb(lv_event_t *e);
static void pwd_back_cb(lv_event_t *e);
static void admin_back_cb(lv_event_t *e);
static void admin_menu_cb(lv_event_t *e);
static void close_modal(void);
static void show_delete_modal(void);
static void show_logs_modal(void);
static void show_chgpwd_modal(void);
static void update_pwd_label(lv_obj_t *label);
static void rebuild_del_list(const char *query);
static void clear_face_boxes(void);
static void process_detect_boxes(void);
static void create_enroll_screen(void);
static void register_save_cb(lv_event_t *e);
static void ta_show_kb_cb(lv_event_t *e);
static void scr_hide_kb_cb(lv_event_t *e);
static void kb_stop_bubble_cb(lv_event_t *e);
static void recog_popup_hide_cb(lv_timer_t *t);

/* ================================================================
 *  屏幕构建
 * ================================================================ */

/**
 * @brief 创建待机屏幕
 *
 * 显示标题、设备 ID 和状态信息。
 */
static void create_idle_screen(void)
{
    s_scr_idle = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_idle, lv_color_hex(CLR_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_scr_idle, LV_OPA_COVER, LV_PART_MAIN);

    /* time.png 风格：顶部深青渐变装饰条（6px） */
    lv_obj_t *teal_bar = lv_obj_create(s_scr_idle);
    lv_obj_set_size(teal_bar, UI_DISP_W, 6);
    lv_obj_align(teal_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(teal_bar, lv_color_hex(CLR_TEAL), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(teal_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(teal_bar, 0, LV_PART_MAIN);

    /* 标题（深绿色） */
    lv_obj_t *title = lv_label_create(s_scr_idle);
    lv_label_set_text(title, "Face Access System");
    lv_obj_set_style_text_color(title, lv_color_hex(CLR_PRIMARY_DARK), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -60);

    /* 数字时钟（深绿色大字） */
    s_clock_label = lv_label_create(s_scr_idle);
    lv_obj_set_style_text_color(s_clock_label, lv_color_hex(CLR_TEAL), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_clock_label, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_align(s_clock_label, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(s_clock_label, "--:--:--");

    s_clock_timer = lv_timer_create(clock_timer_cb, 1000, NULL);

    /* 底部提示 */
    lv_obj_t *hint = lv_label_create(s_scr_idle);
    lv_label_set_text(hint, "Touch the screen to start");
    lv_obj_set_style_text_color(hint, lv_color_hex(CLR_TEXT_LIGHT), LV_PART_MAIN);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -40);

    /* time.png 风格：底部浅青装饰条 */
    lv_obj_t *bot_bar = lv_obj_create(s_scr_idle);
    lv_obj_set_size(bot_bar, UI_DISP_W, 3);
    lv_obj_align(bot_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(bot_bar, lv_color_hex(CLR_PRIMARY_LIGHT), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bot_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(bot_bar, 0, LV_PART_MAIN);
}

/**
 * @brief 创建预览屏幕
 *
 * 全屏显示摄像头实时画面。
 */
/** @brief 时钟定时器回调：每秒更新待机屏右下角时间 */
static void clock_timer_cb(lv_timer_t *timer)
{
    if (s_clock_label == NULL) return;
    time_t now = time(NULL);
    if (now <= 0) {
        lv_label_set_text(s_clock_label, "--:--:--");
        return;
    }
    struct tm t;
    localtime_r(&now, &t);
    lv_label_set_text_fmt(s_clock_label, "%02d:%02d:%02d",
                          t.tm_hour, t.tm_min, t.tm_sec);
    (void)timer;
}

static void create_preview_screen(void)
{
    s_scr_preview = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_preview, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_scr_preview, LV_OPA_COVER, LV_PART_MAIN);

    /* 预览图像控件（全屏） */
    s_preview_img = lv_image_create(s_scr_preview);
    lv_obj_set_size(s_preview_img, UI_DISP_W, UI_DISP_H);
    lv_obj_align(s_preview_img, LV_ALIGN_CENTER, 0, 0);

    /* 顶部绿色状态栏（半透明） */
    lv_obj_t *bar = lv_obj_create(s_scr_preview);
    lv_obj_set_size(bar, UI_DISP_W, 28);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(CLR_PRIMARY_DARK), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_border_width(bar, 0, LV_PART_MAIN);

    lv_obj_t *label = lv_label_create(bar);
    lv_label_set_text(label, "Live Preview");
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);

    /* 触摸预览屏 → 进入密码界面 */
    lv_obj_add_event_cb(s_scr_preview, preview_screen_click_cb, LV_EVENT_CLICKED, NULL);

    /* ---- 识别反馈弹框（底部居中、半透明、初始隐藏）---- */
    s_recog_popup = lv_obj_create(s_scr_preview);
    lv_obj_set_size(s_recog_popup, 400, 56);
    lv_obj_align(s_recog_popup, LV_ALIGN_BOTTOM_MID, 0, -40);
    lv_obj_set_style_bg_color(s_recog_popup, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_recog_popup, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_recog_popup, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_recog_popup, 10, LV_PART_MAIN);
    lv_obj_add_flag(s_recog_popup, LV_OBJ_FLAG_HIDDEN);

    s_recog_label = lv_label_create(s_recog_popup);
    lv_label_set_text(s_recog_label, "");
    lv_obj_set_style_text_color(s_recog_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_recog_label, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_center(s_recog_label);
}

/* ================================================================
 *  密码屏（数字键盘）
 * ================================================================ */

/** 键盘按钮 map：0-9 + '<'(退格) + 'OK'(确认) */
static const char *s_pwd_keypad_map[] = {
    "1", "2", "3", "\n",
    "4", "5", "6", "\n",
    "7", "8", "9", "\n",
    "<", "0", "OK", ""
};

/** 更新密码掩码显示：固定 6 位，已输 '*'，未输 '_'，空格分隔便于辨认 */
static void update_pwd_label(lv_obj_t *label)
{
    if (label == NULL) {
        return;
    }
    static char disp[PWD_MAX_LEN * 2 + 1];   /* 每位 "* " + nul */
    size_t len = strlen(s_pwd_input);
    if (len > PWD_MAX_LEN) {
        len = PWD_MAX_LEN;
    }
    for (size_t i = 0; i < PWD_MAX_LEN; i++) {
        disp[i * 2]     = (i < len) ? '*' : '_';
        disp[i * 2 + 1] = ' ';
    }
    disp[PWD_MAX_LEN * 2] = '\0';
    lv_label_set_text(label, disp);
}

static void create_password_screen(void)
{
    s_scr_password = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_password, lv_color_hex(CLR_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_scr_password, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(s_scr_password, LV_OBJ_FLAG_SCROLLABLE);

    /* 顶部绿色装饰条 */
    lv_obj_t *top_bar = lv_obj_create(s_scr_password);
    lv_obj_set_size(top_bar, UI_DISP_W, 4);
    lv_obj_align(top_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(top_bar, lv_color_hex(CLR_TEAL), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(top_bar, 0, LV_PART_MAIN);

    /* Back 按钮 */
    lv_obj_t *back = lv_button_create(s_scr_password);
    lv_obj_set_size(back, 80, 36);
    lv_obj_align(back, LV_ALIGN_TOP_LEFT, 12, 12);
    lv_obj_set_style_bg_color(back, lv_color_hex(CLR_BORDER), LV_PART_MAIN);
    lv_obj_t *bl = lv_label_create(back);
    lv_label_set_text(bl, "Back");
    lv_obj_set_style_text_color(bl, lv_color_hex(CLR_TEXT), LV_PART_MAIN);
    lv_obj_center(bl);
    lv_obj_add_event_cb(back, pwd_back_cb, LV_EVENT_CLICKED, NULL);

    /* 标题 */
    lv_obj_t *title = lv_label_create(s_scr_password);
    lv_label_set_text(title, "Admin Password");
    lv_obj_set_style_text_color(title, lv_color_hex(CLR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);

    /* 密码掩码显示 */
    s_pwd_label = lv_label_create(s_scr_password);
    lv_obj_set_style_text_color(s_pwd_label, lv_color_hex(CLR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_pwd_label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_align(s_pwd_label, LV_ALIGN_TOP_MID, 0, 100);
    update_pwd_label(s_pwd_label);

    /* 数字键盘 */
    lv_obj_t *kb = lv_btnmatrix_create(s_scr_password);
    lv_btnmatrix_set_map(kb, s_pwd_keypad_map);
    lv_obj_set_size(kb, 360, 280);
    lv_obj_align(kb, LV_ALIGN_CENTER, 0, 50);
    lv_obj_set_style_bg_color(kb, lv_color_hex(CLR_BG), LV_PART_MAIN);
    /* 按键样式（通过 theme，这里简单设置） */
    lv_obj_add_event_cb(kb, pwd_keypad_cb, LV_EVENT_VALUE_CHANGED, NULL);
}

/** 密码键盘回调：数字追加 / '<' 退格 / 'OK' 校验 */
static void pwd_keypad_cb(lv_event_t *e)
{
    lv_obj_t *kb = lv_event_get_target(e);
    uint16_t id = lv_btnmatrix_get_selected_btn(kb);
    const char *txt = lv_btnmatrix_get_btn_text(kb, id);
    if (txt == NULL) {
        return;
    }

    if (strcmp(txt, "<") == 0) {
        size_t len = strlen(s_pwd_input);
        if (len > 0) {
            s_pwd_input[len - 1] = '\0';
        }
    } else if (strcmp(txt, "OK") == 0) {
        /* 校验密码：db_config "admin.pwd"，默认 "123" */
        char pwd[32];
        if (db_config_get_str(ADMIN_PWD_KEY, pwd, sizeof(pwd), ADMIN_PWD_DEF) != DAL_OK) {
            snprintf(pwd, sizeof(pwd), "%s", ADMIN_PWD_DEF);
        }
        if (strcmp(s_pwd_input, pwd) == 0) {
            s_pwd_input[0] = '\0';
            update_pwd_label(s_pwd_label);
            switch_screen(UI_STA_ADMIN);
        } else {
            s_pwd_input[0] = '\0';
            update_pwd_label(s_pwd_label);
            lv_obj_t *m = lv_msgbox_create(NULL);
            lv_msgbox_add_title(m, "Wrong password");
            lv_msgbox_add_text(m, "Please try again");
            lv_msgbox_add_close_button(m);
        }
        return;
    } else {
        /* 数字 */
        size_t len = strlen(s_pwd_input);
        if (len < PWD_MAX_LEN) {
            s_pwd_input[len] = txt[0];
            s_pwd_input[len + 1] = '\0';
        }
    }
    update_pwd_label(s_pwd_label);
}

/** 密码屏 Back → 回预览 */
static void pwd_back_cb(lv_event_t *e)
{
    (void)e;
    s_pwd_input[0] = '\0';
    update_pwd_label(s_pwd_label);
    switch_screen(UI_STA_PREVIEW);
}

/* ================================================================
 *  触摸事件回调
 * ================================================================ */

/** 预览屏触摸 → 进入密码界面 */
static void preview_screen_click_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(SVC_UI_TAG, "preview clicked → PERM_PASSWORD");
    switch_screen(UI_STA_PERM_PASSWORD);
}

/* ================================================================
 *  管理员屏 + 模态浮层
 * ================================================================ */

/** 创建管理员菜单屏 */
static void create_admin_screen(void)
{
    s_scr_admin = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_admin, lv_color_hex(CLR_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_scr_admin, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(s_scr_admin, LV_OBJ_FLAG_SCROLLABLE);

    /* 顶部绿色装饰条 */
    lv_obj_t *top_bar = lv_obj_create(s_scr_admin);
    lv_obj_set_size(top_bar, UI_DISP_W, 4);
    lv_obj_align(top_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(top_bar, lv_color_hex(CLR_TEAL), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(top_bar, 0, LV_PART_MAIN);

    /* Back 按钮 */
    lv_obj_t *back = lv_button_create(s_scr_admin);
    lv_obj_set_size(back, 80, 36);
    lv_obj_align(back, LV_ALIGN_TOP_LEFT, 12, 12);
    lv_obj_set_style_bg_color(back, lv_color_hex(CLR_BORDER), LV_PART_MAIN);
    lv_obj_t *bl = lv_label_create(back);
    lv_label_set_text(bl, "Back");
    lv_obj_set_style_text_color(bl, lv_color_hex(CLR_TEXT), LV_PART_MAIN);
    lv_obj_center(bl);
    lv_obj_add_event_cb(back, admin_back_cb, LV_EVENT_CLICKED, NULL);

    /* 标题 */
    lv_obj_t *title = lv_label_create(s_scr_admin);
    lv_label_set_text(title, "Admin");
    lv_obj_set_style_text_color(title, lv_color_hex(CLR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);

    /* 4 个菜单按钮（2×2 网格） */
    static const char *labels[4] = {"Register", "Delete", "Logs", "Change Pwd"};
    static const int   btn_colors[4] = {CLR_PRIMARY, CLR_DANGER, CLR_PRIMARY, CLR_PRIMARY};
    for (int i = 0; i < 4; i++) {
        lv_obj_t *btn = lv_button_create(s_scr_admin);
        lv_obj_set_size(btn, 240, 80);
        int col = i % 2;
        int row = i / 2;
        lv_obj_align(btn, LV_ALIGN_CENTER, col * 270 - 135, row * 100 - 40);
        lv_obj_set_style_bg_color(btn, lv_color_hex(btn_colors[i]), LV_PART_MAIN);
        lv_obj_set_style_radius(btn, 8, LV_PART_MAIN);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, labels[i]);
        lv_obj_set_style_text_color(lbl, lv_color_hex(CLR_CARD), LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_center(lbl);
        lv_obj_add_event_cb(btn, admin_menu_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
}

/* ================================================================
 *  注册屏（预览 + 进度条）
 * ================================================================ */

static void enroll_back_cb(lv_event_t *e)
{
    (void)e;
    svc_perm_manager_cancel_enroll();
    camera_cmdmsg_t cmd = { .cmd = CAM_CMD_PAUSE };
    xQueueSend(g_q_ui_to_cam, &cmd, 0);
    switch_screen(UI_STA_ADMIN);
}

/** @brief 注册成功后 1.5s 自动切回 ADMIN */
static void enroll_success_close_cb(lv_timer_t *t)
{
    (void)t;
    switch_screen(UI_STA_ADMIN);
}

/**
 * @brief 注册屏 — 左半圆脸预览 + 反馈 / 右半名字输入 + 按钮
 *
 * 布局（800×480）：
 *   顶部半透明横条 "人员注册"（与预览屏同风格）
 *   横条左下：Back 按钮
 *   左半：圆形人脸预览框 (280×280) + 下方注册反馈框
 *   右半：Name 标签 + 输入框 + Start/Cancel 按钮
 *   键盘：点击输入框时 LVGL 自动弹出
 */
/**
 * @brief 注册屏 — 左：圆脸+反馈 / 右：名字+按钮
 *
 *   x=0    60    350  400              780 800
 *   ┌──────┬──────┬────┬─────────────────┐ y=0
 *   │ ▓▓▓▓▓▓ Person Register ▓▓▓▓▓▓▓▓▓ │   top_bar 28px
 *   ├──────┴──────┴────┴─────────────────┤ y=28
 *   │ [Back]                             │   y=36
 *   │                                    │
 *   │   ╭────────╮      Name: ┌──────┐  │   y=90
 *   │  │  (Face) │             │(input)│ │   y=110
 *   │   ╰────────╯             └──────┘  │
 *   │  280×280         ┌──────────────┐  │   y=170
 *   │  y=90~370        │Start Register│  │   y=180
 *   │                  └──────────────┘  │
 *   │  ┌────────────┐  ┌──────────────┐ │   y=240
 *   │  │  feedback  │  │   Cancel     │ │   y=260
 *   │  └────────────┘  └──────────────┘ │
 *   └────────────────────────────────────┘ y=480
 */
static void create_enroll_screen(void)
{
    s_scr_enroll = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_enroll, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_scr_enroll, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(s_scr_enroll, LV_OBJ_FLAG_SCROLLABLE);

    /* ---- 顶部半透明横条 ---- */
    lv_obj_t *top_bar = lv_obj_create(s_scr_enroll);
    lv_obj_set_size(top_bar, UI_DISP_W, 28);
    lv_obj_set_pos(top_bar, 0, 0);
    lv_obj_set_style_bg_color(top_bar, lv_color_hex(CLR_PRIMARY_DARK), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_border_width(top_bar, 0, LV_PART_MAIN);

    lv_obj_t *bar_label = lv_label_create(top_bar);
    lv_label_set_text(bar_label, "Person Register");
    lv_obj_set_style_text_color(bar_label, lv_color_hex(CLR_CARD), LV_PART_MAIN);
    lv_obj_set_style_text_font(bar_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_center(bar_label);

    /* ---- Back 按钮 ---- */
    lv_obj_t *back = lv_button_create(s_scr_enroll);
    lv_obj_set_size(back, 70, 30);
    lv_obj_set_pos(back, 10, 36);
    lv_obj_set_style_bg_color(back, lv_color_hex(CLR_PRIMARY_DARK), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(back, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_radius(back, 6, LV_PART_MAIN);
    lv_obj_t *bl = lv_label_create(back);
    lv_label_set_text(bl, "Back");
    lv_obj_set_style_text_color(bl, lv_color_hex(CLR_CARD), LV_PART_MAIN);
    lv_obj_center(bl);
    lv_obj_add_event_cb(back, enroll_back_cb, LV_EVENT_CLICKED, NULL);

    /* ========== 左半：圆形人脸预览 ========== */
    /* 位置：x=60~340, y=90~370 (280×280), center at (200,230) */
    s_enroll_img = lv_image_create(s_scr_enroll);
    lv_obj_set_size(s_enroll_img, 280, 280);
    lv_obj_set_pos(s_enroll_img, 60, 90);
    lv_obj_set_style_radius(s_enroll_img, 140, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_enroll_img, 3, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_enroll_img, lv_color_hex(CLR_PRIMARY), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_enroll_img, lv_color_hex(0x222222), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_enroll_img, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_clip_corner(s_enroll_img, true, LV_PART_MAIN);

    /* 反馈框（圆形下方, x=60~430, y=378~438） */
    lv_obj_t *fb_box = lv_obj_create(s_scr_enroll);
    lv_obj_set_size(fb_box, 370, 60);
    lv_obj_set_pos(fb_box, 15, 378);
    lv_obj_set_style_bg_color(fb_box, lv_color_hex(CLR_CARD), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(fb_box, LV_OPA_20, LV_PART_MAIN);
    lv_obj_set_style_border_width(fb_box, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(fb_box, lv_color_hex(CLR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_radius(fb_box, 6, LV_PART_MAIN);
    lv_obj_clear_flag(fb_box, LV_OBJ_FLAG_SCROLLABLE);

    s_enroll_fb_label = lv_label_create(fb_box);
    lv_label_set_text(s_enroll_fb_label, "");
    lv_obj_set_style_text_color(s_enroll_fb_label, lv_color_hex(CLR_CARD), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_enroll_fb_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_center(s_enroll_fb_label);

    /* ========== 右半：名字 + 按钮 ========== */
    /* Name 标签 (x=400, y=90) */
    lv_obj_t *name_label = lv_label_create(s_scr_enroll);
    lv_label_set_text(name_label, "Name:");
    lv_obj_set_style_text_color(name_label, lv_color_hex(CLR_CARD), LV_PART_MAIN);
    lv_obj_set_style_text_font(name_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_pos(name_label, 400, 90);

    /* 姓名输入框 (x=400~730, y=110~152) */
    lv_obj_t *ta = lv_textarea_create(s_scr_enroll);
    lv_textarea_set_placeholder_text(ta, "Enter name");
    lv_textarea_set_max_length(ta, DB_PERSON_NAME_LEN - 1);
    lv_textarea_set_one_line(ta, true);
    lv_obj_set_size(ta, 340, 42);
    lv_obj_set_pos(ta, 400, 110);
    lv_obj_set_style_bg_color(ta, lv_color_hex(CLR_CARD), LV_PART_MAIN);
    lv_obj_set_style_border_color(ta, lv_color_hex(CLR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_width(ta, 1, LV_PART_MAIN);
    lv_obj_set_style_text_color(ta, lv_color_hex(CLR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_radius(ta, 6, LV_PART_MAIN);
    lv_obj_add_flag(ta, LV_OBJ_FLAG_CLICKABLE);  /* 确保可点击获取焦点 */

    /* 键盘：初始隐藏，点击输入框时手动弹出 */
    lv_obj_t *kb = lv_keyboard_create(s_scr_enroll);
    lv_keyboard_set_textarea(kb, ta);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);

    /* 点击输入框 → 显示键盘 */
    lv_obj_add_event_cb(ta, ta_show_kb_cb, LV_EVENT_CLICKED, kb);

    /* 键盘上的点击不要冒泡到屏幕（否则按键即隐藏） */
    lv_obj_add_event_cb(kb, kb_stop_bubble_cb, LV_EVENT_CLICKED, NULL);

    /* Start Register 按钮 (x=400~740, y=170~218) */
    lv_obj_t *start_btn = lv_button_create(s_scr_enroll);
    lv_obj_set_size(start_btn, 340, 48);
    lv_obj_set_pos(start_btn, 400, 170);
    lv_obj_set_style_bg_color(start_btn, lv_color_hex(CLR_PRIMARY), LV_PART_MAIN);
    lv_obj_set_style_radius(start_btn, 8, LV_PART_MAIN);
    lv_obj_t *start_lbl = lv_label_create(start_btn);
    lv_label_set_text(start_lbl, "Start Register");
    lv_obj_set_style_text_color(start_lbl, lv_color_hex(CLR_CARD), LV_PART_MAIN);
    lv_obj_set_style_text_font(start_lbl, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_center(start_lbl);
    lv_obj_add_event_cb(start_btn, register_save_cb, LV_EVENT_CLICKED, ta);

    /* 点击屏幕空白区域 → 隐藏键盘 */
    lv_obj_add_event_cb(s_scr_enroll, scr_hide_kb_cb, LV_EVENT_CLICKED, kb);
}

/** 管理员 Back → 回预览 */
static void admin_back_cb(lv_event_t *e)
{
    (void)e;
    close_modal();
    switch_screen(UI_STA_PREVIEW);
}

/** 管理员菜单按钮 → 打开对应模态 */
static void admin_menu_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    switch (idx) {
    case 0: switch_screen(UI_STA_ENROLL); break;
    case 1: show_delete_modal();   break;
    case 2: show_logs_modal();     break;
    case 3: show_chgpwd_modal();   break;
    default: break;
    }
}

/* ----- 模态浮层 ----- */

/** 在管理员屏上创建全屏模态浮层，返回内容容器 */
static lv_obj_t *show_modal(void)
{
    close_modal();
    s_modal = lv_obj_create(s_scr_admin);
    lv_obj_remove_style_all(s_modal);
    lv_obj_set_size(s_modal, UI_DISP_W, UI_DISP_H);
    lv_obj_align(s_modal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(s_modal, lv_color_hex(CLR_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_modal, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(s_modal, LV_OBJ_FLAG_SCROLLABLE);

    /* 顶部绿色装饰条 */
    lv_obj_t *bar = lv_obj_create(s_modal);
    lv_obj_set_size(bar, UI_DISP_W, 4);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(CLR_TEAL), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(bar, 0, LV_PART_MAIN);
    return s_modal;
}

/** 关闭模态浮层 */
static void close_modal(void)
{
    if (s_modal != NULL) {
        lv_obj_del(s_modal);
        s_modal = NULL;
    }
}

/** 模态 Cancel 按钮 → 关闭 */
static void modal_cancel_cb(lv_event_t *e)
{
    (void)e;
    close_modal();
}

/* ----- 注册人员 ----- */

/* ----- 注册人员（模态 UI；逻辑桩见 svc_perm_manager）----- */

static void register_save_cb(lv_event_t *e)
{
    lv_obj_t *ta = (lv_obj_t *)lv_event_get_user_data(e);
    const char *name = lv_textarea_get_text(ta);
    if (name == NULL || name[0] == '\0') {
        lv_obj_t *m = lv_msgbox_create(NULL);
        lv_msgbox_add_title(m, "Register");
        lv_msgbox_add_text(m, "Name empty");
        lv_msgbox_add_close_button(m);
        return;
    }

    ui_to_perm_t cmd = { .cmd = PERM_CMD_ADD_USER, .user_id = 0 };
    snprintf(cmd.user_name, sizeof(cmd.user_name), "%s", name);
    xQueueSend(g_q_ui_to_perm, &cmd, 0);
    /* 已在 ENROLL 屏上，直接发命令即可 */
}

/**
 * @brief 点击输入框 → 显示键盘，阻止事件冒泡以免被屏幕级隐藏
 */
static void ta_show_kb_cb(lv_event_t *e)
{
    lv_obj_t *kb = (lv_obj_t *)lv_event_get_user_data(e);
    lv_obj_remove_flag(kb, LV_OBJ_FLAG_HIDDEN);
    lv_event_stop_bubbling(e);  /* 阻止冒泡到屏幕 handler */
}

/**
 * @brief 点击屏幕空白区域 → 隐藏键盘
 */
static void scr_hide_kb_cb(lv_event_t *e)
{
    lv_obj_t *kb = (lv_obj_t *)lv_event_get_user_data(e);
    if (!lv_obj_has_flag(kb, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }
}

/**
 * @brief 阻止键盘上的点击冒泡到屏幕
 */
static void kb_stop_bubble_cb(lv_event_t *e)
{
    (void)e;
    lv_event_stop_bubbling(e);
}

/**
 * @brief 名字遮罩：保留首尾字符，中间用 * 替换
 */
static void mask_name(const char *src, char *dst, size_t dst_len)
{
    size_t len = strlen(src);
    if (len <= 2) {
        snprintf(dst, dst_len, "%s", src);
        return;
    }
    char tmp[32];
    tmp[0] = src[0];
    for (size_t i = 1; i < len - 1 && i < sizeof(tmp) - 2; i++) {
        tmp[i] = '*';
    }
    tmp[len - 1] = src[len - 1];
    tmp[len] = '\0';
    snprintf(dst, dst_len, "%s", tmp);
}

/**
 * @brief 识别弹框 2 秒后自动隐藏
 */
static void recog_popup_hide_cb(lv_timer_t *t)
{
    (void)t;
    if (s_recog_popup && s_recog_showing) {
        lv_obj_add_flag(s_recog_popup, LV_OBJ_FLAG_HIDDEN);
        s_recog_showing = false;
    }
    s_recog_timer = NULL;
}

/* ----- 删除人员 ----- */

/** 列表项点击 → db_person_del + 重新查询并按当前搜索词重建列表 */
static void del_item_cb(lv_event_t *e)
{
    uint32_t id = (uint32_t)(uintptr_t)lv_event_get_user_data(e);

    /* 发给 perm 执行删除 */
    ui_to_perm_t cmd = { .cmd = PERM_CMD_DELETE_USER, .user_id = id };
    xQueueSend(g_q_ui_to_perm, &cmd, 0);

    /* 重新查询并按当前搜索词重建列表 */
    s_del_count = (s_del_persons != NULL)
                  ? db_person_list(s_del_persons, DEL_LIST_MAX) : 0;
    const char *q = (s_del_search_ta != NULL) ? lv_textarea_get_text(s_del_search_ta) : NULL;
    rebuild_del_list(q);

    char msg[48];
    snprintf(msg, sizeof(msg), "删除请求已发送 ID=%lu", (unsigned long)id);
    lv_obj_t *m = lv_msgbox_create(NULL);
    lv_msgbox_add_title(m, "Delete");
    lv_msgbox_add_text(m, msg);
    lv_msgbox_add_close_button(m);
}

/** 按搜索词（姓名或 ID 子串）重建删除列表。query 为空则显示全部 */
static void rebuild_del_list(const char *query)
{
    if (s_del_list == NULL) {
        return;
    }
    lv_obj_clean(s_del_list);   /* 清空旧条目 */

    uint16_t shown = 0;
    for (uint16_t i = 0; i < s_del_count && shown < DEL_LIST_MAX; i++) {
        char id_str[16];
        snprintf(id_str, sizeof(id_str), "%lu", (unsigned long)s_del_persons[i].id);

        if (query != NULL && query[0] != '\0') {
            /* 姓名或 ID 含搜索词才显示 */
            if (strstr(s_del_persons[i].name, query) == NULL &&
                strstr(id_str, query) == NULL) {
                continue;
            }
        }

        char line[64];
        snprintf(line, sizeof(line), "ID:%lu  %s",
                 (unsigned long)s_del_persons[i].id, s_del_persons[i].name);
        lv_obj_t *btn = lv_list_add_button(s_del_list, NULL, line);
        lv_obj_add_event_cb(btn, del_item_cb, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)s_del_persons[i].id);
        shown++;
    }
    if (shown == 0) {
        lv_list_add_text(s_del_list, "(no match)");
    }
}

/** 搜索框内容变化 → 重建列表 */
static void del_search_cb(lv_event_t *e)
{
    (void)e;
    const char *q = (s_del_search_ta != NULL) ? lv_textarea_get_text(s_del_search_ta) : NULL;
    rebuild_del_list(q);
}

/** 删除模态：搜索框 + 过滤列表 + 键盘 + Close */
static void show_delete_modal(void)
{
    lv_obj_t *m = show_modal();

    lv_obj_t *title = lv_label_create(m);
    lv_label_set_text(title, "Delete Person");
    lv_obj_set_style_text_color(title, lv_color_hex(CLR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    s_del_search_ta = lv_textarea_create(m);
    lv_textarea_set_placeholder_text(s_del_search_ta, "Search name or ID");
    lv_textarea_set_one_line(s_del_search_ta, true);
    lv_obj_set_size(s_del_search_ta, 500, 35);
    lv_obj_align(s_del_search_ta, LV_ALIGN_TOP_MID, 0, 55);
    lv_obj_set_style_bg_color(s_del_search_ta, lv_color_hex(CLR_CARD), LV_PART_MAIN);
    lv_obj_set_style_border_color(s_del_search_ta, lv_color_hex(CLR_BORDER), LV_PART_MAIN);
    lv_obj_add_event_cb(s_del_search_ta, del_search_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_del_list = lv_list_create(m);
    lv_obj_set_size(s_del_list, 600, 150);
    lv_obj_align(s_del_list, LV_ALIGN_TOP_MID, 0, 100);

    if (s_del_persons == NULL) {
        s_del_persons = (db_person_t *)heap_caps_malloc(DEL_LIST_MAX * sizeof(db_person_t),
                                                        MALLOC_CAP_SPIRAM);
    }
    s_del_count = (s_del_persons != NULL)
                  ? db_person_list(s_del_persons, DEL_LIST_MAX) : 0;
    rebuild_del_list(NULL);

    lv_obj_t *kb = lv_keyboard_create(m);
    lv_keyboard_set_textarea(kb, s_del_search_ta);
    lv_obj_set_height(kb, 165);
    lv_obj_align(kb, LV_ALIGN_TOP_MID, 0, 260);

    /* Close 按钮 */
    lv_obj_t *close = lv_button_create(m);
    lv_obj_set_size(close, 120, 36);
    lv_obj_align(close, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_color(close, lv_color_hex(CLR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_radius(close, 6, LV_PART_MAIN);
    lv_obj_t *cl = lv_label_create(close);
    lv_label_set_text(cl, "Close");
    lv_obj_set_style_text_color(cl, lv_color_hex(CLR_TEXT), LV_PART_MAIN);
    lv_obj_center(cl);
    lv_obj_add_event_cb(close, modal_cancel_cb, LV_EVENT_CLICKED, NULL);
}

/* ----- 日志查看 ----- */

/** 日志模态：最近记录列表 + Close */
static void show_logs_modal(void)
{
    lv_obj_t *m = show_modal();

    lv_obj_t *title = lv_label_create(m);
    lv_label_set_text(title, "Recent Logs");
    lv_obj_set_style_text_color(title, lv_color_hex(CLR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    static db_record_t recs[LOG_LIST_MAX];
    uint16_t n = db_record_query_recent(recs, LOG_LIST_MAX);

    lv_obj_t *list = lv_list_create(m);
    lv_obj_set_size(list, 600, 320);
    lv_obj_align(list, LV_ALIGN_CENTER, 0, -10);
    if (n == 0) {
        lv_list_add_text(list, "(no records)");
    }
    for (uint16_t i = 0; i < n; i++) {
        char line[64];
        struct tm t;
        time_t ts = (time_t)recs[i].timestamp;
        char tbuf[16] = "--:--:--";
        if (ts > 0 && localtime_r(&ts, &t) != NULL) {
            snprintf(tbuf, sizeof(tbuf), "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
        }
        const char *res = (recs[i].result == 0) ? "PASS"
                        : (recs[i].result == 1) ? "REJECT" : "ERROR";
        snprintf(line, sizeof(line), "%s  ID:%lu  %s", tbuf,
                 (unsigned long)recs[i].person_id, res);
        lv_list_add_text(list, line);
    }

    lv_obj_t *close = lv_button_create(m);
    lv_obj_set_size(close, 120, 36);
    lv_obj_align(close, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_color(close, lv_color_hex(CLR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_radius(close, 6, LV_PART_MAIN);
    lv_obj_t *cl = lv_label_create(close);
    lv_label_set_text(cl, "Close");
    lv_obj_set_style_text_color(cl, lv_color_hex(CLR_TEXT), LV_PART_MAIN);
    lv_obj_center(cl);
    lv_obj_add_event_cb(close, modal_cancel_cb, LV_EVENT_CLICKED, NULL);
}

/* ----- 改密 ----- */

/** 改密键盘回调：数字 / '<' 退格 / 'OK' 保存 */
static void chgpwd_keypad_cb(lv_event_t *e)
{
    lv_obj_t *kb = lv_event_get_target(e);
    uint16_t id = lv_btnmatrix_get_selected_btn(kb);
    const char *txt = lv_btnmatrix_get_btn_text(kb, id);
    if (txt == NULL) {
        return;
    }

    if (strcmp(txt, "<") == 0) {
        size_t len = strlen(s_pwd_input);
        if (len > 0) {
            s_pwd_input[len - 1] = '\0';
        }
    } else if (strcmp(txt, "OK") == 0) {
        if (strlen(s_pwd_input) == 0) {
            return;
        }
        /* 发给 perm 执行改密 */
        ui_to_perm_t cmd = { .cmd = PERM_CMD_MODIFY_USER };
        snprintf(cmd.user_name, sizeof(cmd.user_name), "%s", s_pwd_input);
        xQueueSend(g_q_ui_to_perm, &cmd, 0);
        s_pwd_input[0] = '\0';
        update_pwd_label(s_chgpwd_label);
        close_modal();
        lv_obj_t *m = lv_msgbox_create(NULL);
        lv_msgbox_add_title(m, "Change Password");
        lv_msgbox_add_text(m, "密码更改中...");
        lv_msgbox_add_close_button(m);
        return;
    } else {
        size_t len = strlen(s_pwd_input);
        if (len < PWD_MAX_LEN) {
            s_pwd_input[len] = txt[0];
            s_pwd_input[len + 1] = '\0';
        }
    }
    update_pwd_label(s_chgpwd_label);
}

/** 改密模态：新密码显示 + 键盘 + Cancel */
static void show_chgpwd_modal(void)
{
    s_pwd_input[0] = '\0';
    lv_obj_t *m = show_modal();

    lv_obj_t *cancel = lv_button_create(m);
    lv_obj_set_size(cancel, 80, 36);
    lv_obj_align(cancel, LV_ALIGN_TOP_LEFT, 12, 12);
    lv_obj_set_style_bg_color(cancel, lv_color_hex(CLR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_radius(cancel, 6, LV_PART_MAIN);
    lv_obj_t *cl = lv_label_create(cancel);
    lv_label_set_text(cl, "Cancel");
    lv_obj_set_style_text_color(cl, lv_color_hex(CLR_TEXT), LV_PART_MAIN);
    lv_obj_center(cl);
    lv_obj_add_event_cb(cancel, modal_cancel_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *title = lv_label_create(m);
    lv_label_set_text(title, "New Password");
    lv_obj_set_style_text_color(title, lv_color_hex(CLR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);

    s_chgpwd_label = lv_label_create(m);
    lv_obj_set_style_text_color(s_chgpwd_label, lv_color_hex(CLR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_chgpwd_label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_align(s_chgpwd_label, LV_ALIGN_TOP_MID, 0, 100);
    update_pwd_label(s_chgpwd_label);

    lv_obj_t *kb = lv_btnmatrix_create(m);
    lv_btnmatrix_set_map(kb, s_pwd_keypad_map);
    lv_obj_set_size(kb, 360, 280);
    lv_obj_align(kb, LV_ALIGN_CENTER, 0, 50);
    lv_obj_add_event_cb(kb, chgpwd_keypad_cb, LV_EVENT_VALUE_CHANGED, NULL);
}

/* ================================================================
 *  摄像头帧处理
 * ================================================================ */

/**
 * @brief 消费并归还一帧（共享 framebuffer 模型）
 *
 * 从 camera 共享槽 acquire 最新帧（ref++），释放上一帧（ref--），
 * 更新预览图像源指针。持帧到下次 acquire 才释放——LVGL 异步引用
 * 须保活，否则 flush 读到已回收的 BSP 缓冲。
 */
static ui_state_t s_state = UI_STA_IDLE;  /**< 当前 UI 状态 */

static void process_camera_frame(void)
{
    /* IDLE/ENROLL 态：收首帧/注册预览；PREVIEW 态：更新预览画面。
     * ADMIN/密码屏跳过（camera 已 paused）。 */
    if (s_state != UI_STA_IDLE && s_state != UI_STA_PREVIEW && s_state != UI_STA_ENROLL) {
        return;
    }

    svc_frame_t f;
    if (!svc_camera_acquire_frame(&f, &s_last_cam_seq)) {
        return;   /* 无新帧 */
    }

    /* 释放上一帧（上轮 lv_timer_handler 已完成刷新） */
    if (s_preview_fb.buf != NULL) {
        svc_camera_release_frame(&s_preview_fb);
    }
    s_preview_fb = f;
    s_last_frame_tick = xTaskGetTickCount();

    /* 首帧到达 → 自动切 PREVIEW */
    if (s_state == UI_STA_IDLE) {
        ESP_LOGI(SVC_UI_TAG, "first frame → switch to PREVIEW");
        switch_screen(UI_STA_PREVIEW);
    }

    /* 更新 LVGL 图像源为新的帧数据（dsc 须持久化，栈变量会被 LVGL 延迟引用） */
    static lv_image_dsc_t s_img_dsc;
    s_img_dsc.header.magic  = LV_IMAGE_HEADER_MAGIC;
    s_img_dsc.header.cf     = LV_COLOR_FORMAT_RGB888;
    s_img_dsc.header.w      = f.width;
    s_img_dsc.header.h      = f.height;
    s_img_dsc.header.stride = (uint32_t)f.width * 3;
    s_img_dsc.data          = f.buf;
    s_img_dsc.data_size     = (uint32_t)f.width * f.height * 3;

    lv_obj_t *target_img = NULL;
    if (s_state == UI_STA_ENROLL) {
        target_img = s_enroll_img;
    } else if (s_state == UI_STA_PREVIEW || s_state == UI_STA_IDLE) {
        target_img = s_preview_img;
    }
    if (target_img != NULL && f.buf != NULL) {
        lv_image_set_src(target_img, &s_img_dsc);
        lv_obj_invalidate(target_img);
    }
}

/* ================================================================
 *  人脸检测框叠加
 * ================================================================ */

/** 清除所有人脸框对象 */
static void clear_face_boxes(void)
{
    for (uint32_t i = 0; i < FACE_MAX_BOXES; i++) {
        if (s_face_boxes[i] != NULL) {
            lv_obj_del(s_face_boxes[i]);
            s_face_boxes[i] = NULL;
        }
    }
}

/** 在人脸框角上画一段绿条（水平或垂直），作为容器子对象 */
static void box_corner_seg(lv_obj_t *parent, int16_t x, int16_t y,
                           int16_t w, int16_t h)
{
    lv_obj_t *seg = lv_obj_create(parent);
    lv_obj_remove_style_all(seg);
    lv_obj_set_size(seg, w, h);
    lv_obj_set_pos(seg, x, y);
    lv_obj_set_style_bg_color(seg, lv_color_hex(0x00FF00), 0);
    lv_obj_set_style_bg_opa(seg, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(seg, 0, 0);
    lv_obj_clear_flag(seg, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
}

/** 按 detect 结果刷新角标框（旧删，新画四角不闭合） */
static void update_face_boxes(const detect_to_ui_t *det)
{
    lv_obj_t *scr = (s_state == UI_STA_ENROLL && s_scr_enroll) ? s_scr_enroll : s_scr_preview;
    clear_face_boxes();
    if (det == NULL || scr == NULL) return;
    /* 帧坐标 → 显示坐标：帧 800×640→预览 800×480，Y 乘 480/640=0.75 */
    for (uint32_t i = 0; i < det->face_count && i < FACE_MAX_BOXES; i++) {
        const face_box_t *b = &det->boxes[i];
        int16_t w = (int16_t)b->w;
        int16_t h = (int16_t)((uint32_t)b->h * 480u / 640u);

        /* 容器：定位于人脸框区域，尺寸 w×h，为角标段的裁剪边界 */
        lv_obj_t *c = lv_obj_create(scr);
        lv_obj_remove_style_all(c);
        lv_obj_set_pos(c, (int16_t)b->x,
                       (int16_t)((uint32_t)b->y * 480u / 640u));
        lv_obj_set_size(c, w, h);
        lv_obj_clear_flag(c, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        s_face_boxes[i] = c;

        /* 段坐标在容器内（左上为 0,0） */
        /* 顶部左角 ┌ */
        box_corner_seg(c, 0, 0, CORNER_LEN, CORNER_W);
        box_corner_seg(c, 0, 0, CORNER_W, CORNER_LEN);
        /* 顶部右角 ┐ */
        box_corner_seg(c, w - CORNER_LEN, 0, CORNER_LEN, CORNER_W);
        box_corner_seg(c, w - CORNER_W, 0, CORNER_W, CORNER_LEN);
        /* 底部右角 ┘ */
        box_corner_seg(c, w - CORNER_LEN, h - CORNER_W, CORNER_LEN, CORNER_W);
        box_corner_seg(c, w - CORNER_W, h - CORNER_LEN, CORNER_W, CORNER_LEN);
        /* 底部左角 └ */
        box_corner_seg(c, 0, h - CORNER_W, CORNER_LEN, CORNER_W);
        box_corner_seg(c, 0, h - CORNER_LEN, CORNER_W, CORNER_LEN);
    }
}

/** 从 g_q_det_to_ui 读最新检测结果，刷新框；非预览态清框并丢弃 */
static void process_detect_boxes(void)
{
    if (s_state != UI_STA_PREVIEW) {  /* 注册态不画检测框 */
        /* 非预览/注册态：排空队列 + 清框 */
        detect_to_ui_t drop;
        while (xQueueReceive(g_q_det_to_ui, &drop, 0) == pdTRUE) { }
        if (s_face_boxes[0] != NULL) {
            clear_face_boxes();
        }
        return;
    }

    /* 排空到最新 */
    detect_to_ui_t det;
    bool got = false;
    while (xQueueReceive(g_q_det_to_ui, &det, 0) == pdTRUE) {
        got = true;
    }
    if (got) {
        update_face_boxes(&det);
    }
}

/* ================================================================
 *  状态机辅助
 * ================================================================ */

/** @brief 切换到指定屏幕 */
static void switch_screen(ui_state_t new_state)
{
    if (new_state == s_state) {
        return;
    }
    s_state = new_state;

    switch (new_state) {
    case UI_STA_IDLE:
        if (s_scr_idle != NULL) {
            lv_scr_load(s_scr_idle);
        }
        clear_face_boxes();   /* 离开预览清框 */
        /* 清空预览帧引用 */
        if (s_preview_fb.buf != NULL) {
            svc_camera_release_frame(&s_preview_fb);
            memset(&s_preview_fb, 0, sizeof(s_preview_fb));
        }
        ESP_LOGI(SVC_UI_TAG, "screen → IDLE");
        break;

    case UI_STA_PREVIEW:
        if (s_scr_preview != NULL) {
            lv_scr_load(s_scr_preview);
            lv_obj_invalidate(s_scr_preview);  /* 强制全屏重绘 */
        }
        /* 通知摄像头恢复采集 */
        {
            camera_cmdmsg_t cmd = { .cmd = CAM_CMD_RESUME };
            xQueueSend(g_q_ui_to_cam, &cmd, 0);
        }
        s_last_frame_tick = xTaskGetTickCount();
        ESP_LOGI(SVC_UI_TAG, "screen → PREVIEW");
        break;

    case UI_STA_PERM_PASSWORD:
        /* 暂停 camera + 停止 AI 流水线 */
        {
            camera_cmdmsg_t cmd = { .cmd = CAM_CMD_PAUSE };
            xQueueSend(g_q_ui_to_cam, &cmd, 0);
        }
        clear_face_boxes();   /* 离开预览清框 */
        s_pwd_input[0] = '\0';
        update_pwd_label(s_pwd_label);
        if (s_scr_password != NULL) {
            lv_scr_load(s_scr_password);
        }
        /* 清空预览帧引用（不再需要显示预览） */
        if (s_preview_fb.buf != NULL) {
            svc_camera_release_frame(&s_preview_fb);
            memset(&s_preview_fb, 0, sizeof(s_preview_fb));
        }
        ESP_LOGI(SVC_UI_TAG, "screen → PERM_PASSWORD (camera paused)");
        break;

    case UI_STA_ADMIN:
        svc_perm_manager_set_enrolling(false); /* 退出注册模式 */
        {
            camera_cmdmsg_t cmd = { .cmd = CAM_CMD_PAUSE };
            xQueueSend(g_q_ui_to_cam, &cmd, 0);
        }
        if (s_scr_admin != NULL) {
            lv_scr_load(s_scr_admin);
        }
        /* 延迟释放预览帧：LVGL 可能还在引用该帧的 pixel 数据，
         * 立即释放会导致过渡帧读到无效内存 → 闪屏 */
        ESP_LOGI(SVC_UI_TAG, "screen → ADMIN");
        break;

    case UI_STA_ENROLL:
        svc_perm_manager_set_enrolling(true);  /* 先禁止识别，再启 camera */
        {
            camera_cmdmsg_t cmd = { .cmd = CAM_CMD_RESUME };
            xQueueSend(g_q_ui_to_cam, &cmd, 0);
        }
        if (s_scr_enroll != NULL) {
            lv_scr_load(s_scr_enroll);
            if (s_enroll_fb_label != NULL) lv_label_set_text(s_enroll_fb_label, "");
        }
        s_last_frame_tick = xTaskGetTickCount();
        ESP_LOGI(SVC_UI_TAG, "screen → ENROLL");
        break;

    default:
        break;
    }
}

/* ================================================================
 *  UI 任务主体
 * ================================================================ */

static void s_ui_task(void *arg)
{
    (void)arg;

    /* -- LVGL 初始化 -- */
    lv_init();

    /* -- 创建显示驱动 -- */
    s_disp = lv_display_create(UI_DISP_W, UI_DISP_H);
    if (s_disp == NULL) {
        ESP_LOGE(SVC_UI_TAG, "lv_display_create failed");
        s_worker = NULL;
        vTaskDelete(NULL);
        return;
    }

    /* 设置显示颜色格式为 RGB888（与 TC358762 帧缓冲一致） */
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB888);

    /* draw buffer 直接用 DPI panel 的两个 framebuffer（DIRECT 双缓冲）：
     * LVGL 渲染进 fb，flush_cb 仅 last 时调 draw_bitmap 触发 vsync swap，
     * 免撕裂。fb_size = 整屏（DIRECT 模式要求 draw buffer = 屏幕尺寸）。 */
    if (s_deps.disp_ops->get_fb(s_deps.disp_ctx, 0, &s_fb0) != DAL_OK ||
        s_deps.disp_ops->get_fb(s_deps.disp_ctx, 1, &s_fb1) != DAL_OK) {
        ESP_LOGE(SVC_UI_TAG, "get_fb failed");
        s_worker = NULL;
        vTaskDelete(NULL);
        return;
    }
    lv_display_set_buffers(s_disp, s_fb0, s_fb1,
                           (uint32_t)UI_DISP_W * UI_DISP_H * 3,
                           LV_DISPLAY_RENDER_MODE_DIRECT);

    /* 注册刷新回调 */
    lv_display_set_flush_cb(s_disp, display_flush_cb);

    /* -- 创建触摸输入设备 -- */
    s_indev = lv_indev_create();
    if (s_indev != NULL) {
        lv_indev_set_type(s_indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(s_indev, touch_read_cb);
        lv_indev_set_display(s_indev, s_disp);
    } else {
        ESP_LOGW(SVC_UI_TAG, "lv_indev_create failed, touch disabled");
    }

    /* 设置默认主题（基础主题，无额外字体/图片依赖） */
    lv_disp_set_default(s_disp);

    /* -- 构建屏幕 -- */
    create_idle_screen();
    create_preview_screen();
    create_password_screen();
    create_admin_screen();
    create_enroll_screen();

    /* 初始进入 IDLE */
    lv_scr_load(s_scr_idle);
    s_state = UI_STA_IDLE;

    ESP_LOGI(SVC_UI_TAG, "worker started, %dx%d RGB888", UI_DISP_W, UI_DISP_H);

    /* -- 主循环 -- */
    uint32_t lvgl_delay_ms = 5;

    for (;;) {
        /* perm 响应优先处理——识别弹框需在帧处理前发出，与开门同步 */
        perm_result_t pr;
        while (xQueueReceive(g_q_perm_to_ui, &pr, 0) == pdTRUE) {
            ESP_LOGI(SVC_UI_TAG, "perm result: cmd=%d ret=%d msg=%s", (int)pr.cmd, (int)pr.result, pr.msg);
            if (pr.cmd == PERM_CMD_ADD_USER) {
                if (s_state != UI_STA_ENROLL) {
                    continue;
                }
                if (s_enroll_fb_label) lv_label_set_text(s_enroll_fb_label, pr.msg);
            } else if (pr.cmd == PERM_CMD_RECOG_RESULT) {
                /* 识别反馈弹框：底部居中半透明，2 秒后自动消失 */
                if (s_recog_popup && s_recog_label) {
                    char masked[32];
                    mask_name(pr.user_name, masked, sizeof(masked));
                    if (pr.result == DAL_OK) {
                        lv_label_set_text_fmt(s_recog_label, "%s  %s", pr.msg, masked);
                        lv_obj_set_style_text_color(s_recog_label, lv_color_hex(0x4CAF50), 0);
                    } else {
                        lv_label_set_text_fmt(s_recog_label, "%s", pr.msg);
                        lv_obj_set_style_text_color(s_recog_label, lv_color_hex(0xFF5252), 0);
                    }
                    lv_obj_remove_flag(s_recog_popup, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_invalidate(s_recog_popup);  /* 立即标记脏区 */
                    s_recog_showing = true;
                    /* 1.5 秒后自动隐藏 */
                    if (s_recog_timer) lv_timer_del(s_recog_timer);
                    s_recog_timer = lv_timer_create(recog_popup_hide_cb, 1500, NULL);
                    lv_timer_set_repeat_count(s_recog_timer, 1);
                }
            } else {
                lv_obj_t *m = lv_msgbox_create(NULL);
                lv_msgbox_add_title(m, "Result");
                lv_msgbox_add_text(m, pr.msg);
                lv_msgbox_add_close_button(m);
            }
        }

        /* 摄像头帧处理 */
        process_camera_frame();
        /* 人脸检测框叠加 */
        process_detect_boxes();

        /* 预览超时回 IDLE（camera 停止推帧后触发） */
        if (s_state == UI_STA_PREVIEW
            && (xTaskGetTickCount() - s_last_frame_tick)
                >= pdMS_TO_TICKS(PREVIEW_TIMEOUT_MS)) {
            ESP_LOGI(SVC_UI_TAG, "preview timeout → IDLE");
            switch_screen(UI_STA_IDLE);
        }

        /* LVGL 渲染一轮（测实际耗时，驱动 lv_tick_inc 避免漂移） */
        int64_t t0 = esp_timer_get_time();
        lvgl_delay_ms = lv_timer_handler();
        uint32_t render_ms = (uint32_t)((esp_timer_get_time() - t0) / 1000);
        if (render_ms > 500) {
            ESP_LOGW(SVC_UI_TAG, "render slow: %ums (state=%d)", render_ms, (int)s_state);
        }

        /* 延时：相对延时 vTaskDelay 必定让出 CPU——渲染耗时超目标时
         * vTaskDelayUntil 会立即返回（追赶）不让出，饿死同核低优先级
         * 任务（ota/mqtt/perm）触发看门狗。保底 1 tick(10ms)、上限 33ms。 */
        uint32_t sleep_ms = lvgl_delay_ms;
        if (sleep_ms < 10) {
            sleep_ms = 10;
        } else if (sleep_ms > 33) {
            sleep_ms = 33;
        }
        TickType_t sleep_ticks = pdMS_TO_TICKS(sleep_ms);
        if (sleep_ticks == 0) {
            sleep_ticks = 1;
        }
        vTaskDelay(sleep_ticks);

        /* 推进 LVGL 内部 tick：实际耗时（渲染 + 睡眠），避免漂移 */
        lv_tick_inc(render_ms + sleep_ms);
    }
}

/* ================================================================
 *  公共 API
 * ================================================================ */

/**
 * @brief UI 服务初始化 — 注入依赖契约
 */
dal_err_t svc_ui_init(const svc_ui_deps_t *deps)
{
    if (s_inited) {
        return DAL_ERR_STATE;
    }

    if (deps == NULL) {
        return DAL_ERR_INVALID;
    }

    /* 校验 disp_ops */
    if (deps->disp_ops == NULL ||
        deps->disp_ops->init == NULL ||
        deps->disp_ops->fill == NULL ||
        deps->disp_ops->draw_bitmap == NULL ||
        deps->disp_ops->get_fb == NULL ||
        deps->disp_ops->deinit == NULL) {
        ESP_LOGE(SVC_UI_TAG, "init: disp_ops invalid");
        return DAL_ERR_INVALID;
    }

    s_deps = *deps;
    s_inited = true;
    ESP_LOGI(SVC_UI_TAG, "init ok");
    return DAL_OK;
}

/**
 * @brief UI 服务启动 — 初始化显示/触摸硬件 + 创建 UI 任务
 */
dal_err_t svc_ui_start(void)
{
    if (!s_inited) {
        return DAL_ERR_STATE;
    }
    if (s_worker != NULL) {
        return DAL_OK;
    }

    /* -- 同步初始化显示硬件（Assembler 不 init，由 UI 服务自行管理） -- */
    const dal_display_config_t disp_cfg = {
        .width      = UI_DISP_W,
        .height     = UI_DISP_H,
        .brightness = 80,
    };
    dal_err_t ret = s_deps.disp_ops->init(s_deps.disp_ctx, &disp_cfg);
    if (ret != DAL_OK) {
        ESP_LOGE(SVC_UI_TAG, "disp init failed: %d", ret);
        return ret;
    }

    /* -- 创建 UI 任务 -- */
    BaseType_t xret = xTaskCreatePinnedToCore(
        s_ui_task, "svc_ui",
        CONFIG_FACE_UI_TASK_STACK, NULL,
        CONFIG_FACE_UI_TASK_PRIO, &s_worker,
        0   /* Core 0 */
    );
    if (xret != pdPASS) {
        s_worker = NULL;
        s_deps.disp_ops->deinit(s_deps.disp_ctx);
        return DAL_ERR_NO_MEM;
    }

    ESP_LOGI(SVC_UI_TAG, "task created");
    return DAL_OK;
}

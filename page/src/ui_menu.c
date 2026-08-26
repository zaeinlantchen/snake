/*
 * ui_menu.c
 *
 * 用户名屏 + 主菜单屏（单人模式 / 多人模式）。
 */

#include <stdio.h>
#include <string.h>

#include "../inc/ui_page.h"

#ifndef UI_DEFAULT_NAME
#define UI_DEFAULT_NAME "Player"
#endif

/* ---------------- 用户名屏状态 ---------------- */
typedef struct {
    lv_obj_t *name_ta;
    lv_obj_t *kb;
    lv_obj_t *status;
    ui_name_cb_t on_name;
} name_state_t;
static name_state_t s_name;

/* “Start” 按钮回调：收起软键盘，读取输入框里的昵称并回调 on_name
 * 交给上层（snakeInit）发起网络连接。昵称为空时用默认名。 */
static void name_start_pressed(lv_event_t *e)
{
    (void)e;
    if (s_name.kb) lv_obj_add_flag(s_name.kb, LV_OBJ_FLAG_HIDDEN);   /* 点击后收起键盘 */
    if (!s_name.on_name) return;
    const char *n = lv_textarea_get_text(s_name.name_ta);
    s_name.on_name((n && n[0]) ? n : UI_DEFAULT_NAME);
}

/* 输入框聚焦回调：弹出底部软键盘并绑定到该输入框。 */
static void name_focus_cb(lv_event_t *e)
{
    lv_obj_clear_flag(s_name.kb, LV_OBJ_FLAG_HIDDEN);   /* 聚焦 -> 显示键盘 */
    lv_keyboard_set_textarea(s_name.kb, lv_event_get_target(e));
}

/* 输入框失焦回调：隐藏底部软键盘。 */
static void name_defocus_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_add_flag(s_name.kb, LV_OBJ_FLAG_HIDDEN);     /* 失焦 -> 隐藏键盘 */
}

/* 创建用户名屏：标题 + 提示文字 + 昵称输入框 + Start 按钮 + 状态提示，
 * 底部是默认隐藏的软键盘（聚焦输入框时弹出）。
 * 返回值：用户名屏对象（由上层 lv_scr_load() 加载）。 */
lv_obj_t *ui_name_screen_create(lv_obj_t *parent, ui_name_cb_t on_name, const char *def_name)
{
    memset(&s_name, 0, sizeof(s_name));
    s_name.on_name = on_name;

    lv_obj_t *scr = lv_obj_create(parent ? parent : NULL);
    lv_obj_set_size(scr, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x10202f), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(scr);                /* 屏标题 */
    lv_label_set_text(title, "Snake - Multiplayer");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_26, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x7fc8ff), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 25);

    lv_obj_t *tip = lv_label_create(scr);                  /* 操作提示文字 */
    lv_label_set_text(tip, "Enter your username");
    lv_obj_set_style_text_color(tip, lv_color_hex(0x9fb3c8), 0);
    lv_obj_align(tip, LV_ALIGN_TOP_MID, 0, 70);

    lv_obj_t *ta = lv_textarea_create(scr);                /* 昵称输入框（单行，聚焦弹软键盘） */
    lv_textarea_set_one_line(ta, 1);
    lv_obj_set_scrollbar_mode(ta, LV_SCROLLBAR_MODE_OFF);
    lv_textarea_set_text(ta, def_name ? def_name : UI_DEFAULT_NAME);
    lv_obj_set_size(ta, 380, 60);
    lv_obj_align(ta, LV_ALIGN_TOP_MID, 0, 110);
    lv_obj_set_style_bg_color(ta, lv_color_hex(0x1c3346), 0);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, 0);

    lv_obj_t *btn = lv_btn_create(scr);                    /* “Start”按钮：确认昵称并连接服务器 */
    lv_obj_set_size(btn, 180, 48);
    lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, 180);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x2e8b57), 0);
    lv_obj_t *bl = lv_label_create(btn);
    lv_label_set_text(bl, "Start");
    lv_obj_center(bl);

    lv_obj_t *st = lv_label_create(scr);                   /* 状态提示：连接中 / 失败等反馈文字 */
    lv_label_set_text(st, " ");
    lv_obj_set_style_text_color(st, lv_color_hex(0x7fc8ff), 0);
    lv_obj_align(st, LV_ALIGN_TOP_MID, 0, 250);

    lv_obj_t *kb = lv_keyboard_create(scr);                /* 底部软键盘：默认隐藏，输入框聚焦时弹出 */
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(kb, lv_color_hex(0x182c3c), 0);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, 0);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);            /* 初始隐藏，聚焦才显示 */
    lv_obj_add_event_cb(ta, name_focus_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(ta, name_defocus_cb, LV_EVENT_DEFOCUSED, NULL);
    lv_keyboard_set_textarea(kb, ta);

    s_name.name_ta = ta;
    s_name.kb = kb;
    s_name.status = st;

    lv_obj_add_event_cb(btn, name_start_pressed, LV_EVENT_CLICKED, NULL);
    return scr;
}

/* 更新用户名屏的状态提示文字（如 Connecting... / Connection lost）。 */
void ui_name_set_status(lv_obj_t *s, const char *msg)
{
    (void)s;
    if (s_name.status) lv_label_set_text(s_name.status, msg ? msg : " ");
}

/* ---------------- 主菜单屏状态 ---------------- */
typedef struct {
    lv_obj_t *status;
    lv_obj_t *btn_classic;      /* 经典地图按钮 */
    lv_obj_t *btn_wrap;         /* 环形地图按钮 */
    int       map_wrap;         /* 当前选中的地图：0=经典, 1=环形 */
    ui_mode_cb_t on_mode;
    ui_map_cb_t  on_map;
} main_state_t;
static main_state_t s_main;

/* 主菜单两个模式按钮的共用回调：把绑定的模式字符串
 * （single / multi）回调 on_mode 交给上层处理。 */
static void main_mode_pressed(lv_event_t *e)
{
    const char *mode = (const char *)lv_event_get_user_data(e);
    if (s_main.on_mode && mode) s_main.on_mode(mode);
}

/* 更新两个地图按钮的高亮样式：选中者用亮色，未选中者用暗色。 */
static void map_style_update(void)
{
    lv_obj_t *sel = s_main.map_wrap ? s_main.btn_wrap : s_main.btn_classic;
    lv_obj_t *unsel = s_main.map_wrap ? s_main.btn_classic : s_main.btn_wrap;
    if (sel) lv_obj_set_style_bg_color(sel, lv_color_hex(0x2e8b57), 0);
    if (unsel) lv_obj_set_style_bg_color(unsel, lv_color_hex(0x22394b), 0);
}

/* 地图选择按钮回调：切换选中项并把地图类型（0=经典, 1=环形）交给上层。 */
static void map_select_pressed(lv_event_t *e)
{
    int wrap = (int)(intptr_t)lv_event_get_user_data(e);
    if (s_main.map_wrap == wrap) return;
    s_main.map_wrap = wrap;
    map_style_update();
    if (s_main.on_map) s_main.on_map(wrap);
}

/* 创建主菜单屏：标题 + 两个大按钮（单人模式 / 多人模式）+ 地图选择（经典/环形）+ 底部状态提示。
 * 返回值：主菜单屏对象。 */
lv_obj_t *ui_main_screen_create(lv_obj_t *parent, ui_mode_cb_t on_mode, ui_map_cb_t on_map)
{
    memset(&s_main, 0, sizeof(s_main));
    s_main.on_mode = on_mode;
    s_main.on_map  = on_map;
    s_main.map_wrap = 0;               /* 默认经典地图（撞墙死） */

    lv_obj_t *scr = lv_obj_create(parent ? parent : NULL);
    lv_obj_set_size(scr, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x10202f), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(scr);                /* 屏标题 */
    lv_label_set_text(title, "Select Mode");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_26, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x7fc8ff), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    lv_obj_t *b1 = lv_btn_create(scr);                     /* 单人模式按钮：进入单人房间 */
    lv_obj_set_size(b1, 260, 150);
    lv_obj_align(b1, LV_ALIGN_LEFT_MID, 60, 0);
    lv_obj_set_style_radius(b1, 12, 0);
    lv_obj_set_style_bg_color(b1, lv_color_hex(0x2e8b57), 0);
    lv_obj_t *l1 = lv_label_create(b1);
    lv_label_set_text(l1, "Single\nPlayer");
    lv_obj_set_style_text_font(l1, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_align(l1, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(l1);
    lv_obj_add_event_cb(b1, main_mode_pressed, LV_EVENT_CLICKED, (void *)MODE_SINGLE);

    lv_obj_t *b2 = lv_btn_create(scr);                     /* 多人模式按钮：进入房间选择屏 */
    lv_obj_set_size(b2, 260, 150);
    lv_obj_align(b2, LV_ALIGN_RIGHT_MID, -60, 0);
    lv_obj_set_style_radius(b2, 12, 0);
    lv_obj_set_style_bg_color(b2, lv_color_hex(0x3d6fa8), 0);
    lv_obj_t *l2 = lv_label_create(b2);
    lv_label_set_text(l2, "Multi\nPlayer");
    lv_obj_set_style_text_font(l2, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_align(l2, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(l2);
    lv_obj_add_event_cb(b2, main_mode_pressed, LV_EVENT_CLICKED, (void *)MODE_MULTI);

    lv_obj_t *cap = lv_label_create(scr);                  /* 地图选择标题 */
    lv_label_set_text(cap, "Map");
    lv_obj_set_style_text_color(cap, lv_color_hex(0x9fb3c8), 0);
    lv_obj_align(cap, LV_ALIGN_BOTTOM_MID, 0, -152);

    lv_obj_t *mc = lv_btn_create(scr);                     /* 经典地图按钮：撞墙即死 */
    lv_obj_set_size(mc, 120, 44);
    lv_obj_align(mc, LV_ALIGN_BOTTOM_MID, 0, -110);
    lv_obj_set_style_radius(mc, 8, 0);
    lv_obj_set_style_bg_color(mc, lv_color_hex(0x2e8b57), 0);
    lv_obj_t *mcl = lv_label_create(mc);
    lv_label_set_text(mcl, "Classic");
    lv_obj_center(mcl);
    lv_obj_add_event_cb(mc, map_select_pressed, LV_EVENT_CLICKED, (void *)(intptr_t)0);

    lv_obj_t *mw = lv_btn_create(scr);                     /* 环形地图按钮：穿墙 */
    lv_obj_set_size(mw, 120, 44);
    lv_obj_align(mw, LV_ALIGN_BOTTOM_MID, 130, -110);
    lv_obj_set_style_radius(mw, 8, 0);
    lv_obj_set_style_bg_color(mw, lv_color_hex(0x22394b), 0);
    lv_obj_t *mwl = lv_label_create(mw);
    lv_label_set_text(mwl, "Wrap");
    lv_obj_center(mwl);
    lv_obj_add_event_cb(mw, map_select_pressed, LV_EVENT_CLICKED, (void *)(intptr_t)1);

    s_main.btn_classic = mc;
    s_main.btn_wrap = mw;
    map_style_update();

    lv_obj_t *st = lv_label_create(scr);                   /* 底部状态提示（如 Connected / 错误信息） */
    lv_label_set_text(st, " ");
    lv_obj_set_style_text_color(st, lv_color_hex(0x7fc8ff), 0);
    lv_obj_align(st, LV_ALIGN_BOTTOM_MID, 0, -30);

    s_main.status = st;
    return scr;
}

/* 更新主菜单屏的状态提示文字（如 Connected / 服务器错误信息）。 */
void ui_main_set_status(lv_obj_t *s, const char *msg)
{
    (void)s;
    if (s_main.status) lv_label_set_text(s_main.status, msg ? msg : " ");
}

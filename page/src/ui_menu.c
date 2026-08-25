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

static void name_start_pressed(lv_event_t *e)
{
    (void)e;
    if (s_name.kb) lv_obj_add_flag(s_name.kb, LV_OBJ_FLAG_HIDDEN);   /* 点击后收起键盘 */
    if (!s_name.on_name) return;
    const char *n = lv_textarea_get_text(s_name.name_ta);
    s_name.on_name((n && n[0]) ? n : UI_DEFAULT_NAME);
}

static void name_focus_cb(lv_event_t *e)
{
    lv_obj_clear_flag(s_name.kb, LV_OBJ_FLAG_HIDDEN);   /* 聚焦 -> 显示键盘 */
    lv_keyboard_set_textarea(s_name.kb, lv_event_get_target(e));
}

static void name_defocus_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_add_flag(s_name.kb, LV_OBJ_FLAG_HIDDEN);     /* 失焦 -> 隐藏键盘 */
}

lv_obj_t *ui_name_screen_create(lv_obj_t *parent, ui_name_cb_t on_name, const char *def_name)
{
    memset(&s_name, 0, sizeof(s_name));
    s_name.on_name = on_name;

    lv_obj_t *scr = lv_obj_create(parent ? parent : NULL);
    lv_obj_set_size(scr, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x10202f), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Snake - Multiplayer");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_26, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x7fc8ff), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 25);

    lv_obj_t *tip = lv_label_create(scr);
    lv_label_set_text(tip, "Enter your username");
    lv_obj_set_style_text_color(tip, lv_color_hex(0x9fb3c8), 0);
    lv_obj_align(tip, LV_ALIGN_TOP_MID, 0, 70);

    lv_obj_t *ta = lv_textarea_create(scr);
    lv_textarea_set_one_line(ta, 1);
    lv_textarea_set_text(ta, def_name ? def_name : UI_DEFAULT_NAME);
    lv_obj_set_size(ta, 380, 44);
    lv_obj_align(ta, LV_ALIGN_TOP_MID, 0, 110);
    lv_obj_set_style_bg_color(ta, lv_color_hex(0x1c3346), 0);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, 0);

    lv_obj_t *btn = lv_btn_create(scr);
    lv_obj_set_size(btn, 180, 48);
    lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, 180);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x2e8b57), 0);
    lv_obj_t *bl = lv_label_create(btn);
    lv_label_set_text(bl, "Start");
    lv_obj_center(bl);

    lv_obj_t *st = lv_label_create(scr);
    lv_label_set_text(st, " ");
    lv_obj_set_style_text_color(st, lv_color_hex(0x7fc8ff), 0);
    lv_obj_align(st, LV_ALIGN_TOP_MID, 0, 250);

    lv_obj_t *kb = lv_keyboard_create(scr);
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

void ui_name_set_status(lv_obj_t *s, const char *msg)
{
    (void)s;
    if (s_name.status) lv_label_set_text(s_name.status, msg ? msg : " ");
}

/* ---------------- 主菜单屏状态 ---------------- */
typedef struct {
    lv_obj_t *status;
    ui_mode_cb_t on_mode;
} main_state_t;
static main_state_t s_main;

static void main_mode_pressed(lv_event_t *e)
{
    const char *mode = (const char *)lv_event_get_user_data(e);
    if (s_main.on_mode && mode) s_main.on_mode(mode);
}

lv_obj_t *ui_main_screen_create(lv_obj_t *parent, ui_mode_cb_t on_mode)
{
    memset(&s_main, 0, sizeof(s_main));
    s_main.on_mode = on_mode;

    lv_obj_t *scr = lv_obj_create(parent ? parent : NULL);
    lv_obj_set_size(scr, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x10202f), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Select Mode");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_26, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x7fc8ff), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    lv_obj_t *b1 = lv_btn_create(scr);
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

    lv_obj_t *b2 = lv_btn_create(scr);
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

    lv_obj_t *st = lv_label_create(scr);
    lv_label_set_text(st, " ");
    lv_obj_set_style_text_color(st, lv_color_hex(0x7fc8ff), 0);
    lv_obj_align(st, LV_ALIGN_BOTTOM_MID, 0, -30);

    s_main.status = st;
    return scr;
}

void ui_main_set_status(lv_obj_t *s, const char *msg)
{
    (void)s;
    if (s_main.status) lv_label_set_text(s_main.status, msg ? msg : " ");
}

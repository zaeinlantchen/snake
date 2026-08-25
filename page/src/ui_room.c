/*
 * ui_room.c
 *
 * 多人模式 -> 房间选择屏：显示服务器维护的房间列表（点击加入）、
 * 按房间号加入、随机加入、创建房间、返回主菜单。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../inc/ui_page.h"

typedef struct {
    lv_obj_t *list;             /* 房间按钮容器 */
    lv_obj_t *room_ta;          /* 房间号输入 */
    lv_obj_t *kb;
    lv_obj_t *status;
    ui_room_cb_t cb;
} room_state_t;
static room_state_t s_room;

/* 点击某个房间 -> 加入 */
static void room_row_pressed(lv_event_t *e)
{
    int id = (int)(intptr_t)lv_event_get_user_data(e);
    if (s_room.cb) s_room.cb(UI_ROOM_JOIN, id);
}

/* 收起软键盘 */
static void hide_kb(void) { if (s_room.kb) lv_obj_add_flag(s_room.kb, LV_OBJ_FLAG_HIDDEN); }

/* 按房间号加入 */
static void join_by_number(lv_event_t *e)
{
    (void)e;
    hide_kb();
    const char *t = lv_textarea_get_text(s_room.room_ta);
    int id = (t && t[0]) ? atoi(t) : -1;
    if (s_room.cb && id >= 0) s_room.cb(UI_ROOM_JOIN, id);
}

static void random_join(lv_event_t *e) { (void)e; hide_kb(); if (s_room.cb) s_room.cb(UI_ROOM_RANDOM, 0); }
static void create_room(lv_event_t *e) { (void)e; hide_kb(); if (s_room.cb) s_room.cb(UI_ROOM_CREATE, 0); }
static void back_pressed(lv_event_t *e) { (void)e; hide_kb(); if (s_room.cb) s_room.cb(UI_ROOM_BACK, 0); }

static void room_focus_cb(lv_event_t *e)
{
    lv_obj_clear_flag(s_room.kb, LV_OBJ_FLAG_HIDDEN);   /* 聚焦 -> 显示键盘 */
    lv_keyboard_set_textarea(s_room.kb, lv_event_get_target(e));
}

static void room_defocus_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_add_flag(s_room.kb, LV_OBJ_FLAG_HIDDEN);     /* 失焦 -> 隐藏键盘 */
}

lv_obj_t *ui_room_screen_create(lv_obj_t *parent, ui_room_cb_t cb)
{
    memset(&s_room, 0, sizeof(s_room));
    s_room.cb = cb;

    lv_obj_t *scr = lv_obj_create(parent ? parent : NULL);
    lv_obj_set_size(scr, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x10202f), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Multiplayer - Select Room");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_26, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x7fc8ff), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

    /* 房间列表容器（可滚动） */
    lv_obj_t *list = lv_obj_create(scr);
    lv_obj_set_size(list, 380, 200);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 120);
    lv_obj_set_style_bg_color(list, lv_color_hex(0x16283a), 0);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(list, 8, 0);
    lv_obj_set_style_pad_all(list, 6, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 4, 0);

    /* 房间号输入 + 加入（放在顶部，聚焦弹键盘时不遮挡） */
    lv_obj_t *ta = lv_textarea_create(scr);
    lv_textarea_set_one_line(ta, 1);
    lv_obj_set_size(ta, 120, 40);
    lv_obj_align(ta, LV_ALIGN_TOP_LEFT, 130, 62);
    lv_obj_set_style_bg_color(ta, lv_color_hex(0x1c3346), 0);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, 0);

    lv_obj_t *joinbtn = lv_btn_create(scr);
    lv_obj_set_size(joinbtn, 120, 40);
    lv_obj_align(joinbtn, LV_ALIGN_TOP_LEFT, 270, 62);
    lv_obj_set_style_bg_color(joinbtn, lv_color_hex(0x2e8b57), 0);
    lv_obj_t *jb = lv_label_create(joinbtn);
    lv_label_set_text(jb, "Join");
    lv_obj_center(jb);
    lv_obj_add_event_cb(joinbtn, join_by_number, LV_EVENT_CLICKED, NULL);

    /* 随机加入 / 创建房间 */
    lv_obj_t *ranbtn = lv_btn_create(scr);
    lv_obj_set_size(ranbtn, 160, 44);
    lv_obj_align(ranbtn, LV_ALIGN_TOP_LEFT, 60, 350);
    lv_obj_set_style_bg_color(ranbtn, lv_color_hex(0x3d6fa8), 0);
    lv_obj_t *rb = lv_label_create(ranbtn);
    lv_label_set_text(rb, "Random");
    lv_obj_center(rb);
    lv_obj_add_event_cb(ranbtn, random_join, LV_EVENT_CLICKED, NULL);

    lv_obj_t *cbtn = lv_btn_create(scr);
    lv_obj_set_size(cbtn, 160, 44);
    lv_obj_align(cbtn, LV_ALIGN_TOP_RIGHT, -60, 350);
    lv_obj_set_style_bg_color(cbtn, lv_color_hex(0x7a5c2e), 0);
    lv_obj_t *cbl = lv_label_create(cbtn);
    lv_label_set_text(cbl, "Create");
    lv_obj_center(cbl);
    lv_obj_add_event_cb(cbtn, create_room, LV_EVENT_CLICKED, NULL);

    /* 返回 */
    lv_obj_t *backbtn = lv_btn_create(scr);
    lv_obj_set_size(backbtn, 130, 44);
    lv_obj_align(backbtn, LV_ALIGN_TOP_LEFT, 60, 408);
    lv_obj_set_style_bg_color(backbtn, lv_color_hex(0x5a3a3a), 0);
    lv_obj_t *bb = lv_label_create(backbtn);
    lv_label_set_text(bb, "Back");
    lv_obj_center(bb);
    lv_obj_add_event_cb(backbtn, back_pressed, LV_EVENT_CLICKED, NULL);

    /* 状态 */
    lv_obj_t *st = lv_label_create(scr);
    lv_label_set_text(st, " ");
    lv_obj_set_style_text_color(st, lv_color_hex(0x7fc8ff), 0);
    lv_obj_align(st, LV_ALIGN_TOP_RIGHT, -60, 120);

    /* 软键盘（房间号为数字；聚焦显示，失焦隐藏） */
    lv_obj_t *kb = lv_keyboard_create(scr);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(kb, lv_color_hex(0x182c3c), 0);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, 0);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(ta, room_focus_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(ta, room_defocus_cb, LV_EVENT_DEFOCUSED, NULL);
    lv_keyboard_set_textarea(kb, ta);

    s_room.list = list;
    s_room.room_ta = ta;
    s_room.kb = kb;
    s_room.status = st;
    return scr;
}

void ui_room_refresh(lv_obj_t *s, const int ids[], const int players[], const int maxs[], int n)
{
    (void)s;
    if (!s_room.list) return;
    lv_obj_clean(s_room.list);      /* 清空旧房间 */

    if (n <= 0) {
        lv_obj_t *l = lv_label_create(s_room.list);
        lv_label_set_text(l, "No rooms yet");
        lv_obj_set_style_text_color(l, lv_color_hex(0x9fb3c8), 0);
        return;
    }
    for (int i = 0; i < n; i++) {
        lv_obj_t *row = lv_btn_create(s_room.list);
        lv_obj_set_size(row, LV_PCT(100), 42);
        lv_obj_set_style_radius(row, 8, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x22405a), 0);
        lv_obj_t *lab = lv_label_create(row);
        char buf[64];
        snprintf(buf, sizeof(buf), "Room %d   (%d/%d)", ids[i], players[i], maxs[i]);
        lv_label_set_text(lab, buf);
        lv_obj_center(lab);
        lv_obj_add_event_cb(row, room_row_pressed, LV_EVENT_CLICKED, (void *)(intptr_t)ids[i]);
    }
}

void ui_room_set_status(lv_obj_t *s, const char *msg)
{
    (void)s;
    if (s_room.status) lv_label_set_text(s_room.status, msg ? msg : " ");
}

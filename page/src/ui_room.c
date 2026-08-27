/*
 * ui_room.c
 *
 * 多人模式 -> 房间选择屏（白底）：
 *   左侧左上角返回键（back 图标）+ 创建房间按键；
 *   右侧顶部搜索框（左 320 宽搜索框 + 右 60 宽搜索按键），按房间号加入；
 *   搜索框下方为房间列表（显示房间号与用户数，点击加入）。
 *   错误信息以居中弹出框显示，2 秒后自动消失。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../inc/ui_page.h"
#include "../../font.h"          /* 中文字体（FreeType） */

LV_IMG_DECLARE(back);           /* LunaUI/img/back.c（200×200 黑色图标） */

typedef struct {
    lv_obj_t *list;             /* 房间按钮容器 */
    lv_obj_t *room_ta;          /* 房间号搜索框 */
    lv_obj_t *kb;
    lv_obj_t *popup;            /* 错误弹出框 */
    lv_obj_t *popup_label;
    lv_timer_t *pop_timer;      /* 弹出框自动消失定时器 */
    ui_room_cb_t cb;
} room_state_t;
static room_state_t s_room;

/* 点击房间列表中的某一项：把该房间号通过回调 UI_ROOM_JOIN 交给上层。 */
static void room_row_pressed(lv_event_t *e)
{
    int id = (int)(intptr_t)lv_event_get_user_data(e);
    if (s_room.cb) s_room.cb(UI_ROOM_JOIN, id);
}

/* 收起软键盘（各按钮点击后调用，避免遮挡界面）。 */
static void hide_kb(void) { if (s_room.kb) lv_obj_add_flag(s_room.kb, LV_OBJ_FLAG_HIDDEN); }

/* 搜索按键回调：读取搜索框中的房间号，按号加入该房间。 */
static void search_pressed(lv_event_t *e)
{
    (void)e;
    hide_kb();
    const char *t = lv_textarea_get_text(s_room.room_ta);
    int id = (t && t[0]) ? atoi(t) : -1;
    if (s_room.cb && id >= 0) s_room.cb(UI_ROOM_JOIN, id);
}

/* “Create”按钮回调：创建新房间。 */
static void create_room(lv_event_t *e) { (void)e; hide_kb(); if (s_room.cb) s_room.cb(UI_ROOM_CREATE, 0); }
/* “Back”按钮回调：返回主菜单。 */
static void back_pressed(lv_event_t *e) { (void)e; hide_kb(); if (s_room.cb) s_room.cb(UI_ROOM_BACK, 0); }

/* 搜索框聚焦回调：弹出软键盘并绑定。 */
static void room_focus_cb(lv_event_t *e)
{
    lv_obj_clear_flag(s_room.kb, LV_OBJ_FLAG_HIDDEN);   /* 聚焦 -> 显示键盘 */
    lv_keyboard_set_textarea(s_room.kb, lv_event_get_target(e));
}

/* 搜索框失焦回调：隐藏软键盘。 */
static void room_defocus_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_add_flag(s_room.kb, LV_OBJ_FLAG_HIDDEN);     /* 失焦 -> 隐藏键盘 */
}

/* ---------------- 错误弹出框 ---------------- */

static void popup_hide(void)
{
    if (s_room.popup) lv_obj_add_flag(s_room.popup, LV_OBJ_FLAG_HIDDEN);
}

/* 弹出框超时回调：到时后自动隐藏。 */
static void popup_timeout_cb(lv_timer_t *t)
{
    (void)t;
    s_room.pop_timer = NULL;
    popup_hide();
}

/* 显示弹出框消息；空消息则隐藏。重复调用会重置计时。 */
static void popup_show(const char *msg)
{
    if (!s_room.popup) return;
    if (!msg || !msg[0]) { popup_hide(); return; }
    if (s_room.pop_timer) { lv_timer_del(s_room.pop_timer); s_room.pop_timer = NULL; }
    lv_label_set_text(s_room.popup_label, msg);
    lv_obj_clear_flag(s_room.popup, LV_OBJ_FLAG_HIDDEN);
    s_room.pop_timer = lv_timer_create(popup_timeout_cb, 2000, NULL);
    lv_timer_set_repeat_count(s_room.pop_timer, 1);
}

/* 点击弹出框可提前关闭。 */
static void popup_pressed_cb(lv_event_t *e)
{
    (void)e;
    if (s_room.pop_timer) { lv_timer_del(s_room.pop_timer); s_room.pop_timer = NULL; }
    popup_hide();
}

/* 创建多人房间选择屏（白底）：
 *   左侧：左上角返回键（back 图标）、其右侧创建房间按键；
 *   右侧：顶部搜索框（320 + 60），下方房间列表；
 *   错误信息用居中弹出框显示，2 秒后自动消失；底部软键盘默认隐藏。
 * 返回值：房间屏对象。 */
lv_obj_t *ui_room_screen_create(lv_obj_t *parent, ui_room_cb_t cb)
{
    memset(&s_room, 0, sizeof(s_room));
    s_room.cb = cb;

    lv_obj_t *scr = lv_obj_create(parent ? parent : NULL);
    lv_obj_set_size(scr, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* 左侧左上角：返回键（back 图标） */
    lv_obj_t *backbtn = lv_btn_create(scr);
    lv_obj_set_size(backbtn, 80, 80);
    lv_obj_set_pos(backbtn, 20, 20);
    lv_obj_set_style_radius(backbtn, 20, 0);
    lv_obj_set_style_bg_color(backbtn, lv_color_hex(0xeef2f6), 0);
    lv_obj_set_style_pad_all(backbtn, 0, 0);
    lv_obj_t *back_img = lv_img_create(backbtn);
    lv_img_set_src(back_img, &back);                     /* 44×44 原生尺寸 */
    lv_obj_center(back_img);
    lv_obj_add_event_cb(backbtn, back_pressed, LV_EVENT_CLICKED, NULL);

    /* 返回键右侧：创建房间 */
    lv_obj_t *cbtn = lv_btn_create(scr);                 /* “Create”按钮：创建新房间 */
    lv_obj_set_size(cbtn, 150, 50);
    lv_obj_set_pos(cbtn, 120, 35);
    lv_obj_set_style_radius(cbtn, 8, 0);
    lv_obj_set_style_bg_color(cbtn, lv_color_hex(0x2e8b57), 0);
    lv_obj_t *cbl = lv_label_create(cbtn);
    lv_label_set_text(cbl, "创建房间");
    lv_obj_set_style_text_font(cbl, luna_font_normal(), 0);
    lv_obj_set_style_text_color(cbl, lv_color_hex(0xffffff), 0);
    lv_obj_center(cbl);
    lv_obj_add_event_cb(cbtn, create_room, LV_EVENT_CLICKED, NULL);

    /* 右侧顶部：搜索框（左 320 + 右 60） */
    lv_obj_t *ta = lv_textarea_create(scr);              /* 房间号搜索框（数字） */
    lv_textarea_set_one_line(ta, 1);
    lv_obj_set_scrollbar_mode(ta, LV_SCROLLBAR_MODE_OFF);
    lv_textarea_set_placeholder_text(ta, "房间号");
    lv_obj_set_size(ta, 320, 50);
    lv_obj_set_pos(ta, 390, 25);
    lv_obj_set_style_bg_color(ta, lv_color_hex(0xf4f6f8), 0);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(ta, lv_color_hex(0x22364a), 0);
    lv_obj_set_style_text_font(ta, luna_font_normal(), 0);
    lv_obj_set_style_border_color(ta, lv_color_hex(0x9fb3c8), 0);
    lv_obj_set_style_border_width(ta, 2, 0);
    lv_obj_set_style_radius(ta, 8, 0);
    lv_obj_set_style_pad_all(ta, 6, 0);

    lv_obj_t *searchbtn = lv_btn_create(scr);            /* 搜索按键：按输入的房间号加入 */
    lv_obj_set_size(searchbtn, 60, 50);
    lv_obj_set_pos(searchbtn, 720, 25);
    lv_obj_set_style_radius(searchbtn, 8, 0);
    lv_obj_set_style_bg_color(searchbtn, lv_color_hex(0x3d6fa8), 0);
    lv_obj_t *sb = lv_label_create(searchbtn);
    lv_label_set_text(sb, "搜索");
    lv_obj_set_style_text_font(sb, luna_font_normal(), 0);
    lv_obj_set_style_text_color(sb, lv_color_hex(0xffffff), 0);
    lv_obj_center(sb);
    lv_obj_add_event_cb(searchbtn, search_pressed, LV_EVENT_CLICKED, NULL);

    /* 搜索框下方：房间列表（可滚动，点击即加入） */
    lv_obj_t *list = lv_obj_create(scr);
    lv_obj_set_size(list, 390, 320);
    lv_obj_set_pos(list, 390, 95);
    lv_obj_set_style_bg_color(list, lv_color_hex(0xf1f3f5), 0);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(list, 8, 0);
    lv_obj_set_style_pad_all(list, 6, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 4, 0);

    /* 软键盘（房间号为数字；聚焦显示，失焦隐藏） */
    lv_obj_t *kb = lv_keyboard_create(scr);              /* 底部软键盘：默认隐藏，聚焦搜索框时弹出 */
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(kb, lv_color_hex(0x182c3c), 0);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, 0);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(ta, room_focus_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(ta, room_defocus_cb, LV_EVENT_DEFOCUSED, NULL);
    lv_keyboard_set_textarea(kb, ta);

    /* 错误弹出框：居中，2 秒后自动消失（最后创建保证在最上层） */
    lv_obj_t *popup = lv_obj_create(scr);
    lv_obj_set_size(popup, 460, 140);
    lv_obj_align(popup, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_pad_all(popup, 0, 0);
    lv_obj_set_style_bg_color(popup, lv_color_hex(0x1f2a36), 0);
    lv_obj_set_style_bg_opa(popup, LV_OPA_90, 0);
    lv_obj_set_style_radius(popup, 12, 0);
    lv_obj_clear_flag(popup, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(popup, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *pl = lv_label_create(popup);
    lv_label_set_text(pl, " ");
    lv_obj_set_style_text_color(pl, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(pl, luna_font_normal(), 0);
    lv_obj_set_style_text_align(pl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(pl);
    lv_obj_add_event_cb(popup, popup_pressed_cb, LV_EVENT_CLICKED, NULL);

    s_room.list = list;
    s_room.room_ta = ta;
    s_room.kb = kb;
    s_room.popup = popup;
    s_room.popup_label = pl;
    return scr;
}

/* 刷新房间列表：清空旧列表，按服务端返回的房间数组为每个房间
 * 生成一个可点击的按钮行（显示 "Room N (players/max) [C|W]"，C=经典, W=环形），
 * 点击即加入。 */
void ui_room_refresh(lv_obj_t *s, const int ids[], const int players[],
                     const int maxs[], const int wraps[], int n)
{
    (void)s;
    if (!s_room.list) return;
    lv_obj_clean(s_room.list);      /* 清空旧房间 */

    if (n <= 0) {
        lv_obj_t *l = lv_label_create(s_room.list);          /* 无房间时的占位提示 */
        lv_label_set_text(l, "暂无房间");
        lv_obj_set_style_text_color(l, lv_color_hex(0x7a8ca0), 0);
        lv_obj_set_style_text_font(l, luna_font_normal(), 0);
        return;
    }
    for (int i = 0; i < n; i++) {
        lv_obj_t *row = lv_btn_create(s_room.list);          /* 一个房间的入口按钮 */
        lv_obj_set_size(row, LV_PCT(100), 42);
        lv_obj_set_style_radius(row, 8, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(0xdfe9f2), 0);
        lv_obj_t *lab = lv_label_create(row);
        char buf[64];
        int w = (wraps && wraps[i]) ? 1 : 0;
        snprintf(buf, sizeof(buf), "房间 %d   (%d/%d) %s", ids[i], players[i], maxs[i],
                 w ? "环形" : "经典");
        lv_label_set_text(lab, buf);
        lv_obj_set_style_text_color(lab, lv_color_hex(0x22364a), 0);
        lv_obj_set_style_text_font(lab, luna_font_normal(), 0);
        lv_obj_center(lab);
        lv_obj_add_event_cb(row, room_row_pressed, LV_EVENT_CLICKED, (void *)(intptr_t)ids[i]);
    }
}

/* 提示信息（如 Loading rooms...）—— 新布局中不再单独显示，保留接口兼容。 */
void ui_room_set_status(lv_obj_t *s, const char *msg)
{
    (void)s;
    (void)msg;
}

/* 错误信息：以居中弹出框显示，2 秒后自动消失。 */
void ui_room_show_error(lv_obj_t *s, const char *msg)
{
    (void)s;
    popup_show(msg);
}

/*
 * ui_menu.c
 *
 * 用户名屏 + 主菜单屏（单人模式 / 多人模式）。
 */

#include <stdio.h>
#include <string.h>

#include "../inc/ui_page.h"
#include "../../font.h"          /* 中文字体（FreeType） */

/* LunaUI/img 目录下的图片（200×200 黑色图标） */
LV_IMG_DECLARE(start);
LV_IMG_DECLARE(img_map);
LV_IMG_DECLARE(own);
LV_IMG_DECLARE(multy);

#ifndef UI_DEFAULT_NAME
#define UI_DEFAULT_NAME "玩家"
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

/* 创建用户名屏（白底）：
 *   左侧显示游戏名；右侧为 300×400 用户名卡片（背景透明度 80%），
 *   卡片内从上到下依次为昵称输入框、开始键（80×80 圆角矩形，start 图标居中）；
 *   错误/提示信息在页面底部居中横向滚动；底部软键盘宽度改为 640。 */
lv_obj_t *ui_name_screen_create(lv_obj_t *parent, ui_name_cb_t on_name, const char *def_name)
{
    memset(&s_name, 0, sizeof(s_name));
    s_name.on_name = on_name;

    lv_obj_t *scr = lv_obj_create(parent ? parent : NULL);
    lv_obj_set_size(scr, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* 左侧：游戏名 */
    lv_obj_t *title = lv_label_create(scr);                /* 主标题 */
    lv_label_set_text(title, "贪吃蛇");
    lv_obj_set_style_text_font(title, luna_font_title(), 0);   /* 48px 中文字体 */
    lv_obj_set_style_text_color(title, lv_color_hex(0x22405a), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 60, -26);
    lv_obj_t *sub = lv_label_create(scr);                  /* 副标题 */
    lv_label_set_text(sub, "多人联机");
    lv_obj_set_style_text_font(sub, luna_font_normal(), 0);    /* 24px 中文字体 */
    lv_obj_set_style_text_color(sub, lv_color_hex(0x7a8ca0), 0);
    lv_obj_align(sub, LV_ALIGN_LEFT_MID, 60, 30);

    /* 右侧：用户名卡片，300×400，透明度 80%（255*0.8=204） */
    lv_obj_t *card = lv_obj_create(scr);
    lv_obj_set_size(card, 300, 400);
    lv_obj_align(card, LV_ALIGN_RIGHT_MID, -40, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0xbfd4e8), 0);
    lv_obj_set_style_bg_opa(card, 204, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_top(card, 60, 0);
    lv_obj_set_style_pad_row(card, 30, 0);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *ta = lv_textarea_create(card);               /* 昵称输入框（单行） */
    lv_textarea_set_one_line(ta, 1);
    lv_obj_set_scrollbar_mode(ta, LV_SCROLLBAR_MODE_OFF);
    lv_textarea_set_text(ta, def_name ? def_name : UI_DEFAULT_NAME);
    lv_obj_set_size(ta, 240, 56);
    lv_obj_set_style_bg_color(ta, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(ta, lv_color_hex(0x22364a), 0);
    lv_obj_set_style_text_font(ta, luna_font_normal(), 0);   /* 24px 中文字体 */
    lv_obj_set_style_border_color(ta, lv_color_hex(0x9fb3c8), 0);
    lv_obj_set_style_border_width(ta, 2, 0);
    lv_obj_set_style_radius(ta, 8, 0);
    lv_obj_set_style_pad_all(ta, 6, 0);

    lv_obj_t *btn = lv_btn_create(card);                   /* “Start”键：80×80 圆角矩形，start 图标居中 */
    lv_obj_set_size(btn, 80, 80);
    lv_obj_set_style_radius(btn, 20, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x2e8b57), 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_t *start_img = lv_img_create(btn);
    lv_img_set_src(start_img, &start);                   /* 56×56 原生尺寸 */
    lv_obj_center(start_img);
    lv_obj_add_event_cb(btn, name_start_pressed, LV_EVENT_CLICKED, NULL);

    /* 状态/错误信息：页面底部居中，横向滚动 */
    lv_obj_t *st = lv_label_create(scr);
    lv_label_set_long_mode(st, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(st, 640);
    lv_label_set_text(st, " ");
    lv_obj_set_style_text_color(st, lv_color_hex(0xc0392b), 0);
    lv_obj_set_style_text_font(st, luna_font_normal(), 0);   /* 24px 中文字体 */
    lv_obj_align(st, LV_ALIGN_BOTTOM_MID, 0, -36);

    /* 底部软键盘：宽度 640，高度不变 */
    lv_obj_t *kb = lv_keyboard_create(scr);
    lv_obj_set_width(kb, 640);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);            /* 初始隐藏，聚焦才显示 */
    lv_obj_add_event_cb(ta, name_focus_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(ta, name_defocus_cb, LV_EVENT_DEFOCUSED, NULL);
    lv_keyboard_set_textarea(kb, ta);

    s_name.name_ta = ta;
    s_name.kb = kb;
    s_name.status = st;
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

/* 创建主菜单屏（白底）：
 *   左侧左上角显示地图图标（map.c），次行显示经典/环形两个地图选择按键；
 *   右侧显示两个模式入口（单人模式在上、多人模式在下），分别用
 *   ownmode.c / multymode.c 图片表示。 */
lv_obj_t *ui_main_screen_create(lv_obj_t *parent, ui_mode_cb_t on_mode, ui_map_cb_t on_map)
{
    memset(&s_main, 0, sizeof(s_main));
    s_main.on_mode = on_mode;
    s_main.on_map  = on_map;
    s_main.map_wrap = 0;               /* 默认经典地图（撞墙死） */

    lv_obj_t *scr = lv_obj_create(parent ? parent : NULL);
    lv_obj_set_size(scr, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* 左侧左上角：地图图标 */
    lv_obj_t *map_img = lv_img_create(scr);
    lv_img_set_src(map_img, &img_map);                   /* 150×150 原生尺寸 */
    lv_obj_set_pos(map_img, 24, 24);

    /* 次行：两个地图选择按键（Classic / Wrap） */
    lv_obj_t *mc = lv_btn_create(scr);                 /* 经典地图按钮：撞墙即死 */
    lv_obj_set_size(mc, 130, 46);
    lv_obj_set_pos(mc, 24, 196);
    lv_obj_set_style_radius(mc, 8, 0);
    lv_obj_set_style_bg_color(mc, lv_color_hex(0x2e8b57), 0);
    lv_obj_t *mcl = lv_label_create(mc);
    lv_label_set_text(mcl, "经典");
    lv_obj_set_style_text_font(mcl, luna_font_normal(), 0);
    lv_obj_set_style_text_color(mcl, lv_color_hex(0xffffff), 0);
    lv_obj_center(mcl);
    lv_obj_add_event_cb(mc, map_select_pressed, LV_EVENT_CLICKED, (void *)(intptr_t)0);

    lv_obj_t *mw = lv_btn_create(scr);                 /* 环形地图按钮：穿墙 */
    lv_obj_set_size(mw, 130, 46);
    lv_obj_set_pos(mw, 170, 196);
    lv_obj_set_style_radius(mw, 8, 0);
    lv_obj_set_style_bg_color(mw, lv_color_hex(0x22394b), 0);
    lv_obj_t *mwl = lv_label_create(mw);
    lv_label_set_text(mwl, "环形");
    lv_obj_set_style_text_font(mwl, luna_font_normal(), 0);
    lv_obj_set_style_text_color(mwl, lv_color_hex(0xffffff), 0);
    lv_obj_center(mwl);
    lv_obj_add_event_cb(mw, map_select_pressed, LV_EVENT_CLICKED, (void *)(intptr_t)1);

    /* 右侧：单人模式（上）/ 多人模式（下）入口，用 own / multy 图片 */
    lv_obj_t *b1 = lv_btn_create(scr);                 /* 单人模式入口 */
    lv_obj_set_size(b1, 320, 160);
    lv_obj_set_pos(b1, 440, 60);
    lv_obj_set_style_radius(b1, 12, 0);
    lv_obj_set_style_bg_color(b1, lv_color_hex(0x2e8b57), 0);
    lv_obj_set_style_pad_all(b1, 0, 0);
    lv_obj_clear_flag(b1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(b1, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(b1, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(b1, 6, 0);
    lv_obj_t *own_img = lv_img_create(b1);
    lv_img_set_src(own_img, &own);                       /* 96×96 原生尺寸 */
    lv_obj_t *l1 = lv_label_create(b1);
    lv_label_set_text(l1, "单人模式");
    lv_obj_set_style_text_font(l1, luna_font_normal(), 0);
    lv_obj_set_style_text_color(l1, lv_color_hex(0xffffff), 0);
    lv_obj_add_event_cb(b1, main_mode_pressed, LV_EVENT_CLICKED, (void *)MODE_SINGLE);

    lv_obj_t *b2 = lv_btn_create(scr);                 /* 多人模式入口 */
    lv_obj_set_size(b2, 320, 160);
    lv_obj_set_pos(b2, 440, 250);
    lv_obj_set_style_radius(b2, 12, 0);
    lv_obj_set_style_bg_color(b2, lv_color_hex(0x3d6fa8), 0);
    lv_obj_set_style_pad_all(b2, 0, 0);
    lv_obj_clear_flag(b2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(b2, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(b2, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(b2, 6, 0);
    lv_obj_t *multy_img = lv_img_create(b2);
    lv_img_set_src(multy_img, &multy);                   /* 96×96 原生尺寸 */
    lv_obj_t *l2 = lv_label_create(b2);
    lv_label_set_text(l2, "多人模式");
    lv_obj_set_style_text_font(l2, luna_font_normal(), 0);
    lv_obj_set_style_text_color(l2, lv_color_hex(0xffffff), 0);
    lv_obj_add_event_cb(b2, main_mode_pressed, LV_EVENT_CLICKED, (void *)MODE_MULTI);

    s_main.btn_classic = mc;
    s_main.btn_wrap = mw;
    map_style_update();

    lv_obj_t *st = lv_label_create(scr);               /* 底部状态提示（如 已连接 / 错误信息） */
    lv_label_set_text(st, " ");
    lv_obj_set_style_text_color(st, lv_color_hex(0x7a8ca0), 0);
    lv_obj_set_style_text_font(st, luna_font_normal(), 0);
    lv_obj_align(st, LV_ALIGN_BOTTOM_MID, 0, -20);

    s_main.status = st;
    return scr;
}

/* 更新主菜单屏的状态提示文字（如 Connected / 服务器错误信息）。 */
void ui_main_set_status(lv_obj_t *s, const char *msg)
{
    (void)s;
    if (s_main.status) lv_label_set_text(s_main.status, msg ? msg : " ");
}

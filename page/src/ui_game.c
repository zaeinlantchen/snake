/*
 * ui_game.c
 *
 * 游戏屏：顶部得分 + 菜单栏（退出回主菜单）+ 左侧虚拟摇杆 + Canvas 棋盘。
 *
 * 渲染：整张棋盘画进一张 lv_canvas，每次状态帧刷新。
 * 摇杆：拖动虚拟摇杆，把方向通过 on_dir 交给上层；松开时保持上次方向。
 * 无敌：无敌中的蛇带白色“护盾”描边；顶部显示本机剩馀无敌秒数。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "../inc/ui_page.h"

#define UI_CELL        17
#define UI_CANVAS_W    (SNAKE_COLS * UI_CELL)   /* 612 */
#define UI_CANVAS_H    (SNAKE_ROWS * UI_CELL)   /* 374 */

typedef struct {
    lv_obj_t *canvas;
    lv_obj_t *hud;            /* 顶部分数/无敌 */
    lv_obj_t *over;           /* 本局结束遮罩 */
    lv_obj_t *over_label;
    lv_obj_t *joy_base;       /* 摇杆底 */
    lv_obj_t *joy_knob;       /* 摇杆头 */
    int   last_sent_dir;      /* 上次发送给服务端的方向；-1 表示尚未发送 */
    int   my_id;
    ui_quit_cb_t on_quit;
    ui_dir_cb_t  on_dir;
} game_state_t;
static game_state_t s_game;

static lv_color_t s_canvas_buf[UI_CANVAS_W * UI_CANVAS_H];

/* ---------------- 摇杆 ---------------- */
static void joystick_release(lv_event_t *e)
{
    (void)e;
    int w = lv_obj_get_width(s_game.joy_base);
    int h = lv_obj_get_height(s_game.joy_base);
    int kn = lv_obj_get_width(s_game.joy_knob);
    lv_obj_set_pos(s_game.joy_knob, w / 2 - kn / 2, h / 2 - kn / 2);
}

static void joystick_pressing(lv_event_t *e)
{
    (void)e;
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;
    lv_point_t pt;
    lv_indev_get_point(indev, &pt);

    lv_area_t area;
    lv_obj_get_coords(s_game.joy_base, &area);
    int cx = (area.x1 + area.x2) / 2;
    int cy = (area.y1 + area.y2) / 2;
    int dx = pt.x - cx;
    int dy = pt.y - cy;

    const int dead = 18;
    snake_dir_t dir = (snake_dir_t)s_game.last_sent_dir;
    if (abs(dx) >= dead || abs(dy) >= dead) {
        if (abs(dx) >= abs(dy)) dir = (dx < 0) ? SNAKE_DIR_LEFT : SNAKE_DIR_RIGHT;
        else                    dir = (dy < 0) ? SNAKE_DIR_UP   : SNAKE_DIR_DOWN;
    }

    int radius = lv_obj_get_width(s_game.joy_base) / 2 - 12;
    double dist = sqrt((double)dx * dx + (double)dy * dy);
    if (dist > radius) { dx = (int)(dx * radius / dist); dy = (int)(dy * radius / dist); }

    int bw = lv_obj_get_width(s_game.joy_base);
    int bh = lv_obj_get_height(s_game.joy_base);
    int kn = lv_obj_get_width(s_game.joy_knob);
    lv_obj_set_pos(s_game.joy_knob, bw / 2 + dx - kn / 2, bh / 2 + dy - kn / 2);

    if ((int)dir != s_game.last_sent_dir) {
        s_game.last_sent_dir = (int)dir;
        if (s_game.on_dir) s_game.on_dir(dir);
    }
}

static void quit_pressed(lv_event_t *e) { (void)e; if (s_game.on_quit) s_game.on_quit(); }

lv_obj_t *ui_game_screen_create(lv_obj_t *parent, ui_quit_cb_t on_quit, ui_dir_cb_t on_dir)
{
    memset(&s_game, 0, sizeof(s_game));
    s_game.on_quit = on_quit;
    s_game.on_dir  = on_dir;
    s_game.my_id = -1;
    s_game.last_sent_dir = -1;      /* 尚未发送，首次摇杆输入必然触发 */

    lv_obj_t *scr = lv_obj_create(parent ? parent : NULL);
    lv_obj_set_size(scr, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0a1622), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    s_game.hud = lv_label_create(scr);
    lv_obj_set_size(s_game.hud, 800, 40);
    lv_obj_set_pos(s_game.hud, 0, 0);
    lv_label_set_text(s_game.hud, "Score: 0");
    lv_obj_set_style_text_font(s_game.hud, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_game.hud, lv_color_hex(0x7fc8ff), 0);
    lv_obj_set_style_pad_left(s_game.hud, 14, 0);
    lv_obj_set_style_pad_top(s_game.hud, 8, 0);

    lv_obj_t *back = lv_btn_create(scr);
    lv_obj_set_size(back, 110, 34);
    lv_obj_set_pos(back, 676, 44);
    lv_obj_set_style_radius(back, 8, 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x5a3a3a), 0);
    lv_obj_t *bl = lv_label_create(back);
    lv_label_set_text(bl, "Exit");
    lv_obj_center(bl);
    lv_obj_add_event_cb(back, quit_pressed, LV_EVENT_CLICKED, NULL);

    s_game.canvas = lv_canvas_create(scr);
    lv_canvas_set_buffer(s_game.canvas, s_canvas_buf, UI_CANVAS_W, UI_CANVAS_H, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_pos(s_game.canvas, 176, 92);

    lv_obj_t *base = lv_obj_create(scr);
    lv_obj_set_size(base, 160, 160);
    lv_obj_set_pos(base, 8, 210);
    lv_obj_set_style_radius(base, 80, 0);
    lv_obj_set_style_bg_color(base, lv_color_hex(0x22394b), 0);
    lv_obj_set_style_bg_opa(base, LV_OPA_60, 0);
    lv_obj_clear_flag(base, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(base, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *knob = lv_obj_create(base);
    lv_obj_set_size(knob, 64, 64);
    lv_obj_set_pos(knob, 48, 48);
    lv_obj_set_style_radius(knob, 32, 0);
    lv_obj_set_style_bg_color(knob, lv_color_hex(0x59d9ff), 0);
    lv_obj_set_style_bg_opa(knob, LV_OPA_90, 0);
    lv_obj_clear_flag(knob, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(knob, LV_OBJ_FLAG_CLICKABLE);   /* 不拦截触摸，确保按到底座 */
    lv_obj_set_style_shadow_width(knob, 0, 0);

    lv_obj_add_event_cb(base, joystick_pressing, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(base, joystick_release, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(base, joystick_release, LV_EVENT_PRESS_LOST, NULL);

    lv_obj_t *over = lv_obj_create(scr);
    lv_obj_set_size(over, 560, 260);
    lv_obj_align(over, LV_ALIGN_CENTER, 40, 20);
    lv_obj_set_style_bg_color(over, lv_color_hex(0x101a24), 0);
    lv_obj_set_style_bg_opa(over, LV_OPA_90, 0);
    lv_obj_set_style_radius(over, 16, 0);
    lv_obj_clear_flag(over, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(over, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *ol = lv_label_create(over);
    lv_label_set_text(ol, "Game Over");
    lv_obj_center(ol);
    lv_obj_set_style_text_font(ol, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(ol, lv_color_hex(0xffffff), 0);

    s_game.over = over;
    s_game.over_label = ol;
    s_game.joy_base = base;
    s_game.joy_knob = knob;
    return scr;
}

lv_color_t ui_palette_color(int index)
{
    switch (index % 8) {
        case 0: return lv_color_hex(0xff5b5b);
        case 1: return lv_color_hex(0x59d9ff);
        case 2: return lv_color_hex(0xffd166);
        case 3: return lv_color_hex(0x74f28c);
        case 4: return lv_color_hex(0xff8a5c);
        case 5: return lv_color_hex(0xc58cff);
        case 6: return lv_color_hex(0xf2f2f2);
        case 7: return lv_color_hex(0xff7ab8);
        default: return lv_color_hex(0xffffff);
    }
}

static void draw_board(const snake_world_t *w)
{
    lv_color_t bg    = lv_color_hex(0x0d1b26);
    lv_color_t grid  = lv_color_hex(0x1c3346);
    lv_color_t frame = lv_color_hex(0x2e6b8f);
    lv_color_t food  = lv_color_hex(0xff5252);
    int x, y, i, j;

    lv_canvas_fill_bg(s_game.canvas, bg, LV_OPA_COVER);

    lv_draw_rect_dsc_t dr;
    lv_draw_rect_dsc_init(&dr);
    dr.bg_opa = LV_OPA_COVER;
    dr.bg_color = grid;
    for (x = 0; x < UI_CANVAS_W; x += UI_CELL)
        lv_canvas_draw_rect(s_game.canvas, x, 0, 1, UI_CANVAS_H, &dr);
    for (y = 0; y < UI_CANVAS_H; y += UI_CELL)
        lv_canvas_draw_rect(s_game.canvas, 0, y, UI_CANVAS_W, 1, &dr);

    {
        lv_draw_rect_dsc_t fr;
        lv_draw_rect_dsc_init(&fr);
        fr.bg_opa = LV_OPA_TRANSP;
        fr.border_opa = LV_OPA_COVER;
        fr.border_width = 3;
        fr.border_color = frame;
        lv_canvas_draw_rect(s_game.canvas, 1, 1, UI_CANVAS_W - 2, UI_CANVAS_H - 2, &fr);
    }

    {
        lv_draw_rect_dsc_t fd;
        lv_draw_rect_dsc_init(&fd);
        fd.bg_opa = LV_OPA_COVER;
        fd.bg_color = food;
        fd.radius = 7;
        for (i = 0; i < w->food_count; i++) {
            int px = w->foods[i].x * UI_CELL + 2;
            int py = w->foods[i].y * UI_CELL + 2;
            lv_canvas_draw_rect(s_game.canvas, px, py, UI_CELL - 4, UI_CELL - 4, &fd);
        }
    }

    for (i = 0; i < w->nsnakes; i++) {
        const snake_player_t *s = &w->snakes[i];
        lv_color_t base = ui_palette_color(s->color);
        lv_draw_rect_dsc_t sd;
        lv_draw_rect_dsc_init(&sd);
        sd.bg_opa = LV_OPA_COVER;
        sd.radius = 4;
        if (s->inv > 0) {
            sd.border_opa = LV_OPA_COVER;
            sd.border_width = 2;
            sd.border_color = lv_color_hex(0xffffff);
        }
        for (j = 0; j < s->len; j++) {
            int cx = s->body[j].x, cy = s->body[j].y;
            if (cx < 0 || cx >= SNAKE_COLS || cy < 0 || cy >= SNAKE_ROWS) continue;
            int px = cx * UI_CELL, py = cy * UI_CELL;
            if (j == 0) { sd.bg_color = lv_color_mix(base, lv_color_white(), 96); sd.radius = 6; }
            else        { sd.bg_color = base; sd.radius = 4; }
            lv_canvas_draw_rect(s_game.canvas, px + 1, py + 1, UI_CELL - 2, UI_CELL - 2, &sd);
        }
    }
    lv_obj_invalidate(s_game.canvas);
}

void ui_game_update(lv_obj_t *s, const snake_world_t *w)
{
    (void)s;
    draw_board(w);
    if (w->my_id >= 0) s_game.my_id = w->my_id;

    const snake_player_t *me = NULL;
    char hud[160];
    for (int i = 0; i < w->nsnakes; i++)
        if (w->snakes[i].id == s_game.my_id) { me = &w->snakes[i]; break; }
    if (me && me->inv > 0) {
        snprintf(hud, sizeof(hud), "You: %s   Score: %d   Invincible: %ds",
                 me->name, me->score, me->inv);
    } else if (me) {
        snprintf(hud, sizeof(hud), "You: %s   Score: %d", me->name, me->score);
    } else {
        snprintf(hud, sizeof(hud), "Score: %d   Alive: %d", w->my_score, w->nsnakes);
    }
    lv_label_set_text(s_game.hud, hud);
}

void ui_game_set_over(lv_obj_t *s, const snake_world_t *w)
{
    (void)s;
    char buf[192];
    if (w->winner_id == -1) {
        snprintf(buf, sizeof(buf), "Draw!\n\nYour score: %d", w->my_score);
    } else {
        const char *wname = "?";
        for (int i = 0; i < w->nsnakes; i++)
            if (w->snakes[i].id == w->winner_id) { wname = w->snakes[i].name; break; }
        snprintf(buf, sizeof(buf), "Winner: %s\n\nYour score: %d", wname, w->my_score);
    }
    lv_label_set_text(s_game.over_label, buf);
    lv_obj_clear_flag(s_game.over, LV_OBJ_FLAG_HIDDEN);
}

void ui_game_clear_over(lv_obj_t *s)
{
    (void)s;
    if (s_game.over) lv_obj_add_flag(s_game.over, LV_OBJ_FLAG_HIDDEN);
}

void ui_game_set_my_id(int my_id)
{
    s_game.my_id = my_id;
}

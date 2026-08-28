/*
 * ui_game.c
 *
 * 游戏屏：顶部得分 + 菜单栏（退出回主菜单）+ 左侧虚拟摇杆 + Canvas 棋盘。
 *
 * 渲染：画布固定为 800×480 视口，每次状态帧刷新。
 *   - 经典地图：地图即屏幕（40×24），相机固定在原点；
 *   - 环形地图：地图远大于屏幕（80×48），相机跟随本机蛇头——蛇头固定在屏幕中心、
 *     地图滚动，只绘制视口内的实体（环形地图经"最近映像"可出现在屏幕两侧）。
 * 摇杆：拖动虚拟摇杆，把方向通过 on_dir 交给上层；松开时保持上次方向。
 * 无敌：无敌中的蛇带白色“护盾”描边；顶部显示本机剩馀无敌秒数。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "../inc/ui_page.h"
#include "../../font.h"          /* 中文字体（FreeType） */

LV_IMG_DECLARE(back);           /* LunaUI/img/back.c（200×200 黑色图标） */

#define UI_CELL        20
#define UI_CANVAS_W    800     /* 视口宽（固定，与地图大小无关） */
#define UI_CANVAS_H    480     /* 视口高 */

#define JOY_MAX_RADIUS  100     /* 摇杆头相对底座中心的最大偏移半径（px） */

typedef struct {
    lv_obj_t *canvas;
    lv_obj_t *hud;            /* 顶部分数/无敌 */
    lv_obj_t *over;           /* 本局结束遮罩 */
    lv_obj_t *over_label;
    lv_obj_t *joy_base;       /* 摇杆底（环形地图用） */
    lv_obj_t *joy_knob;       /* 摇杆头 */
    lv_obj_t *btn_up, *btn_down, *btn_left, *btn_right;  /* 4 方向键（经典地图用） */
    int   last_sent_dir;      /* 上次发送给服务端的方向；-1 表示尚未发送 */
    int   my_id;
    ui_quit_cb_t on_quit;
    ui_dir_cb_t  on_dir;
} game_state_t;
static game_state_t s_game;

static lv_color_t s_canvas_buf[UI_CANVAS_W * UI_CANVAS_H];

/* ---------------- 经典地图方向键（4 向箭头按键） ---------------- */
#define DKEY_SIZE    60      /* 方向键按钮尺寸 */
#define DKEY_ARR     36      /* 箭头画布尺寸 */
static lv_color_t s_arr_buf[4][DKEY_ARR * DKEY_ARR];

/* 方向键点击回调：把方向交给上层（仅 4 向：上/下/左/右） */
static void dir_key_pressed(lv_event_t *e)
{
    int dir = (int)(intptr_t)lv_event_get_user_data(e);
    if (dir != s_game.last_sent_dir) {
        s_game.last_sent_dir = dir;
        if (s_game.on_dir) s_game.on_dir((snake_dir_t)dir);
    }
}

/* 创建一个方向键：绿底圆角按钮 + 中央白色箭头（canvas 三角形） */
static lv_obj_t *dir_key_create(lv_obj_t *parent, int dir, int x, int y)
{
    lv_obj_t *b = lv_btn_create(parent);
    lv_obj_set_size(b, DKEY_SIZE, DKEY_SIZE);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_style_radius(b, 12, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x2e8b57), 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *cv = lv_canvas_create(b);
    lv_canvas_set_buffer(cv, s_arr_buf[dir & 3], DKEY_ARR, DKEY_ARR, LV_IMG_CF_TRUE_COLOR);
    lv_obj_center(cv);
    lv_canvas_fill_bg(cv, lv_color_hex(0x2e8b57), LV_OPA_COVER);

    /* LVGL 8.3 无 draw_triangle，用多边形填充（lv_canvas_draw_polygon + 矩形描画笔） */
    lv_draw_rect_dsc_t rd;
    lv_draw_rect_dsc_init(&rd);
    rd.bg_opa = LV_OPA_COVER;
    rd.bg_color = lv_color_hex(0xffffff);
    rd.radius = 0;
    lv_point_t pts[3];
    switch (dir) {
        case SNAKE_DIR_UP:    pts[0].x = 18; pts[0].y = 4;  pts[1].x = 6;  pts[1].y = 30; pts[2].x = 30; pts[2].y = 30; break;
        case SNAKE_DIR_DOWN:  pts[0].x = 18; pts[0].y = 32; pts[1].x = 6;  pts[1].y = 6;  pts[2].x = 30; pts[2].y = 6;  break;
        case SNAKE_DIR_LEFT:  pts[0].x = 4;  pts[0].y = 18; pts[1].x = 30; pts[1].y = 6;  pts[2].x = 30; pts[2].y = 30; break;
        default:              pts[0].x = 32; pts[0].y = 18; pts[1].x = 6;  pts[1].y = 6;  pts[2].x = 6;  pts[2].y = 30; break;
    }
    lv_canvas_draw_polygon(cv, pts, 3, &rd);

    lv_obj_add_event_cb(b, dir_key_pressed, LV_EVENT_CLICKED, (void *)(intptr_t)dir);
    return b;
}

/* ---------------- 插值渲染状态（方案B：10Hz 状态 + ~30fps 渲染） ---------------- */
static snake_world_t s_w_cur;    /* 最新一帧状态快照 */
static snake_world_t s_w_prev;   /* 上一帧状态快照（用于两帧间插值） */
static uint32_t      s_state_tick = 0;   /* 收到最新状态的时刻（lv_tick，ms） */
static int           s_has_state = 0;    /* 是否收到过状态 */
static int           s_has_prev  = 0;    /* 上一帧快照是否有效 */

static void interp_timer_cb(lv_timer_t *t);   /* 前置声明（定义在 draw_board 之后） */

/* ---------------- 摇杆 ---------------- */

/* 摇杆松开 / 触摸丢失回调：把摇杆头弹回中心。
 * 游戏逻辑上“保持上次方向”，因此松手后方向不重置，蛇继续沿原方向前进。 */
static void joystick_release(lv_event_t *e)
{
    (void)e;
    int w = lv_obj_get_width(s_game.joy_base);
    int h = lv_obj_get_height(s_game.joy_base);
    int kn = lv_obj_get_width(s_game.joy_knob);
    lv_obj_set_pos(s_game.joy_knob, w / 2 - kn / 2, h / 2 - kn / 2);
}

/* 摇杆拖动回调（LV_EVENT_PRESSING，手指移动时反复触发）：
 * 1) 计算手指相对摇杆中心的偏移 (dx, dy)；
 * 2) 偏移超过死区后按 8 个扇区判定方向（4 个基本方向 + 4 个对角线方向），
 *    并借助 s_game.last_sent_dir 去重——仅当方向改变时回调 on_dir 发给服务端；
 * 3) 把摇杆头移动到手指位置（偏移被限制在 JOY_MAX_RADIUS=100px 内）。 */
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
    int h = (abs(dx) >= dead) ? 1 : 0;   /* 水平分量超过死区 */
    int v = (abs(dy) >= dead) ? 1 : 0;   /* 垂直分量超过死区 */
    if (h || v) {
        int sx = (dx < 0) ? -1 : 1;      /* 左/右 */
        int sy = (dy < 0) ? -1 : 1;      /* 上/下 */
        if (h && v) {                    /* 对角线方向 */
            if (sx < 0) dir = (sy < 0) ? SNAKE_DIR_UP_LEFT : SNAKE_DIR_DOWN_LEFT;
            else        dir = (sy < 0) ? SNAKE_DIR_UP_RIGHT : SNAKE_DIR_DOWN_RIGHT;
        } else if (h) {
            dir = (sx < 0) ? SNAKE_DIR_LEFT : SNAKE_DIR_RIGHT;
        } else {
            dir = (sy < 0) ? SNAKE_DIR_UP : SNAKE_DIR_DOWN;
        }
    }

    const int radius = JOY_MAX_RADIUS;                       /* 摇杆头最大偏移半径 100px */
    double dist = sqrt((double)dx * dx + (double)dy * dy);
    if (dist > radius) { dx = (int)(dx * radius / dist); dy = (int)(dy * radius / dist); }

    int bw = lv_obj_get_width(s_game.joy_base);
    int bh = lv_obj_get_height(s_game.joy_base);
    int kn = lv_obj_get_width(s_game.joy_knob);
    int offset_x = bw / 2 + dx - kn / 2;
    int offset_y = bh / 2 + dy - kn / 2;
    lv_obj_set_pos(s_game.joy_knob, offset_x, offset_y);

    if ((int)dir != s_game.last_sent_dir) {
        s_game.last_sent_dir = (int)dir;
        if (s_game.on_dir) s_game.on_dir(dir);
    }
}

/* “Exit” 按钮回调：通知上层退出当前房间并返回主菜单。 */
static void quit_pressed(lv_event_t *e) { (void)e; if (s_game.on_quit) s_game.on_quit(); }

/* 创建游戏屏（800×480，视口即棋盘）：
 *   ① 顶部菜单栏（hud 标签 + Exit 按钮）：显示本机昵称/得分/名次/无敌秒数，以及退出按钮；
 *   ② 整屏棋盘（canvas）：800×480 的 LVGL 画布，经典地图绘制整张 40×24 地图，
 *      环形地图用"蛇头居中、地图滚动"的相机方式绘制 80×48 大地图的视口；
 *   ③ 左下虚拟摇杆（base 底座 + knob 摇杆头）：透明底座叠在棋盘上，拖动控制 8 方向移动。
 * 屏幕中央另有一个默认隐藏的“本局结束”遮罩（over）。
 * 返回值：游戏屏对象（由上层用 lv_scr_load() 切换到该屏）。 */
lv_obj_t *ui_game_screen_create(lv_obj_t *parent, ui_quit_cb_t on_quit, ui_dir_cb_t on_dir)
{
    memset(&s_game, 0, sizeof(s_game));
    s_game.on_quit = on_quit;
    s_game.on_dir  = on_dir;
    s_game.my_id = -1;
    s_game.last_sent_dir = -1;      /* 尚未发送，首次摇杆输入必然触发 */

    lv_obj_t *scr = lv_obj_create(parent ? parent : NULL);   /* 游戏屏根对象：深色背景铺满整屏 */
    lv_obj_set_size(scr, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0a1622), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    s_game.hud = lv_label_create(scr);                       /* 顶部菜单栏：本机昵称、得分、名次、无敌剩余秒数 */
    lv_obj_set_size(s_game.hud, 800, 100);
    lv_obj_set_pos(s_game.hud, 0, 0);
    lv_label_set_text(s_game.hud, "得分: 0");
    lv_obj_set_style_text_font(s_game.hud, luna_font_normal(), 0);   /* 24px 中文字体 */
    lv_obj_set_style_text_color(s_game.hud, lv_color_hex(0x22364a), 0);   /* 深色文字，适配米白色地图 */
    lv_obj_set_style_pad_left(s_game.hud, 14, 0);
    lv_obj_set_style_pad_top(s_game.hud, 8, 0);
    lv_obj_clear_flag(s_game.hud, LV_OBJ_FLAG_CLICKABLE); // 只显示文字，不拦截触摸事件

    lv_obj_t *exit_btn = lv_btn_create(scr);             /* 退出按钮：右上角 back 图标 */
    lv_obj_set_size(exit_btn, 44, 44);
    lv_obj_set_pos(exit_btn, 740, 20);
    lv_obj_set_style_radius(exit_btn, 22, 0);
    lv_obj_set_style_bg_color(exit_btn, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_pad_all(exit_btn, 0, 0);
    lv_obj_set_style_opa(exit_btn, 150, 0);
    lv_obj_t *back_img = lv_img_create(exit_btn);
    lv_img_set_src(back_img, &back);                     /* 44×44 原生尺寸 */
    lv_obj_center(back_img);
    lv_obj_add_event_cb(exit_btn, quit_pressed, LV_EVENT_CLICKED, NULL);

    s_game.canvas = lv_canvas_create(scr);                   /* 整屏棋盘画布：800×480 视口，绘制食物与所有蛇 */
    lv_canvas_set_buffer(s_game.canvas, s_canvas_buf, UI_CANVAS_W, UI_CANVAS_H, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_style_border_width(s_game.canvas, 0, 0);
    lv_obj_set_pos(s_game.canvas, 0, 0);

    lv_obj_t *base = lv_obj_create(scr);                     /* 摇杆底座：圆形透明区域，叠在棋盘左下，接收触摸以判定方向 */
    lv_obj_set_size(base, 300, 300);
    lv_obj_set_pos(base, 0, 180);
    lv_obj_set_style_radius(base, 150, 0);
    lv_obj_set_style_bg_color(base, lv_color_hex(0x22394b), 0);
    lv_obj_set_style_bg_opa(base, 0, 0);                     /* 透明背景，不遮挡棋盘 */
    lv_obj_set_style_pad_all(base, 0, 0);                    /* 关键：去掉默认主题的 20px 内边距，否则子控件坐标被整体偏移一个网格 */
    lv_obj_set_style_border_width(base, 0, 0);             /* 去掉边框 */
    lv_obj_clear_flag(base, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(base, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *knob = lv_obj_create(base);                    /* 摇杆头：跟随手指移动，默认位于底座中心，提供操作反馈 */
    lv_obj_set_size(knob, 40, 40);
    lv_obj_set_style_pad_all(knob, 0, 0);                    /* 去掉默认主题内边距 */
    lv_obj_set_pos(knob, 130, 130);                          /* (300-40)/2：位于 300×300 底座正中心 */
    lv_obj_set_style_radius(knob, 20, 0);
    lv_obj_set_style_bg_color(knob, lv_color_hex(0x59d9ff), 0);
    lv_obj_set_style_bg_opa(knob, LV_OPA_90, 0);
    lv_obj_set_style_border_width(knob, 0, 0);               /* 去掉边框 */
    lv_obj_clear_flag(knob, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(knob, LV_OBJ_FLAG_CLICKABLE);   /* 不拦截触摸，确保按到底座 */
    lv_obj_set_style_shadow_width(knob, 0, 0);

    lv_obj_add_event_cb(base, joystick_pressing, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(base, joystick_release, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(base, joystick_release, LV_EVENT_PRESS_LOST, NULL);

    /* 经典地图 4 方向键：左下角 D-pad 布局（默认隐藏，进入游戏后按地图类型切换） */
    s_game.btn_up    = dir_key_create(scr, SNAKE_DIR_UP,    93, 232);
    s_game.btn_left  = dir_key_create(scr, SNAKE_DIR_LEFT,  25, 292);
    s_game.btn_right = dir_key_create(scr, SNAKE_DIR_RIGHT, 161, 292);
    s_game.btn_down  = dir_key_create(scr, SNAKE_DIR_DOWN,  93, 352);
    lv_obj_add_flag(s_game.btn_up, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_game.btn_left, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_game.btn_right, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_game.btn_down, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *over = lv_obj_create(scr);                     /* 本局结束遮罩：默认隐藏，收到 over 消息后弹出 */
    lv_obj_set_size(over, 560, 260);
    lv_obj_align(over, LV_ALIGN_CENTER, 40, 20);
    lv_obj_set_style_pad_all(over, 0, 0);                    /* 去掉默认内边距，保证内部文字真正居中 */
    lv_obj_set_style_bg_color(over, lv_color_hex(0x101a24), 0);
    lv_obj_set_style_bg_opa(over, LV_OPA_90, 0);
    lv_obj_set_style_radius(over, 16, 0);
    lv_obj_clear_flag(over, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(over, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *ol = lv_label_create(over);                    /* 遮罩内文字：显示胜者昵称或 Game Over 及本机得分 */
    lv_label_set_text(ol, "游戏结束");
    lv_obj_center(ol);
    lv_obj_set_style_text_font(ol, luna_font_normal(), 0);   /* 24px 中文字体 */
    lv_obj_set_style_text_color(ol, lv_color_hex(0xffffff), 0);

    /* 菜单栏与退出按钮调到最顶层：必须在棋盘/摇杆/遮罩创建之后再调用才生效 */
    lv_obj_move_foreground(s_game.hud);
    lv_obj_move_foreground(exit_btn);

    s_game.over = over;
    s_game.over_label = ol;
    s_game.joy_base = base;
    s_game.joy_knob = knob;

    lv_timer_create(interp_timer_cb, 16, NULL);    /* 插值渲染定时器：约 30fps */
    return scr;
}

/* 调色板：根据服务端下发的 color 索引（0~7）返回蛇身颜色，
 * 同一房间内不同玩家颜色不同，便于区分。 */
lv_color_t ui_palette_color(int index)
{
    switch (index % 8) {
        case 0: return lv_color_hex(0xff5b5b);
        case 1: return lv_color_hex(0x59d9ff);
        case 2: return lv_color_hex(0xffd166);
        case 3: return lv_color_hex(0x74f28c);
        case 4: return lv_color_hex(0xff8a5c);
        case 5: return lv_color_hex(0xc58cff);
        case 6: return lv_color_hex(0x9a9a9a);   /* 深灰，米白色地图上可见 */
        case 7: return lv_color_hex(0xff7ab8);
        default: return lv_color_hex(0xffffff);
    }
}

/* 环形(相机)渲染辅助：把"世界坐标相对相机"的偏移折算到最近映像（环形地图）。
 * d 为偏移，m 为地图像素边长；返回 [-m/2, m/2) 内的最近映像偏移。 */
static int wrap_off(int d, int m)
{
    d = d % m;
    if (d > m / 2) d -= m;
    else if (d < -m / 2) d += m;
    return d;
}

/* 插值辅助：计算某条蛇第 j 个折线点（像素坐标）在插值系数 k∈[0,1] 时的位置。
 * 仅在前后两帧该蛇点数相同时被调用（点数跳变的帧由调用方改为直画本帧，见 draw_board）。
 * 环形地图下相邻两帧的同点坐标差取"最近映像"，跨边界时走最短路径。 */
static void interp_pt(const snake_player_t *pv, const snake_player_t *cur, int j,
                      float *fx, float *fy, int wrap, int mapw, int maph, float k)
{
    /* 上一帧该点缺失（蛇在生长/重生）时，退化为上一帧尾部或本帧位置 */
    int pl = (pv && pv->len > 0) ? pv->len : 0;
    int pi = (pl > 0 && j < pl) ? j : (pl > 0 ? pl - 1 : 0);
    int px = (pl > 0) ? pv->body[pi].x : cur->body[j].x;
    int py = (pl > 0) ? pv->body[pi].y : cur->body[j].y;
    int cx = cur->body[j].x, cy = cur->body[j].y;
    float dx = (float)(cx - px);
    float dy = (float)(cy - py);
    if (wrap) { dx = (float)wrap_off((int)dx, mapw); dy = (float)wrap_off((int)dy, maph); }
    *fx = px + dx * k;
    *fy = py + dy * k;
}

/* 两相邻折线点折叠到屏幕后是否"跨缝"（跨越环形地图的缝合线）。
 * 跨缝段的中间部分在视口外，整段直连会横穿屏幕，应跳过不画。 */
static int seg_straddle(int sx1, int sy1, int sx2, int sy2, int wrap, int mapw, int maph)
{
    if (!wrap) return 0;
    return (abs(sx1 - sx2) > mapw / 2 || abs(sy1 - sy2) > maph / 2);
}

/* 环形地图：世界像素坐标 -> 屏幕坐标。
 * 关键：先对"相机相对偏移"（世界坐标 - 蛇头世界坐标 = wx - cam_x - hcx）做最近映像折叠，
 * 再加上屏幕中心偏移 hcx/hcy。
 * 若直接把含 +hcx 的屏幕坐标丢进 wrap_off（阈值 mapw/2），会让蛇身在视口边缘
 * （距蛇头 mapw/2 以内、但屏幕 x 已超 mapw/2）被提前折叠到对侧，相邻折线点一个在
 * 视口内、一个跳到对侧，seg_straddle 判定"跨缝"从而跳过该段——于是右下角蛇身断开。
 * 经典地图（wrap=0）相机固定在原点，屏幕坐标 = 世界坐标 - 相机。 */
static void world_to_screen(int wx, int wy, int cam_x, int cam_y,
                            int wrap, int mapw, int maph, int hcx, int hcy,
                            int *sx, int *sy)
{
    int dx = wx - cam_x;
    int dy = wy - cam_y;
    if (wrap) {
        dx = wrap_off(dx - hcx, mapw);
        dy = wrap_off(dy - hcy, maph);
        *sx = hcx + dx;
        *sy = hcy + dy;
    } else {
        *sx = dx;
        *sy = dy;
    }
}

/* 经典地图蛇渲染：方块（body[] 为网格坐标），两帧间插值平滑移动 */
static void draw_classic_snake(const snake_player_t *s, const snake_player_t *sp, float k)
{
    int n = s->len;
    if (n > SNAKE_MAX_LEN) n = SNAKE_MAX_LEN;
    if (n < 1) return;
    lv_color_t base  = ui_palette_color(s->color);
    lv_color_t headc = lv_color_mix(base, lv_color_white(), 96);
    lv_draw_rect_dsc_t sd;
    lv_draw_rect_dsc_init(&sd);
    sd.bg_opa = LV_OPA_COVER;
    sd.radius = 4;
    if (s->inv > 0) { sd.border_opa = LV_OPA_COVER; sd.border_width = 2; sd.border_color = lv_color_hex(0xffffff); }
    for (int j = 0; j < n; j++) {
        float fx, fy;
        if (sp) interp_pt(sp, s, j, &fx, &fy, 0, 0, 0, k);   /* 经典：格坐标插值（无环形包装） */
        else { fx = (float)s->body[j].x; fy = (float)s->body[j].y; }
        int sx = (int)(fx * UI_CELL);   /* 格 -> 屏幕像素（相机固定在原点） */
        int sy = (int)(fy * UI_CELL);
        if (sx < -UI_CELL || sx >= UI_CANVAS_W || sy < -UI_CELL || sy >= UI_CANVAS_H) continue;
        if (j == 0) { sd.bg_color = headc; sd.radius = 6; }
        else        { sd.bg_color = base;  sd.radius = 4; }
        lv_canvas_draw_rect(s_game.canvas, sx + 1, sy + 1, UI_CELL - 2, UI_CELL - 2, &sd);
    }
}

/* 绘制棋盘到 800×480 视口 canvas，顺序为：
 * 背景底色 → 网格线（随相机偏移滚动）→ 边框 → 食物（小食物 4×4px 红色、
 * 大食物 16×16px 金黄，直接使用服务端下发的屏幕像素坐标）→ 所有蛇
 * （蛇头更亮且稍大；无敌中的蛇整体带白色描边）。
 *
 * 采用插值渲染：距最新状态 100ms 内，蛇的每一节在"上一帧 → 本帧"之间
 * 按最近映像线性插值，画面以 ~30fps 平滑滑动（服务器仍是 10Hz）。
 *
 * 经典地图相机固定在原点（地图即屏幕）；环形地图相机跟随"插值后"的本机
 * 蛇头（蛇头居中平滑移动、地图滚动），实体按"最近映像"折算后仅绘制视口内部分。 */
static void draw_board(void)
{
    const snake_world_t *w  = &s_w_cur;                     /* 最新状态 */
    const snake_world_t *pv = (s_has_state && s_has_prev) ? &s_w_prev : NULL;
    uint32_t dt = lv_tick_get() - s_state_tick;             /* 距最新状态的时间 */
    float k = (dt >= 100) ? 1.0f : (float)dt / 100.0f;      /* 插值系数（10Hz=100ms/帧） */
    /* 只要有前帧就恒走插值：k=0 画上一帧位置、k=1 画本帧位置，位移单调向前。
     * （若用"仅 k∈(0,1) 才插值"，帧到达瞬间 k=0 会直接画本帧，随后定时器又以
     *  k=0.33 画回上一帧方向，造成每帧前后振荡——即画面抖动。） */
    const int have_pv = (pv != NULL);

    lv_color_t bg    = lv_color_hex(0xf7f3e8);   /* 米白色地图底色 */
    lv_color_t grid  = lv_color_hex(0xe9e1cc);   /* 浅米色网格线 */
    lv_color_t food  = lv_color_hex(0xff5252);
    static int cam_x = 0, cam_y = 0;    /* 相机：屏幕左上角对应的世界像素坐标 */
    const int wrap = w->wrap;
    const int mapw = w->cols * UI_CELL;
    const int maph = w->rows * UI_CELL;
    const int hcx = UI_CANVAS_W / 2, hcy = UI_CANVAS_H / 2;   /* 蛇头屏幕位置（居中） */
    int x, y, i, j;

    if (wrap) {
        /* 环形地图：相机对准"插值后"的本机蛇头（像素坐标），蛇头始终平滑显示在屏幕中心 */
        const snake_player_t *me = NULL, *me_pv = NULL;
        for (i = 0; i < w->nsnakes; i++)
            if (w->snakes[i].id == s_game.my_id) { me = &w->snakes[i]; break; }
        if (me && me->len > 0) {
            if (have_pv) {
                for (i = 0; i < pv->nsnakes; i++)
                    if (pv->snakes[i].id == me->id && pv->snakes[i].len > 0) { me_pv = &pv->snakes[i]; break; }
                /* 点数跳变（吃食物变长/重生）时不做插值，直接对准本帧蛇头，避免塌陷 */
                if (me_pv && me_pv->len != me->len) me_pv = NULL;
            }
            float hx, hy;
            if (me_pv) interp_pt(me_pv, me, 0, &hx, &hy, wrap, mapw, maph, k);
            else { hx = (float)me->body[0].x; hy = (float)me->body[0].y; }
            cam_x = (int)hx - hcx;
            cam_y = (int)hy - hcy;
        }
    } else {
        cam_x = 0; cam_y = 0;           /* 经典地图：整张地图即屏幕 */
    }

    lv_canvas_fill_bg(s_game.canvas, bg, LV_OPA_COVER);

    /* 网格线：随相机偏移滚动，保持与地图格子对齐（相机 0 时即固定网格） */
    {
        lv_draw_rect_dsc_t dr;
        lv_draw_rect_dsc_init(&dr);
        dr.bg_opa = LV_OPA_COVER;
        dr.bg_color = grid;
        int gx = (UI_CELL - cam_x % UI_CELL) % UI_CELL;
        int gy = (UI_CELL - cam_y % UI_CELL) % UI_CELL;
        for (x = gx; x < UI_CANVAS_W; x += UI_CELL)
            lv_canvas_draw_rect(s_game.canvas, x, 0, 1, UI_CANVAS_H, &dr);
        for (y = gy; y < UI_CANVAS_H; y += UI_CELL)
            lv_canvas_draw_rect(s_game.canvas, 0, y, UI_CANVAS_W, 1, &dr);
    }

    {
        lv_draw_rect_dsc_t fr;
        lv_draw_rect_dsc_init(&fr);
        fr.bg_opa = LV_OPA_TRANSP;
        fr.border_opa = LV_OPA_COVER;
        fr.border_width = 0;
        lv_canvas_draw_rect(s_game.canvas, 1, 1, UI_CANVAS_W - 2, UI_CANVAS_H - 2, &fr);
    }

    /* 小食物：4×4px，世界像素 -> 屏幕，视口外裁剪（食物静态，不插值） */
    {
        lv_draw_rect_dsc_t fd;
        lv_draw_rect_dsc_init(&fd);
        fd.bg_opa = LV_OPA_COVER;
        fd.bg_color = food;
        fd.radius = 1;
        for (i = 0; i < w->food_count; i++) {
            if (w->foods[i].kind != 0) continue;
            int sx, sy;
            world_to_screen(w->foods[i].x, w->foods[i].y, cam_x, cam_y, wrap, mapw, maph, hcx, hcy, &sx, &sy);
            if (sx < 0 || sx >= UI_CANVAS_W || sy < 0 || sy >= UI_CANVAS_H) continue;
            lv_canvas_draw_rect(s_game.canvas, sx, sy, SNAKE_SMALL_FOOD_SIZE, SNAKE_SMALL_FOOD_SIZE, &fd);
        }
    }

    /* 大食物：16×16px 金黄色圆角方块，世界像素 -> 屏幕，视口外裁剪 */
    {
        lv_draw_rect_dsc_t bd;
        lv_draw_rect_dsc_init(&bd);
        bd.bg_opa = LV_OPA_COVER;
        bd.bg_color = lv_color_hex(0xffd166);
        bd.radius = 6;
        bd.border_opa = LV_OPA_COVER;
        bd.border_width = 2;
        bd.border_color = lv_color_hex(0xfff3b0);
        for (i = 0; i < w->food_count; i++) {
            if (w->foods[i].kind != 1) continue;
            int sx, sy;
            world_to_screen(w->foods[i].x, w->foods[i].y, cam_x, cam_y, wrap, mapw, maph, hcx, hcy, &sx, &sy);
            if (sx < 0 || sx >= UI_CANVAS_W || sy < 0 || sy >= UI_CANVAS_H) continue;
            lv_canvas_draw_rect(s_game.canvas, sx, sy, SNAKE_BIG_FOOD_SIZE, SNAKE_BIG_FOOD_SIZE, &bd);
        }
    }

    /* 蛇：连续轨迹折线（像素坐标）。逐段画圆头粗线，并在各折点补圆点使拐角
     * 圆润、蛇身连续；无敌中的蛇先画白色粗线做护盾描边。跨环形缝合线的段跳过。 */
    for (i = 0; i < w->nsnakes; i++) {
        const snake_player_t *s = &w->snakes[i];
        const snake_player_t *sp = NULL;                 /* 上一帧中的同一玩家（无则本帧直画） */
        if (have_pv) {
            for (j = 0; j < pv->nsnakes; j++)
                if (pv->snakes[j].id == s->id && pv->snakes[j].len > 0) { sp = &pv->snakes[j]; break; }
            /* 点数跳变（吃食物变长/重生）时不做插值，直接画本帧蛇身。
             * 原因：环形轨迹点是"头插"的（每帧蛇头前进、新点插入 body[0]、旧点右移），
             * 点数变化的帧里按索引逐点插值会让"新尾点"与"上帧尾点"重叠，蛇身视觉
             * 突然塌陷一截再恢复；变长帧直画则蛇只是干净地长出一节。 */
            if (sp && sp->len != s->len) sp = NULL;
        }
        if (!wrap) { draw_classic_snake(s, sp, k); continue; }   /* 经典：方块 */
        int n = s->len;
        if (n > SNAKE_MAX_LEN) n = SNAKE_MAX_LEN;
        if (n < 1) continue;

        /* 全部折线点插值并折叠到屏幕坐标 */
        static lv_point_t pts[SNAKE_MAX_LEN];
        for (j = 0; j < n; j++) {
            float fx, fy;
            if (sp) interp_pt(sp, s, j, &fx, &fy, wrap, mapw, maph, k);
            else { fx = (float)s->body[j].x; fy = (float)s->body[j].y; }
            world_to_screen((int)fx, (int)fy, cam_x, cam_y, wrap, mapw, maph, hcx, hcy, &pts[j].x, &pts[j].y);
        }

        lv_color_t base  = ui_palette_color(s->color);
        lv_color_t headc = lv_color_mix(base, lv_color_white(), 96);
        const int inv = (s->inv > 0);

        /* 无敌护盾：白色粗线描边（画在主体线之下） */
        if (inv) {
            lv_draw_line_dsc_t wl;
            lv_draw_line_dsc_init(&wl);
            wl.color = lv_color_hex(0xffffff);
            wl.width = 20; wl.opa = LV_OPA_COVER; wl.round_start = 1; wl.round_end = 1;
            for (j = 0; j + 1 < n; j++) {
                if (seg_straddle(pts[j].x, pts[j].y, pts[j + 1].x, pts[j + 1].y, wrap, mapw, maph)) continue;
                lv_canvas_draw_line(s_game.canvas, &pts[j], 2, &wl);
            }
        }
        /* 主体：逐段画圆头粗线（头段更亮） */
        {
            lv_draw_line_dsc_t ld;
            lv_draw_line_dsc_init(&ld);
            ld.width = 14; ld.opa = LV_OPA_COVER; ld.round_start = 1; ld.round_end = 1;
            for (j = 0; j + 1 < n; j++) {
                if (seg_straddle(pts[j].x, pts[j].y, pts[j + 1].x, pts[j + 1].y, wrap, mapw, maph)) continue;
                ld.color = (j == 0) ? headc : base;
                lv_canvas_draw_line(s_game.canvas, &pts[j], 2, &ld);
            }
        }
        /* 折点补圆 + 蛇头圆：让拐角圆润、蛇身视觉连续 */
        {
            lv_draw_rect_dsc_t cd;
            lv_draw_rect_dsc_t eye;
            lv_draw_rect_dsc_init(&cd);
            lv_draw_rect_dsc_init(&eye);
            cd.bg_opa = LV_OPA_COVER;
            cd.border_opa = LV_OPA_TRANSP;
            
            eye.bg_opa = LV_OPA_COVER;
            for (j = n - 1; j >= 0; j--) {
                if (pts[j].x < -40 || pts[j].x > (UI_CANVAS_W + 40) ||
                    pts[j].y < -40 || pts[j].y > (UI_CANVAS_H + 40)) continue; // 视口外不画
                if (j == 0) {
                    cd.radius = 9;  cd.bg_color = headc;
                    eye.radius = 7; 
                    lv_canvas_draw_rect(s_game.canvas, pts[j].x - 9, pts[j].y - 9, 18, 18, &cd);
                    eye.bg_color = lv_color_hex(0x00000000);
                    lv_canvas_draw_rect(s_game.canvas, pts[j].x - 7, pts[j].y - 7, 14, 14, &eye);
                    eye.bg_color = lv_color_hex(0x00ffffff);
                    lv_canvas_draw_rect(s_game.canvas, pts[j].x, pts[j].y, 2, 2, &eye);
                } else {
                    cd.radius = 7;  cd.bg_color = base;
                    lv_canvas_draw_rect(s_game.canvas, pts[j].x - 7, pts[j].y - 7, 14, 14, &cd);
                }
            }
        }
    }
    lv_obj_invalidate(s_game.canvas);
}

/* 插值渲染定时器：两次状态帧之间高频重绘，让蛇身平滑滑动（约 30fps）。
 * 仅在游戏屏激活且距最新状态不足一帧时重绘，避免无谓开销。 */
static void interp_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (!s_game.canvas) return;
    if (lv_scr_act() != lv_obj_get_screen(s_game.canvas)) return;  /* 非游戏屏不重绘 */
    if (!s_has_state) return;
    if (lv_tick_get() - s_state_tick >= 100) return;              /* 已到最终位置 */
    draw_board();
}

/* 刷新游戏屏：用最新世界状态重画棋盘，并更新顶部菜单栏文字
 * （本机昵称、得分、名次 rank/总人数、无敌剩余秒数）。
 * 每收到一帧服务端广播的 state 消息都会调用一次。 */
void ui_game_update(lv_obj_t *s, const snake_world_t *w)
{
    (void)s;
    /* 保存前后两帧快照，供插值渲染平滑滑动 */
    if (s_has_state) { s_w_prev = s_w_cur; s_has_prev = 1; }
    s_w_cur = *w;
    s_has_state = 1;
    s_state_tick = lv_tick_get();
    draw_board();
    if (w->my_id >= 0) s_game.my_id = w->my_id;

    /* 控制方式按地图切换：经典 = 4 方向键；环形 = 虚拟摇杆 */
    {
        int arrows = !w->wrap, joy = w->wrap;
        if (s_game.btn_up) {
            if (arrows) { lv_obj_clear_flag(s_game.btn_up, LV_OBJ_FLAG_HIDDEN);
                          lv_obj_clear_flag(s_game.btn_down, LV_OBJ_FLAG_HIDDEN);
                          lv_obj_clear_flag(s_game.btn_left, LV_OBJ_FLAG_HIDDEN);
                          lv_obj_clear_flag(s_game.btn_right, LV_OBJ_FLAG_HIDDEN); }
            else        { lv_obj_add_flag(s_game.btn_up, LV_OBJ_FLAG_HIDDEN);
                          lv_obj_add_flag(s_game.btn_down, LV_OBJ_FLAG_HIDDEN);
                          lv_obj_add_flag(s_game.btn_left, LV_OBJ_FLAG_HIDDEN);
                          lv_obj_add_flag(s_game.btn_right, LV_OBJ_FLAG_HIDDEN); }
        }
        if (s_game.joy_base) {
            if (joy) lv_obj_clear_flag(s_game.joy_base, LV_OBJ_FLAG_HIDDEN);
            else     lv_obj_add_flag(s_game.joy_base, LV_OBJ_FLAG_HIDDEN);
        }
    }

    const snake_player_t *me = NULL;
    int i, rank = 1;
    char hud[192];
    for (i = 0; i < w->nsnakes; i++)
        if (w->snakes[i].id == s_game.my_id) { me = &w->snakes[i]; break; }
    if (me) {
        /* 名次：得分高于我的蛇数 + 1（得分高者名次靠前） */
        for (i = 0; i < w->nsnakes; i++)
            if (w->snakes[i].id != me->id && w->snakes[i].score > me->score) rank++;
        if (me->inv > 0) {
            snprintf(hud, sizeof(hud), "玩家: %s  得分: %d  名次: %d/%d  食物: %d/%d  无敌: %ds  地图: %s",
                     me->name, me->score, rank, w->nsnakes,
                     me->small_eaten, SNAKE_LEN_PER_SMALL, me->inv,
                     w->wrap ? "环形" : "经典");
        } else {
            snprintf(hud, sizeof(hud), "玩家: %s  得分: %d  名次: %d/%d  食物: %d/%d  地图: %s",
                     me->name, me->score, rank, w->nsnakes,
                     me->small_eaten, SNAKE_LEN_PER_SMALL,
                     w->wrap ? "环形" : "经典");
        }
    } else {
        snprintf(hud, sizeof(hud), "得分: %d  存活: %d", w->my_score, w->nsnakes);
    }
    lv_label_set_text(s_game.hud, hud);
}

/* 显示“本局结束”遮罩：有胜者时显示 "Winner: 昵称"，
 * 无人存活（平局 / 单人失败）时显示 "Game Over"，并附带本机得分。 */
void ui_game_set_over(lv_obj_t *s, const snake_world_t *w)
{
    (void)s;
    char buf[192];
    if (w->winner_id == -1) {
        snprintf(buf, sizeof(buf), "游戏结束\n\n你的得分: %d", w->my_score);
    } else {
        const char *wname = "?";
        for (int i = 0; i < w->nsnakes; i++)
            if (w->snakes[i].id == w->winner_id) { wname = w->snakes[i].name; break; }
        snprintf(buf, sizeof(buf), "胜者: %s\n\n你的得分: %d", wname, w->my_score);
    }
    lv_label_set_text(s_game.over_label, buf);
    lv_obj_clear_flag(s_game.over, LV_OBJ_FLAG_HIDDEN);
}

/* 隐藏“本局结束”遮罩（服务端广播 round 开启新一局时调用）。 */
void ui_game_clear_over(lv_obj_t *s)
{
    (void)s;
    if (s_game.over) lv_obj_add_flag(s_game.over, LV_OBJ_FLAG_HIDDEN);
}

/* 记录本机玩家 id，用于在世界状态中识别“我的蛇”（统计得分与名次）。 */
void ui_game_set_my_id(int my_id)
{
    s_game.my_id = my_id;
}

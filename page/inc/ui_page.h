/*
 * ui_page.h
 *
 * 贪吃蛇客户端 —— 界面（page）模块对外接口
 *
 * 界面流程：用户名屏 → 主菜单（单人/多人）→ 多人房间选择 → 游戏屏
 * 本模块只负责界面布局，通过与上层（snakeInit.c）的回调解耦。
 */

#ifndef LUNAUI_UI_PAGE_H
#define LUNAUI_UI_PAGE_H

#include "../../../lvgl/lvgl.h"
#include "../../snake/snake.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- 回调类型 ---------------- */
typedef void (*ui_name_cb_t)(const char *name);         /* 用户名屏"开始" */
typedef void (*ui_mode_cb_t)(const char *mode);         /* 主菜单：single / multi */
typedef void (*ui_map_cb_t)(int wrap);                  /* 主菜单：地图选择 0=经典, 1=环形 */
typedef void (*ui_room_cb_t)(int action, int room_id);  /* 房间屏操作 */
typedef void (*ui_quit_cb_t)(void);                     /* 游戏屏"退出"回菜单 */
typedef void (*ui_dir_cb_t)(snake_dir_t dir);           /* 摇杆方向 */

/* 房间屏操作动作 */
#define UI_ROOM_CREATE 0
#define UI_ROOM_JOIN   1
#define UI_ROOM_RANDOM 2
#define UI_ROOM_BACK   3

/* ---------------- 用户名屏 ---------------- */
lv_obj_t *ui_name_screen_create(lv_obj_t *parent, ui_name_cb_t on_name, const char *def_name);
void      ui_name_set_status(lv_obj_t *s, const char *msg);

/* ---------------- 主菜单屏 ---------------- */
lv_obj_t *ui_main_screen_create(lv_obj_t *parent, ui_mode_cb_t on_mode, ui_map_cb_t on_map);
void      ui_main_set_status(lv_obj_t *s, const char *msg);

/* ---------------- 多人房间屏 ---------------- */
lv_obj_t *ui_room_screen_create(lv_obj_t *parent, ui_room_cb_t cb);
/* 刷新房间列表；ids/players/maxs/wraps 长度均为 n，wraps: 0=经典, 1=环形 */
void      ui_room_refresh(lv_obj_t *s, const int ids[], const int players[],
                          const int maxs[], const int wraps[], int n);
void      ui_room_set_status(lv_obj_t *s, const char *msg);

/* ---------------- 游戏屏 ---------------- */
lv_obj_t *ui_game_screen_create(lv_obj_t *parent, ui_quit_cb_t on_quit, ui_dir_cb_t on_dir);
void      ui_game_update(lv_obj_t *s, const snake_world_t *w);
void      ui_game_set_over(lv_obj_t *s, const snake_world_t *w);
void      ui_game_clear_over(lv_obj_t *s);
void      ui_game_set_my_id(int my_id);

/* ---------------- 颜色 ---------------- */
lv_color_t ui_palette_color(int color_index);

#ifdef __cplusplus
}
#endif

#endif /* LUNAUI_UI_PAGE_H */

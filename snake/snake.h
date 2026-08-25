/*
 * snake.h
 *
 * 贪吃蛇客户端 —— “蛇结构”数据模型
 *
 * 定义一条/多条蛇、食物、以及整个世界状态的数据结构，
 * 并把服务端下发的 JSON STATE 帧解析为本地世界状态，供界面层渲染使用。
 *
 * 说明：
 *   - 服务端是权威端；客户端只负责渲染服务端下发的状态，不本地推进游戏。
 *   - STATE 里每帧只包含“当前存活”的蛇；死亡的蛇不会再出现。
 *   - 每条蛇带“无敌”剩余秒数（inv），>0 表示仍处出生无敌期。
 */

#ifndef LUNAUI_SNAKE_H
#define LUNAUI_SNAKE_H

#include "../net/protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 方向（供界面层/摇杆使用） */
typedef enum {
    SNAKE_DIR_UP = 0,
    SNAKE_DIR_DOWN,
    SNAKE_DIR_LEFT,
    SNAKE_DIR_RIGHT
} snake_dir_t;

typedef struct { int x, y; } snake_point_t;

/* 一条蛇 */
typedef struct {
    int   id;                          /* 玩家 id */
    char  name[SNAKE_MAX_NAME + 1];
    int   color;                       /* 调色板索引 */
    int   len;                         /* 身节数 */
    int   score;                       /* 得分 */
    int   inv;                         /* 剩余无敌秒数（0=无） */
    int   alive;                       /* 本帧是否存活 */
    snake_point_t body[SNAKE_MAX_LEN];
} snake_player_t;

/* 整个世界状态（一帧快照） */
typedef struct {
    int              tick;
    snake_point_t    foods[SNAKE_FOOD_COUNT];
    int              food_count;
    int              nsnakes;
    snake_player_t   snakes[SNAKE_MAX_PER_ROOM];
    int              my_id;        /* 本机玩家 id */
    int              my_score;     /* 本机最新得分 */
    int              winner_id;    /* 最近一局胜者，-1 表示平局 */
    int              game_over;    /* 1 = 刚收到本局结束 */
    int              room;         /* 房间号 */
} snake_world_t;

void snake_world_init(snake_world_t *w);

/* 解析一帧 JSON 格式的 STATE。
 * @return 0 成功；-1 格式不合法（界面层应忽略该帧）。
 * 注意：json 为只读字符串。 */
int snake_parse_state(snake_world_t *w, const char *json);

/* 本局重置：清空蛇、食物、胜负标记（保留 my_id / room） */
void snake_world_reset_round(snake_world_t *w);

#ifdef __cplusplus
}
#endif

#endif /* LUNAUI_SNAKE_H */

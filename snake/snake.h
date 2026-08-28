/*
 * snake.h
 *
 * 贪吃蛇客户端 —— "蛇结构"数据模型
 *
 * 定义一条/多条蛇、食物、以及整个世界状态的数据结构，
 * 并把服务端下发的二进制 STATE 帧解析为本地世界状态，供界面层渲染使用。
 *
 * 说明：
 *   - 服务端是权威端；客户端只负责渲染服务端下发的状态，不本地推进游戏。
 *   - STATE 里每帧只包含"当前存活"的蛇；死亡的蛇不会再出现。
 *   - 每条蛇带"无敌"剩余秒数（inv），>0 表示仍处出生无敌期。
 *   - 食物直接使用屏幕像素坐标（左上角）：小食物 4×4px（kind=0）、
 *     大食物 16×16px（kind=1），客户端按坐标直接绘制，不再做网格换算。
 *   - 蛇身 body 为"轨迹折线"像素坐标（连续移动模型）：body[0]=蛇头中心，
 *     后续点为蛇头走过的轨迹，相邻两点沿路径间距约 20px；环形地图坐标已
 *     包装（含跨边点），经典地图为地图内坐标。len 为折线点数。
 *   - small_eaten 为本机累计吃掉的小食物数（每 10 个长度 +1），用于 HUD 显示。
 */

#ifndef LUNAUI_SNAKE_H
#define LUNAUI_SNAKE_H

#include "../net/protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 方向（供界面层/摇杆使用）：4 个基本方向 + 4 个对角线方向 */
typedef enum {
    SNAKE_DIR_UP = 0,
    SNAKE_DIR_DOWN,
    SNAKE_DIR_LEFT,
    SNAKE_DIR_RIGHT,
    SNAKE_DIR_UP_LEFT,
    SNAKE_DIR_UP_RIGHT,
    SNAKE_DIR_DOWN_LEFT,
    SNAKE_DIR_DOWN_RIGHT
} snake_dir_t;

typedef struct { int x, y; } snake_point_t;

/* 一个食物：x/y 为屏幕像素坐标（左上角），kind=0 小食物，kind=1 大食物 */
typedef struct {
    int x, y;
    int kind;
} snake_food_t;

/* 一条蛇（阶段C：身体为轨迹折线，像素坐标） */
typedef struct {
    int   id;                          /* 玩家 id */
    char  name[SNAKE_MAX_NAME + 1];
    int   color;                       /* 调色板索引 */
    int   len;                         /* 折线点数（body[] 有效点数） */
    int   score;                       /* 得分 */
    int   inv;                         /* 剩余无敌秒数（0=无） */
    int   small_eaten;                 /* 已累计吃掉的小食物数（每 10 个长度 +1） */
    int   alive;                       /* 本帧是否存活 */
    snake_point_t body[SNAKE_MAX_LEN]; /* 轨迹折线点（像素坐标），body[0]=蛇头中心 */
} snake_player_t;

/* 整个世界状态（一帧快照） */
typedef struct {
    int              tick;
    snake_food_t     foods[SNAKE_FOOD_MAX];
    int              food_count;
    int              nsnakes;
    snake_player_t   snakes[SNAKE_MAX_PER_ROOM];
    int              my_id;        /* 本机玩家 id */
    int              my_score;     /* 本机最新得分 */
    int              winner_id;    /* 最近一局胜者，-1 表示平局 */
    int              game_over;    /* 1 = 刚收到本局结束 */
    int              room;         /* 房间号 */
    int              wrap;         /* 地图类型：0=经典(撞墙死), 1=环形(穿墙) */
    int              cols, rows;   /* 本房间地图网格尺寸（环形地图远大于屏幕，客户端相机渲染） */
} snake_world_t;

void snake_world_init(snake_world_t *w);

/* 解析一帧二进制 STATE（负载，不含帧头）。
 * @return 0 成功；-1 格式不合法（界面层应忽略该帧）。 */
int snake_parse_state(snake_world_t *w, const uint8_t *payload, int plen);

/* 本局重置：清空蛇、食物、胜负标记（保留 my_id / room） */
void snake_world_reset_round(snake_world_t *w);

#ifdef __cplusplus
}
#endif

#endif /* LUNAUI_SNAKE_H */

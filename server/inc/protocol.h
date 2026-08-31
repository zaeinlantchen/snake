/*
 * protocol.h
 *
 * 贪吃蛇多人联机游戏 —— 客户端 / 服务端通信协议定义（纯二进制）
 *
 * 本文件同时被 server/src/server.c 与 LunaUI/net/net.c 使用（各自副本），
 * 两边必须保持完全一致，否则无法互通。
 *
 * ------------------------------------------------------------------
 * 传输层：TCP
 * 消息格式：每条消息为一个二进制帧：
 *
 *     [ 1 字节 类型 ][ 2 字节 负载长度(大端) ][ 负载 ]
 *
 * 即帧头 3 字节（PROTO_HEADER_LEN），负载长度不含帧头，最大
 * PROTO_MAX_PAYLOAD。负载内多字节整数一律大端序，请用 pr_put_u16/pr_get_u16 等读写。
 * ------------------------------------------------------------------
 *
 *  客户端 -> 服务端
 *    MSG_JOIN(1)        负载: name[16]（不足补 0）   注册用户名；随后服务端回 welcome
 *    MSG_MODE(2)        负载: mode u8, wrap u8       选择模式并携带地图偏好
 *                        mode: 0=single, 1=multi；wrap: 0=经典(撞墙死), 1=环形(穿墙)
 *                        （wrap 仅在创建单人/新房间时生效，加入已有房间以房间为准）
 *    MSG_ROOM_LIST(3)   无负载                        请求房间列表
 *    MSG_CREATE_ROOM(4) 负载: wrap u8                创建新房间（多人），wrap: 0=经典, 1=环形
 *    MSG_JOIN_ROOM(5)   负载: room u32               按房间号加入
 *    MSG_RANDOM_JOIN(6) 无负载                        随机加入一个有空的房间
 *    MSG_DIR(7)         负载: dir u8 (0..7)          设置方向（不能 180° 反转）
 *    MSG_LEAVE(8)       无负载                        离开房间，回到主菜单（保持连接）
 *    MSG_BYE(9)         无负载                        断开连接（可选）
 *    MSG_USE_SKILL(10)  负载: skill u8                使用技能（0=加速, 1=护盾），
 *                                                仅环形地图；须已持有该技能，用后进入效果期
 *
 *  服务端 -> 客户端
 *    MSG_WELCOME(20)   负载: id u32, cols u16, rows u16
 *    MSG_ROOMS(21)     负载: n u8，随后 n 个 { id u32, players u8, max u8, wrap u8 }
 *    MSG_ROOM(22)      负载: room u32, mode u8, wrap u8, cols u16, rows u16
 *                       wrap: 0=经典(撞墙死), 1=环形(穿墙)
 *                       cols/rows: 该房间地图的网格尺寸（客户端按此做相机渲染）
 *    MSG_STATE(23)     负载: 见下方 "STATE 帧格式"
 *    MSG_OVER(24)      负载: winner i32（-1 平局/无人存活）
 *    MSG_ROUND(25)     无负载                        新一局开始
 *    MSG_ERROR(26)     负载: 以 '\0' 结尾的错误字符串
 *
 *  STATE 帧格式（全部大端）:
 *    tick         u32
 *    small_n      u16                      小食物数量
 *    small_n ×    { px u16, py u16 }       小食物左上角像素坐标（4×4px）
 *    big_n        u8                       大食物数量
 *    big_n ×      { px u16, py u16 }       大食物左上角像素坐标（16×16px）
 *    pickup_n     u8                       技能道具数量（保留字段，恒为 0：技能改为吃食物获得）
 *    nsnakes      u8
 *    nsnakes ×    {
 *        id u32, color u8, len u16, score u16, inv u8, small_eaten u8,
 *        skills u8, speed_ticks u8, shield_ticks u8,
 *        name[16],
 *        body[ len × { x u16, y u16 } ]    蛇身折线点像素坐标
 *    }
 *    技能说明（仅环形地图）：
 *    - 技能获取：不再地图拾取，改为吃食物获得——每吃 40 个小食物获得 1 次护盾、
 *      每吃 3 个大食物获得 1 次加速；每个技能最多同时持有 1 个（skills 对应位=1），
 *      直到该技能被使用且效果结束（对应 ticks 归 0）后才能再次获得；
 *    - skills 位图：bit0=持加速, bit1=持护盾（获得、未使用）；
 *    - speed_ticks / shield_ticks：对应技能效果剩余 ticks（>0 表示激活中，用完即失效）。
 *    蛇身说明（连续移动模型，像素级）：
 *    - 蛇头像素级连续前进（每 TICK_MS 前进 20px），身体 = 蛇头走过的折线轨迹；
 *    - body[0] 为蛇头（像素坐标，中心点），后续点为轨迹，相邻两点沿路径间距约 20px；
 *    - len 为折线点数（逻辑长度以格为单位，初始 3，每吃 10 小/1 大 +1；
 *      折线点数 = min(len, 服务端保留的历史轨迹点数)）；
 *    - 环形地图坐标为"已包装"（0..cols*20-1），跨地图边界时服务端自动插入边界点，
 *      相邻两点距离恒 ≤ 20px，客户端逐段连线即可正确渲染；
 *      经典地图为地图内坐标（撞墙即死，无包装）。
 * ------------------------------------------------------------------
 *
 *  坐标约定：
 *   - 蛇身 body 为像素坐标折线点（连续移动模型）：body[0]=蛇头中心，相邻点间距约 20px；
 *     环形地图为已包装坐标（含跨边点），经典地图为地图内坐标。
 *     地图类型由房间决定（wrap）：经典地图撞墙即死且不铺小食物；环形地图越界从对侧
 *     出现且撞自身不判死。服务端权威计算，客户端只渲染。
 *   - 环形地图远大于屏幕：客户端用"蛇头居中、地图滚动"的相机方式渲染（视口裁剪），
 *     因此 MSG_ROOM 携带该房间的地图尺寸。
 * ------------------------------------------------------------------
 */

#ifndef SNAKE_PROTOCOL_H
#define SNAKE_PROTOCOL_H

#include <stdint.h>
#include <string.h>

/* ---------------- 二进制消息类型 ---------------- */
#define MSG_JOIN        1
#define MSG_MODE        2
#define MSG_ROOM_LIST   3
#define MSG_CREATE_ROOM 4
#define MSG_JOIN_ROOM   5
#define MSG_RANDOM_JOIN 6
#define MSG_DIR         7
#define MSG_LEAVE       8
#define MSG_BYE         9
#define MSG_USE_SKILL   10

#define MSG_WELCOME     20
#define MSG_ROOMS       21
#define MSG_ROOM        22
#define MSG_STATE       23
#define MSG_OVER        24
#define MSG_ROUND       25
#define MSG_ERROR       26

/* 客户端 net 模块在连接断开时通过 net_poll_msg() 返回的“类型”（内部信令） */
#define MSG_NETCLOSED   255

/* ---------------- 帧格式 ---------------- */
#define PROTO_HEADER_LEN   3       /* 帧头：类型(1) + 负载长度(2, 大端) */
#define PROTO_MAX_PAYLOAD  16384   /* 单帧负载最大字节数 */

/* ---------------- 模式 ---------------- */
#define MODE_SINGLE  "single"
#define MODE_MULTI   "multi"

/* ---------------- 地图 / 游戏常量 ---------------- */
#define SERVER_PORT          5000
#define SNAKE_CELL_PX        20      /* 网格边长（像素） */
#define SNAKE_COLS           80      /* 环形地图网格列数（宽 1600px，客户端按视口渲染） */
#define SNAKE_ROWS           48      /* 环形地图网格行数（高 960px） */
#define SNAKE_CLASSIC_COLS   40      /* 经典地图网格列数（宽 800px，整屏显示） */
#define SNAKE_CLASSIC_ROWS   24      /* 经典地图网格行数（高 480px） */
#define SNAKE_SMALL_FOOD_MAX 960     /* 小食物目标数量（环形地图随机稀疏分布，非铺满） */
#define SNAKE_BIG_FOOD_MAX   8       /* 大食物最大数量 */
#define SNAKE_SMALL_FOOD_SIZE 4      /* 小食物像素尺寸 4×4 */
#define SNAKE_BIG_FOOD_SIZE  16      /* 大食物像素尺寸 16×16 */
#define SNAKE_LEN_PER_SMALL  10      /* 每累计吃 10 个小食物长度 +1 */
#define SNAKE_REFRESH_TICKS  100     /* 小食物刷新间隔（服务端用，10s） */
#define SNAKE_BIGGEN_TICKS   50      /* 大食物生成间隔（服务端用，5s） */
#define SNAKE_FOOD_MAX       (SNAKE_SMALL_FOOD_MAX + SNAKE_BIG_FOOD_MAX)
#define SNAKE_MAX_LEN        256     /* 单条蛇最大身节数 */
#define SNAKE_MAX_NAME       16      /* 昵称最大长度（不含 '\0'） */
#define SNAKE_MAX_PER_ROOM   8       /* 单个房间最大玩家数 */
#define SNAKE_INV_TICKS      80      /* 出生无敌持续时间（80 * TICK_MS = 8s） */

/* ---------------- 技能（仅环形地图） ---------------- */
#define SKILL_SPEED         0       /* 技能类型：加速 */
#define SKILL_SHIELD        1       /* 技能类型：护盾 */
#define SKILL_BIT_SPEED     0x01    /* 持有/激活位图：加速 */
#define SKILL_BIT_SHIELD    0x02    /* 持有/激活位图：护盾 */
#define SNAKE_SMALL_PER_SHIELD 40   /* 每吃 40 个小食物获得一次护盾 */
#define SNAKE_BIG_PER_SPEED    3    /* 每吃 3 个大食物获得一次加速 */
#define SNAKE_PICKUP_MAX    4       /* 技能道具保留数组上限（pickup_n 恒为 0，不再使用） */
#define SKILL_SPEED_TICKS   50      /* 加速效果持续（50 * TICK_MS = 5s） */
#define SKILL_SHIELD_TICKS  50      /* 护盾效果持续（50 * TICK_MS = 5s） */

/* ---------------- 服务器接收缓冲（帧式解析用） ---------------- */
#define PROTO_LINE_MAX       2048

/* ---------------- 二进制读写小工具（大端） ---------------- */
static inline void pr_put_u8(uint8_t *p, uint8_t v)          { p[0] = v; }
static inline uint8_t pr_get_u8(const uint8_t *p)            { return p[0]; }
static inline void pr_put_u16(uint8_t *p, uint16_t v)
{ p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)(v & 0xff); }
static inline uint16_t pr_get_u16(const uint8_t *p)
{ return (uint16_t)(((uint16_t)p[0] << 8) | p[1]); }
static inline void pr_put_u32(uint8_t *p, uint32_t v)
{ p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static inline uint32_t pr_get_u32(const uint8_t *p)
{ return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static inline void pr_put_i32(uint8_t *p, int32_t v)         { pr_put_u32(p, (uint32_t)v); }
static inline int32_t pr_get_i32(const uint8_t *p)           { return (int32_t)pr_get_u32(p); }

#endif /* SNAKE_PROTOCOL_H */

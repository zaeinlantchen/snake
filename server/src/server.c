/*
 * server.c
 *
 * 贪吃蛇多人联机游戏 —— 服务端（权威端，纯二进制协议）
 *
 * 负责：
 *   建立房间（单人/多人）、接受玩家加入（房间号 / 随机）、收发二进制帧、
 *   持有全部游戏逻辑（移动、吃食物、碰撞、无敌时间、得分、胜负）、
 *   按固定帧率把每个房间的世界状态广播给该房间所有玩家。
 *
 * 玩法要点（阶段C：连续移动模型，像素级）：
 *   - 地图类型（wrap）由房间决定：
 *       经典：40×24 撞墙即死，不铺小食物（只有定时生成的大食物）；
 *       环形：80×48 大地图，越界从对侧出现，且撞自身不判死（撞其它蛇仍判死）；
 *       客户端在环形地图用"蛇头居中、地图滚动"的相机方式渲染视口；
 *   - 蛇移动：蛇头每帧（TICK_MS）沿当前方向前进 20px（像素级连续），
 *     身体 = 蛇头走过的折线轨迹（20px 步距，环形地图坐标已包装并含跨边点）；
 *   - 小食物：4×4px，仅环形地图，整张地图随机稀疏分布（目标 SNAKE_SMALL_FOOD_MAX 个），
 *     每隔 SNAKE_REFRESH_TICKS 刷新一次；
 *   - 大食物：16×16px，两种地图每隔 SNAKE_BIGGEN_TICKS 在随机空位生成少量（上限 SNAKE_BIG_FOOD_MAX）；
 *   - 加长：每累计吃 10 个小食物，或吃 1 个大食物，蛇逻辑长度 +1（身体折线随之变长）；
 *   - 技能（仅环形地图）：不再地图拾取，改为吃食物获得——每吃 40 个小食物获得 1 次护盾、
 *     每吃 3 个大食物获得 1 次加速；每个技能最多同时持有 1 个，使用且效果结束后才能再次获得。
 *
 * 运行平台：Linux（gcc，默认 ./bin/snake_server）
 * 用法：    ./bin/snake_server [端口]       默认端口见 SERVER_PORT
 *
 * 协议：见 inc/protocol.h（与客户端 LunaUI/net/protocol.h 一致）。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <time.h>

#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "../inc/protocol.h"

/* ----------------------------------------------------------------- */
/* 常量                                                               */
/* ----------------------------------------------------------------- */
#define TICK_MS         100         /* 帧间隔 100ms = 10 帧/秒 */
#define RESTART_TICKS   30          /* 本局结束到重开之间的 ticks（3s） */
#define MAX_CLIENTS     32
#define MAX_ROOMS       8
#define OBUF_SIZE       32768       /* 单客户端发送缓冲（STATE 帧最大约 12KB） */

/* 连续移动模型常量 */
#define SNAKE_SPEED_PX     SNAKE_CELL_PX   /* 蛇头每 tick 前进像素数（10Hz × 20px = 200px/s） */
#define SNAKE_PATH_MAX     (SNAKE_MAX_LEN + 8)  /* 每条蛇保留的轨迹折线点数上限（覆盖最大身长） */
#define SNAKE_HIT_R        12              /* 碰撞判定半径（px）：蛇头与蛇身点的距离阈值 */
#define SNAKE_HIT_R2       (SNAKE_HIT_R * SNAKE_HIT_R)
#define SNAKE_EAT_MARGIN   10              /* 吃食判定余量：蛇头到食物中心距离 ≤ 半径+余量 即吃到 */

typedef enum { DIR_UP, DIR_DOWN, DIR_LEFT, DIR_RIGHT,
               DIR_UP_LEFT, DIR_UP_RIGHT, DIR_DOWN_LEFT, DIR_DOWN_RIGHT } dir_t;

/* 8 方向的单位向量（速度各方向一致） */
static const double dir_ux[8] = {  0.0,  0.0, -1.0,  1.0, -0.70710678,  0.70710678, -0.70710678,  0.70710678 };
static const double dir_uy[8] = { -1.0,  1.0,  0.0,  0.0, -0.70710678, -0.70710678,  0.70710678,  0.70710678 };

/* 房间内的一名玩家（阶段C：像素头 + 轨迹折线） */
typedef struct {
    int   client_id;            /* 对应客户端 id，-1 表示空槽 */
    int   fd;                   /* 对应客户端 socket */
    char  name[SNAKE_MAX_NAME + 1];
    int   color;                /* 调色板索引 */
    int   alive;
    int   score;
    int   inv;                  /* 剩余无敌 ticks（>0 表示无敌） */
    int   small_eaten;          /* 已累计吃掉的小食物数（每 10 个长度 +1 并清零） */
    int   small_eaten_skill;    /* 吃小食物累计的护盾进度（每 SNAKE_SMALL_PER_SHIELD 个发放一次） */
    int   big_eaten_skill;      /* 吃大食物累计的加速进度（每 SNAKE_BIG_PER_SPEED 个发放一次） */
    int   len;                  /* 逻辑长度（格单位），吃食 +1；身体折线点数 = min(len, path_n) */
    dir_t dir, ndir;
    double hx, hy;              /* 蛇头位置：经典=网格坐标；环形=像素坐标（中心点） */
    int   path[SNAKE_PATH_MAX][2];  /* 环形：轨迹折线点（像素），path[0]=蛇头，越往后越靠尾 */
    int   path_n;               /* 已保留的折线点数（上限 SNAKE_PATH_MAX） */
    int   gbody[SNAKE_MAX_LEN][2];  /* 经典：身体格链（每格唯一），gbody[0]=蛇头 */
    int   skills;               /* 持有技能位图：bit0=加速, bit1=护盾（拾取未使用） */
    int   skill_speed_ticks;    /* 加速效果剩余 ticks（>0 表示加速激活中） */
    int   skill_shield_ticks;   /* 护盾效果剩余 ticks（>0 表示护盾激活中） */
} player_t;

/* 一个食物：kind=0 小食物(4px)，kind=1 大食物(16px)。
 * x/y 为屏幕像素坐标（左上角），cx/cy 为所在网格单元，用于吃食判定。 */
typedef struct {
    int x, y;
    int cx, cy;
    int kind;
} food_t;

/* 一个房间（一种模式） */
typedef struct {
    int   id;                   /* 房间号（服务器维护） */
    int   mode;                 /* 0=单人, 1=多人 */
    int   wrap;                 /* 地图类型：0=经典(撞墙死), 1=环形(穿墙) */
    int   cols, rows;           /* 本房间地图网格尺寸（经典 40×24，环形 80×48） */
    int   active;               /* 是否还在使用 */
    int   round_started;
    int   restart_ticks;
    food_t foods[SNAKE_FOOD_MAX];
    int   food_count;           /* 当前食物总数（小+大） */
    int   refresh_ticks;        /* 小食物刷新倒计时 */
    int   biggen_ticks;         /* 大食物生成倒计时 */
    player_t players[SNAKE_MAX_PER_ROOM];
} room_t;

/* 一个客户端（连接） */
typedef struct {
    int   fd;                   /* -1 表示空槽 */
    int   id;                   /* 玩家/客户端 id（全局唯一） */
    int   joined;               /* 是否已发送 join 并注册用户名 */
    char  name[SNAKE_MAX_NAME + 1];
    int   mode;                 /* 0=未选, 1=单人, 2=多人等待 */
    int   room;                 /* 所在房间下标，-1 无 */
    char  rbuf[PROTO_LINE_MAX];
    int   rlen;
    char  obuf[OBUF_SIZE];
    int   olen;
} client_t;

/* ----------------------------------------------------------------- */
/* 全局状态                                                           */
/* ----------------------------------------------------------------- */
static client_t clients[MAX_CLIENTS];
static room_t    rooms[MAX_ROOMS];
static int       client_id_next = 1;
static int       room_id_next   = 1;
static int       running        = 1;

/* ----------------------------------------------------------------- */
/* 工具                                                               */
/* ----------------------------------------------------------------- */
static long long now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000 + (long long)tv.tv_usec / 1000;
}

/* 折线轨迹辅助（阶段C） ------------------------------------------------ */

/* 把新的头部折线点插入 path[0]（旧点整体右移，超出上限丢弃最老点） */
static void path_push(player_t *p, int wx, int wy)
{
    int i;
    if (p->path_n >= SNAKE_PATH_MAX) {
        /* 已满：整体右移 [SNAKE_PATH_MAX-1 .. 1]，丢弃最老点，并腾出 [0] */
        for (i = SNAKE_PATH_MAX - 1; i > 0; i--) {
            p->path[i][0] = p->path[i - 1][0];
            p->path[i][1] = p->path[i - 1][1];
        }
    } else {
        for (i = p->path_n; i > 0; i--) {
            p->path[i][0] = p->path[i - 1][0];
            p->path[i][1] = p->path[i - 1][1];
        }
        p->path_n++;
    }
    p->path[0][0] = wx;
    p->path[0][1] = wy;
}

/* 环形地图：把坐标包装到 [0, m) */
static int wrap_px(int v, int m)
{
    v %= m;
    if (v < 0) v += m;
    return v;
}

/* 蛇头从 (hx,hy) 前进到 (nhx,nhy)（未包装目标），把新头折线点入列。
 * 经典地图：直接入列；环形地图：dist 为本次前进像素（加速技能时翻倍），
 * 段细分为 2px 子步，跨边时自动插入边界点（保证相邻折线点距离 ≤ 20px），
 * 最终入列新头。 */
static void advance_head(const room_t *r, player_t *p, double nhx, double nhy, int dist)
{
    int mapw = r->cols * SNAKE_CELL_PX, maph = r->rows * SNAKE_CELL_PX;
    if (!r->wrap) {
        path_push(p, (int)(nhx + 0.5), (int)(nhy + 0.5));
        return;
    }
    double dx = nhx - p->hx, dy = nhy - p->hy;
    int sub = dist / 2;                    /* 2px 子步 */
    int pwx = wrap_px((int)p->hx, mapw), pwy = wrap_px((int)p->hy, maph);
    for (int s = 1; s <= sub; s++) {
        double sx = p->hx + dx * s / sub;
        double sy = p->hy + dy * s / sub;
        int wx = wrap_px((int)sx, mapw), wy = wrap_px((int)sy, maph);
        if (abs(wx - pwx) > SNAKE_CELL_PX / 2 || abs(wy - pwy) > SNAKE_CELL_PX / 2) {
            path_push(p, pwx, pwy);            /* 跨边：先补上边界前的点 */
            path_push(p, wx, wy);              /* 再入列跨边后的点 */
        } else if (s == sub) {
            path_push(p, wx, wy);              /* 终点（新头） */
        }
        pwx = wx;
        pwy = wy;
    }
}

/* 环形地图下两点间的环形最短距离（非负） */
static int wrap_dist(int d, int m)
{
    d %= m;
    if (d < 0) d += m;
    if (d > m - d) d = m - d;
    return d;
}

/* 两像素点距离平方（环形地图按"最近映像"计算，经典地图普通距离） */
static int dist2_wrap(const room_t *r, int x1, int y1, int x2, int y2)
{
    int dx = x1 - x2, dy = y1 - y2;
    if (r->wrap) {
        int mapw = r->cols * SNAKE_CELL_PX, maph = r->rows * SNAKE_CELL_PX;
        dx = wrap_dist(dx, mapw);
        dy = wrap_dist(dy, maph);
    }
    return dx * dx + dy * dy;
}

/* 某条蛇当前身体折线点数 = min(逻辑长度, 保留轨迹点数) */
static int body_pts(const player_t *p)
{
    int n = p->len;
    if (n > p->path_n) n = p->path_n;
    return n > 0 ? n : 0;
}

/* 蛇头像素点是否命中某个食物（食物中心距离 ≤ 半径+余量） */
static int food_hit(const food_t *f, int hx, int hy)
{
    int fs = (f->kind == 1) ? SNAKE_BIG_FOOD_SIZE : SNAKE_SMALL_FOOD_SIZE;
    int fcx = f->x + fs / 2, fcy = f->y + fs / 2;
    return (abs(hx - fcx) <= fs / 2 + SNAKE_EAT_MARGIN &&
            abs(hy - fcy) <= fs / 2 + SNAKE_EAT_MARGIN);
}

/* 构建"被蛇占据"的网格（用于食物放置/出生点选择），g 尺寸 cols×rows */
static void build_blocked_grid(const room_t *r, uint8_t *g)
{
    int cols = r->cols, rows = r->rows, i, k;
    memset(g, 0, (size_t)(cols * rows));
    for (i = 0; i < SNAKE_MAX_PER_ROOM; i++) {
        const player_t *p = &r->players[i];
        if (p->client_id < 0 || !p->alive) continue;
        if (!r->wrap) {
            /* 经典：整格链 gbody */
            int n2 = p->len;
            if (n2 > SNAKE_MAX_LEN) n2 = SNAKE_MAX_LEN;
            for (k = 0; k < n2; k++) {
                int cx = p->gbody[k][0], cy = p->gbody[k][1];
                if (cx >= 0 && cx < cols && cy >= 0 && cy < rows) g[cy * cols + cx] = 1;
            }
            continue;
        }
        int n = body_pts(p);
        for (k = 0; k < n; k++) {
            int cx = p->path[k][0] / SNAKE_CELL_PX, cy = p->path[k][1] / SNAKE_CELL_PX;
            if (cx >= 0 && cx < cols && cy >= 0 && cy < rows) g[cy * cols + cx] = 1;
        }
        /* 相邻折线点之间按 4px 步进补点，避免段跨格漏标 */
        for (k = 0; k + 1 < n; k++) {
            double dx = (double)p->path[k + 1][0] - p->path[k][0];
            double dy = (double)p->path[k + 1][1] - p->path[k][1];
            int steps = (int)(fabs(dx) + fabs(dy)) / 4 + 1;
            for (int s = 1; s < steps; s++) {
                int sx = p->path[k][0] + (int)(dx * s / steps);
                int sy = p->path[k][1] + (int)(dy * s / steps);
                int cx = sx / SNAKE_CELL_PX, cy = sy / SNAKE_CELL_PX;
                if (cx >= 0 && cx < cols && cy >= 0 && cy < rows) g[cy * cols + cx] = 1;
            }
        }
    }
}

static dir_t opposite_dir(dir_t d)
{
    switch (d) {
        case DIR_UP:         return DIR_DOWN;
        case DIR_DOWN:       return DIR_UP;
        case DIR_LEFT:       return DIR_RIGHT;
        case DIR_RIGHT:      return DIR_LEFT;
        case DIR_UP_LEFT:    return DIR_DOWN_RIGHT;
        case DIR_UP_RIGHT:   return DIR_DOWN_LEFT;
        case DIR_DOWN_LEFT:  return DIR_UP_RIGHT;
        case DIR_DOWN_RIGHT: return DIR_UP_LEFT;
    }
    return d;
}

static void log_info(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    printf("[server] ");
    vprintf(fmt, ap);
    printf("\n");
    fflush(stdout);
    va_end(ap);
}

/* 按地图类型设置房间地图尺寸：经典 40×24（整屏），环形 80×48（远大于屏幕） */
static void room_set_map(room_t *r, int wrap)
{
    r->wrap = wrap;
    r->cols = wrap ? SNAKE_COLS : SNAKE_CLASSIC_COLS;
    r->rows = wrap ? SNAKE_ROWS : SNAKE_CLASSIC_ROWS;
}

static client_t *client_by_id(int id)
{
    int i;
    for (i = 0; i < MAX_CLIENTS; i++)
        if (clients[i].fd >= 0 && clients[i].id == id) return &clients[i];
    return NULL;
}

static room_t *room_by_id(int id)
{
    int i;
    for (i = 0; i < MAX_ROOMS; i++)
        if (rooms[i].active && rooms[i].id == id) return &rooms[i];
    return NULL;
}

/* 初始化/重置一个房间：清空所有字段并把玩家槽位置为“空” */
static void room_reset(room_t *r)
{
    int j;
    memset(r, 0, sizeof(*r));
    for (j = 0; j < SNAKE_MAX_PER_ROOM; j++)
        r->players[j].client_id = -1;
}

/* ----------------------------------------------------------------- */
/* 发送（二进制帧，非阻塞）                                          */
/* ----------------------------------------------------------------- */
static void close_client(client_t *c);     /* 前向声明 */

static void out_enqueue(client_t *c, uint8_t type, const void *payload, uint16_t plen)
{
    if (c->olen > 0) return;                        /* 有待发数据则先不追加 */
    if (c->olen + PROTO_HEADER_LEN + plen > OBUF_SIZE) return;   /* 缓冲不足，丢弃 */
    c->obuf[c->olen++] = (char)type;
    c->obuf[c->olen++] = (char)(plen >> 8);
    c->obuf[c->olen++] = (char)(plen & 0xff);
    if (plen > 0 && payload) {
        memcpy(c->obuf + c->olen, payload, plen);
        c->olen += plen;
    }
}

static void out_flush(client_t *c)
{
    while (c->olen > 0) {
        ssize_t n = send(c->fd, c->obuf, (size_t)c->olen, MSG_DONTWAIT);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return;
            close_client(c);
            return;
        }
        if (n == 0) { close_client(c); return; }
        memmove(c->obuf, c->obuf + n, (size_t)(c->olen - n));
        c->olen -= (int)n;
    }
}

/* ----------------------------------------------------------------- */
/* 房间/玩家管理                                                      */
/* ----------------------------------------------------------------- */
static int pslot_free(const room_t *r, int i) { return r->players[i].client_id < 0; }

static int count_players(const room_t *r)
{
    int i, n = 0;
    for (i = 0; i < SNAKE_MAX_PER_ROOM; i++)
        if (r->players[i].client_id >= 0) n++;
    return n;
}

/* 随机出生：地图任意位置，带 8s 无敌，不与其它蛇重叠。
 * 经典地图：身体为 3 个网格坐标（hx/hy 存格），初始方向沿出生方向；
 * 环形地图：初始身体 = 头部沿出生方向向后的 3 个折线点（20px 步距，像素）。 */
static void spawn_player(room_t *r, int i)
{
    player_t *p = &r->players[i];
    int mapw = r->cols * SNAKE_CELL_PX, maph = r->rows * SNAKE_CELL_PX;
    dir_t d = (dir_t)(rand() % 4);
    double ux = dir_ux[d], uy = dir_uy[d];

    /* 用"被蛇占据"网格挑一个空位（含出生身体 3 格），重试 50 次 */
    uint8_t *grid = (uint8_t *)malloc((size_t)(r->cols * r->rows));
    build_blocked_grid(r, grid);   /* 标记其它存活蛇的身体占据格（本蛇尚未入列） */
    int cx = -1, cy = -1;
    for (int try = 0; try < 50; try++) {
        int tx = rand() % r->cols, ty = rand() % r->rows;
        /* 经典地图：出生方向的头前格也须在地图内（避免出生即面向墙困住） */
        if (!r->wrap) {
            int fx = tx + (int)ux, fy = ty + (int)uy;
            if (fx < 0 || fx >= r->cols || fy < 0 || fy >= r->rows) continue;
        }
        int ok = 1;
        for (int k = 0; k < 3; k++) {
            int bcx, bcy;
            if (r->wrap) {
                bcx = wrap_px((int)(tx * SNAKE_CELL_PX + SNAKE_CELL_PX / 2.0 - ux * SNAKE_CELL_PX * k), mapw) / SNAKE_CELL_PX;
                bcy = wrap_px((int)(ty * SNAKE_CELL_PX + SNAKE_CELL_PX / 2.0 - uy * SNAKE_CELL_PX * k), maph) / SNAKE_CELL_PX;
            } else {
                bcx = tx - (int)ux * k;
                bcy = ty - (int)uy * k;
            }
            if (bcx < 0 || bcx >= r->cols || bcy < 0 || bcy >= r->rows ||
                grid[bcy * r->cols + bcx]) { ok = 0; break; }
        }
        if (ok) { cx = tx; cy = ty; break; }
    }
    free(grid);
    if (cx < 0) { cx = rand() % r->cols; cy = rand() % r->rows; }   /* 兜底：随机位置 */

    p->alive = 1;
    p->inv   = SNAKE_INV_TICKS;      /* 8s 无敌 */
    p->score = 0;
    p->small_eaten = 0;
    p->small_eaten_skill = 0;    /* 清空技能进度（每局重新累计） */
    p->big_eaten_skill = 0;
    p->len = 3;
    p->dir = d;
    p->ndir = d;
    p->path_n = 0;
    p->skills = 0;                /* 清空技能持有/效果 */
    p->skill_speed_ticks = 0;
    p->skill_shield_ticks = 0;
    /* 依次入列：先尾后头（path_push 头插，最后入列的是蛇头）。
     * 环形地图 path[] 存像素坐标；经典地图 gbody[] 存网格格链（gbody[0]=头）。 */
    if (!r->wrap) {
        p->gbody[0][0] = cx; p->gbody[0][1] = cy;
        p->gbody[1][0] = cx - (int)ux; p->gbody[1][1] = cy - (int)uy;
        p->gbody[2][0] = cx - (int)ux * 2; p->gbody[2][1] = cy - (int)uy * 2;
        for (int k = 0; k < 3; k++) {   /* 兜底：身体格夹到地图内 */
            if (p->gbody[k][0] < 0) p->gbody[k][0] = 0;
            else if (p->gbody[k][0] >= r->cols) p->gbody[k][0] = r->cols - 1;
            if (p->gbody[k][1] < 0) p->gbody[k][1] = 0;
            else if (p->gbody[k][1] >= r->rows) p->gbody[k][1] = r->rows - 1;
        }
        p->hx = cx; p->hy = cy;      /* 经典：hx/hy 存网格坐标 */
    } else {
        for (int k = 2; k >= 0; k--) {
            double bx = cx * SNAKE_CELL_PX + SNAKE_CELL_PX / 2.0 - ux * SNAKE_CELL_PX * k;
            double by = cy * SNAKE_CELL_PX + SNAKE_CELL_PX / 2.0 - uy * SNAKE_CELL_PX * k;
            path_push(p, wrap_px((int)bx, mapw), wrap_px((int)by, maph));
        }
        p->hx = cx * SNAKE_CELL_PX + SNAKE_CELL_PX / 2.0;
        p->hy = cy * SNAKE_CELL_PX + SNAKE_CELL_PX / 2.0;
    }
}

/* 网格上是否已有食物 */
static int food_at_cell(const room_t *r, int cx, int cy)
{
    int i;
    for (i = 0; i < r->food_count; i++)
        if (r->foods[i].cx == cx && r->foods[i].cy == cy) return 1;
    return 0;
}

static void add_small_food(room_t *r, int cx, int cy)
{
    food_t *f = &r->foods[r->food_count++];
    f->kind = 0;
    f->cx = cx;
    f->cy = cy;
    f->x = cx * SNAKE_CELL_PX + (SNAKE_CELL_PX - SNAKE_SMALL_FOOD_SIZE) / 2;
    f->y = cy * SNAKE_CELL_PX + (SNAKE_CELL_PX - SNAKE_SMALL_FOOD_SIZE) / 2;
}

static void add_big_food(room_t *r, int cx, int cy)
{
    food_t *f = &r->foods[r->food_count++];
    f->kind = 1;
    f->cx = cx;
    f->cy = cy;
    f->x = cx * SNAKE_CELL_PX + (SNAKE_CELL_PX - SNAKE_BIG_FOOD_SIZE) / 2;
    f->y = cy * SNAKE_CELL_PX + (SNAKE_CELL_PX - SNAKE_BIG_FOOD_SIZE) / 2;
}

static int count_small_foods(const room_t *r)
{
    int i, n = 0;
    for (i = 0; i < r->food_count; i++)
        if (r->foods[i].kind == 0) n++;
    return n;
}

static int count_big_foods(const room_t *r)
{
    int i, n = 0;
    for (i = 0; i < r->food_count; i++)
        if (r->foods[i].kind == 1) n++;
    return n;
}

/* 小食物（仅环形地图）：按目标数量 SNAKE_SMALL_FOOD_MAX 在整张大地图上
 * 随机稀疏放置（不铺满），并清除已被吃掉的旧小食物。 */
static void refresh_small_foods(room_t *r)
{
    int w = 0, guard = 0;
    uint8_t *grid = (uint8_t *)malloc((size_t)(r->cols * r->rows));
    build_blocked_grid(r, grid);   /* 蛇身占据格，避免把食物放进蛇体内 */
    for (int i = 0; i < r->food_count; i++)
        if (r->foods[i].kind != 0) r->foods[w++] = r->foods[i];
    r->food_count = w;

    while (count_small_foods(r) < SNAKE_SMALL_FOOD_MAX && guard++ < SNAKE_SMALL_FOOD_MAX * 100) {
        int cx = rand() % r->cols;
        int cy = rand() % r->rows;
        if (grid[cy * r->cols + cx]) continue;
        if (food_at_cell(r, cx, cy)) continue;
        add_small_food(r, cx, cy);
    }
    free(grid);
}

/* 随机生成少量大食物，直到达到上限 SNAKE_BIG_FOOD_MAX。
 * 大食物顶替所在格的小食物（环形地图铺有小食物，否则没有空位）。 */
static void spawn_big_foods(room_t *r)
{
    int guard = 0;
    uint8_t *grid = (uint8_t *)malloc((size_t)(r->cols * r->rows));
    build_blocked_grid(r, grid);   /* 蛇身占据格 */
    while (count_big_foods(r) < SNAKE_BIG_FOOD_MAX && guard++ < 20000) {
        int cx = rand() % r->cols;
        int cy = rand() % r->rows;
        if (grid[cy * r->cols + cx]) continue;
        int has_big = 0;
        for (int i = 0; i < r->food_count; i++) {
            if (r->foods[i].cx == cx && r->foods[i].cy == cy) {
                if (r->foods[i].kind == 1) has_big = 1;   /* 该格已有大食物，跳过 */
                else {                                     /* 小食物被大食物顶替 */
                    r->foods[i] = r->foods[r->food_count - 1];
                    r->food_count--;
                }
                break;
            }
        }
        if (has_big) continue;
        add_big_food(r, cx, cy);
    }
    free(grid);
}

/* 开局初始化食物：环形地图随机稀疏铺小食物 + 生成大食物；经典地图不铺小食物，只生成大食物 */
static void init_foods(room_t *r)
{
    r->food_count = 0;
    r->refresh_ticks = SNAKE_REFRESH_TICKS;
    r->biggen_ticks  = SNAKE_BIGGEN_TICKS;
    if (r->wrap) refresh_small_foods(r);   /* 经典地图不生成小食物 */
    spawn_big_foods(r);
}

static void client_leave_room(client_t *c);   /* 前向声明（client_enter_room 会调用） */

/* 把客户端放入某个房间；返回 0 成功 */
static int client_enter_room(client_t *c, room_t *r)
{
    int i;
    if (c->room >= 0) client_leave_room(c);   /* 若已在其它房间，先退出 */
    for (i = 0; i < SNAKE_MAX_PER_ROOM; i++)
        if (pslot_free(r, i)) break;
    if (i >= SNAKE_MAX_PER_ROOM) return -1;

    player_t *p = &r->players[i];
    p->client_id = c->id;
    p->fd        = c->fd;
    snprintf(p->name, sizeof(p->name), "%s", c->name);
    p->color = i;                 /* 颜色 = 房间槽位（0..7），同一房间内唯一 */
    spawn_player(r, i);
    if (r->food_count == 0) init_foods(r);   /* 房间/新一局首次有人时铺食物 */

    c->room = (int)(r - rooms);
    c->mode = r->mode == 1 ? 2 : 1;

    if (!r->round_started && r->restart_ticks == 0) r->round_started = 1;
    r->active = 1;
    return 0;
}

static void client_leave_room(client_t *c)
{
    if (c->room < 0 || c->room >= MAX_ROOMS) return;
    room_t *r = &rooms[c->room];
    int i;
    for (i = 0; i < SNAKE_MAX_PER_ROOM; i++) {
        if (r->players[i].client_id == c->id) {
            r->players[i].client_id = -1;
            r->players[i].fd = -1;
            r->players[i].alive = 0;
            break;
        }
    }
    c->room = -1;
    c->mode = 0;
    if (count_players(r) == 0) {   /* 空房间回收，供后续复用 */
        r->active = 0;
        r->round_started = 0;
        r->restart_ticks = 0;
        r->food_count = 0;
    }
}

/* ----------------------------------------------------------------- */
/* 游戏推进（单房间）                                                  */
/* ----------------------------------------------------------------- */
/* ------------------------------------------------------------------
 * 蛇移动原理（阶段C：连续移动模型，服务端权威，客户端只渲染 STATE）
 *
 *   - 时间模型：每个房间每 TICK_MS(100ms) 推进一帧（10 帧/秒），所有蛇同时移动；
 *   - 移动（两种地图模型）：
 *     经典(40×24, wrap=0)：格点移动——蛇头每帧前进 1 格（20px），仅 4 方向，
 *         gbody[] 存"网格格链"，撞墙即死（无敌期原地不动），身体=格链；
 *     环形(80×48, wrap=1)：连续移动——蛇头按 8 方向单位向量每帧前进
 *         SNAKE_SPEED_PX(20px)（像素级连续，斜向速度与正方向一致），path[] 存像素；
 *     方向由 MSG_DIR 设置 ndir，同帧内先排除 180° 反转再提交为 dir；
 *   - 身体表示：环形 path[] 为蛇头走过的轨迹（path[0]=头，越往后越靠尾），
 *     身体 = 最近 min(len, path_n) 个点；经典 gbody[] 为身体格链（每格唯一，gbody[0]=头，
 *     长度=len）。吃食使 len +1（身体变长）；环形坐标已包装并含跨边点，相邻点距离恒 ≤ 20px；
 *   - 地图边界：经典越界即死；环形越界从对侧出现；
 *   - 食物判定：环形按"蛇头像素点到食物中心距离 ≤ 半径+余量"；经典按"头格 == 食物格"；
 *     小食物累计 small_eaten，每 10 个长度 +1，大食物直接 +1（经典地图无小食物）；
 *   - 碰撞判定：环形按像素距离 < SNAKE_HIT_R（最近映像），经典按"头格 == 他蛇身格"；
 *     环形不判自身碰撞，经典判自身（尾格随移动让位）；无敌蛇身不视为障碍，头碰头同规则；
 *   - 帧广播：移动+碰撞+吃食结算后，把世界状态打包成 STATE 广播（见 broadcast_state）。
 * ------------------------------------------------------------------ */
static void advance_tick(room_t *r)
{
    int i, j, k, alive_cnt, in_room_cnt;
    int    headx[SNAKE_MAX_PER_ROOM], heady[SNAKE_MAX_PER_ROOM];
    int    eat_idx[SNAKE_MAX_PER_ROOM];   /* 吃到食物的下标，-1 无 */
    int    dead[SNAKE_MAX_PER_ROOM];

    if (!r->round_started) return;

    /* 第一遍：方向、移动、轨迹入列、吃食检测 */
    for (i = 0; i < SNAKE_MAX_PER_ROOM; i++) {
        player_t *p = &r->players[i];
        dead[i] = 0;
        eat_idx[i] = -1;
        if (p->client_id < 0 || !p->alive) continue;

        if (p->ndir != opposite_dir(p->dir)) p->dir = p->ndir;
        p->ndir = p->dir;

        if (!r->wrap) {
            /* 经典地图：格点移动（1 格/tick，仅 4 方向），撞墙即死；无敌期原地不动。
             * 身体为格链 gbody（gbody[0]=头）；吃食按"头格 == 食物格"。 */
            if (p->dir > DIR_RIGHT) p->dir = DIR_RIGHT;   /* 兜底：经典只允许上下左右 */
            int gx = (int)p->hx, gy = (int)p->hy;
            int nx = gx + (int)dir_ux[p->dir];
            int ny = gy + (int)dir_uy[p->dir];
            if (nx < 0 || ny < 0 || nx >= r->cols || ny >= r->rows) {
                if (p->inv > 0) { headx[i] = gx; heady[i] = gy; continue; }  /* 无敌期原地不动 */
                dead[i] = 1;
                continue;
            }
            /* 身体链右移（保留尾格在 index=len，供吃食加长时回填） */
            int sh = p->len;
            if (sh >= SNAKE_MAX_LEN) sh = SNAKE_MAX_LEN - 1;
            for (k = sh; k > 0; k--) {
                p->gbody[k][0] = p->gbody[k - 1][0];
                p->gbody[k][1] = p->gbody[k - 1][1];
            }
            p->gbody[0][0] = nx;
            p->gbody[0][1] = ny;
            p->hx = nx; p->hy = ny;
            headx[i] = nx; heady[i] = ny;
            for (k = 0; k < r->food_count; k++)
                if (r->foods[k].cx == nx && r->foods[k].cy == ny) { eat_idx[i] = k; break; }
            continue;
        }

        /* 环形地图：连续移动（像素级，8 方向），越界从对侧出现；
         * 加速技能激活时每 tick 前进 40px（正常 20px）。 */
        {
            int dist = SNAKE_SPEED_PX * ((p->skill_speed_ticks > 0) ? 2 : 1);
            double nx = p->hx + dir_ux[p->dir] * dist;
            double ny = p->hy + dir_uy[p->dir] * dist;

            /* 新头折线点入列（环形地图自动处理跨边点） */
            advance_head(r, p, nx, ny, dist);
            /* 更新蛇头位置（advance_head 内部使用旧 hx 计算位移，须在其之后更新） */
            p->hx = nx;
            p->hy = ny;
            headx[i] = wrap_px((int)nx, r->cols * SNAKE_CELL_PX);
            heady[i] = wrap_px((int)ny, r->rows * SNAKE_CELL_PX);

            /* 吃食检测 */
            for (k = 0; k < r->food_count; k++) {
                if (food_hit(&r->foods[k], headx[i], heady[i])) { eat_idx[i] = k; break; }
            }
        }
    }

    /* 第二遍：碰撞（仅非无敌蛇；无敌蛇身不视为障碍；护盾技能激活等同无敌） */
    for (i = 0; i < SNAKE_MAX_PER_ROOM; i++) {
        player_t *p = &r->players[i];
        if (dead[i] || p->client_id < 0 || !p->alive || p->inv > 0 || p->skill_shield_ticks > 0) continue;

        for (j = 0; j < SNAKE_MAX_PER_ROOM; j++) {
            player_t *q = &r->players[j];
            if (i == j || dead[j] || q->client_id < 0 || !q->alive || q->inv > 0 || q->skill_shield_ticks > 0) continue;
            int qn = body_pts(q);
            if (r->wrap) {
                /* 环形：像素距离判定（最近映像） */
                for (k = 0; k < qn; k++)
                    if (dist2_wrap(r, headx[i], heady[i], q->path[k][0], q->path[k][1]) < SNAKE_HIT_R2) { dead[i] = 1; break; }
            } else {
                /* 经典：头格 == 他蛇身格（排除其尾格，尾格将随移动让位） */
                qn = q->len;
                if (qn > SNAKE_MAX_LEN) qn = SNAKE_MAX_LEN;
                for (k = 0; k + 1 < qn; k++)
                    if (headx[i] == q->gbody[k][0] && heady[i] == q->gbody[k][1]) { dead[i] = 1; break; }
            }
            /* 头碰头：环形按距离；经典按同格 */
            if (!dead[i]) {
                if (r->wrap) {
                    if (dist2_wrap(r, headx[i], heady[i], headx[j], heady[j]) < SNAKE_HIT_R2) dead[i] = 1;
                } else {
                    if (headx[i] == headx[j] && heady[i] == heady[j]) dead[i] = 1;
                }
            }
            if (dead[i]) break;
        }
        /* 撞自身：环形不判死；经典按格判定（gbody[0] 是新头，尾格随移动让位） */
        if (!dead[i] && !r->wrap) {
            int pn = p->len;
            if (pn > SNAKE_MAX_LEN) pn = SNAKE_MAX_LEN;
            for (k = 1; k < pn; k++)
                if (headx[i] == p->gbody[k][0] && heady[i] == p->gbody[k][1]) { dead[i] = 1; break; }
        }
    }

    /* 第三遍：应用（死亡、吃食/加长/计分、无敌递减） */
    for (i = 0; i < SNAKE_MAX_PER_ROOM; i++) {
        player_t *p = &r->players[i];
        if (p->client_id < 0 || !p->alive) continue;
        if (dead[i]) { p->alive = 0; continue; }

        if (eat_idx[i] >= 0) {
            /* 按头部坐标重新查找食物（多蛇同食时索引可能已失效） */
            int fi = -1;
            for (k = 0; k < r->food_count; k++) {
                if (r->wrap) {
                    if (food_hit(&r->foods[k], headx[i], heady[i])) { fi = k; break; }
                } else {
                    if (r->foods[k].cx == headx[i] && r->foods[k].cy == heady[i]) { fi = k; break; }
                }
            }
            if (fi >= 0) {
                int fk = r->foods[fi].kind;   /* 先记录种类再移除 */
                r->foods[fi] = r->foods[r->food_count - 1];
                r->food_count--;
                /* 计分：小食物 +1，大食物 +10 */
                int add = (fk == 1) ? 10 : 1;
                if (p->score > 65535 - add) p->score = 65535;
                else p->score += add;
                if (fk == 1) {                       /* 大食物：长度 +1，并累计加速技能进度（每 3 个一次） */
                    if (p->len < SNAKE_MAX_LEN) p->len++;
                    if (r->wrap && ++p->big_eaten_skill >= SNAKE_BIG_PER_SPEED) {
                        p->big_eaten_skill = 0;      /* 每 3 个为一次发放机会；已持有/激活中则该次作废 */
                        if (!(p->skills & SKILL_BIT_SPEED) && p->skill_speed_ticks <= 0)
                            p->skills |= SKILL_BIT_SPEED;   /* 未持有且未激活 → 获得加速 */
                    }
                } else {                             /* 小食物：累计 10 个长度 +1，并累计护盾技能进度（每 40 个一次） */
                    p->small_eaten++;
                    if (p->small_eaten >= SNAKE_LEN_PER_SMALL) {
                        p->small_eaten = 0;
                        if (p->len < SNAKE_MAX_LEN) p->len++;
                    }
                    if (r->wrap && ++p->small_eaten_skill >= SNAKE_SMALL_PER_SHIELD) {
                        p->small_eaten_skill = 0;    /* 每 40 个为一次发放机会；已持有/激活中则该次作废 */
                        if (!(p->skills & SKILL_BIT_SHIELD) && p->skill_shield_ticks <= 0)
                            p->skills |= SKILL_BIT_SHIELD;  /* 未持有且未激活 → 获得护盾 */
                    }
                }
            }
        }
        if (p->inv > 0) p->inv--;
        /* 技能效果计时：效果结束自动失效（加速/护盾位图清除） */
        if (p->skill_speed_ticks > 0 && --p->skill_speed_ticks == 0) p->skills &= ~SKILL_BIT_SPEED;
        if (p->skill_shield_ticks > 0 && --p->skill_shield_ticks == 0) p->skills &= ~SKILL_BIT_SHIELD;
    }

    /* 食物玩法计时：小食物定期刷新（仅环形地图）；大食物定期生成（两种地图都有） */
    if (r->wrap) {
        if (r->refresh_ticks <= 0) {
            refresh_small_foods(r);
            r->refresh_ticks = SNAKE_REFRESH_TICKS;
        } else {
            r->refresh_ticks--;
        }
    }
    if (r->biggen_ticks <= 0) {
        spawn_big_foods(r);
        r->biggen_ticks = SNAKE_BIGGEN_TICKS;
    } else {
        r->biggen_ticks--;
    }

    /* 胜负：多人时存活蛇 ≤ 1 结束；单人时蛇死亡即结束 */
    alive_cnt = 0;
    in_room_cnt = 0;
    for (i = 0; i < SNAKE_MAX_PER_ROOM; i++) {
        player_t *p = &r->players[i];
        if (p->client_id < 0) continue;
        in_room_cnt++;
        if (p->alive) alive_cnt++;
    }
    if ((in_room_cnt >= 2 && alive_cnt <= 1) || (in_room_cnt == 1 && alive_cnt == 0)) {
        int winner = -1;
        for (i = 0; i < SNAKE_MAX_PER_ROOM; i++)
            if (r->players[i].client_id >= 0 && r->players[i].alive) { winner = r->players[i].client_id; break; }
        r->round_started = 0;
        r->restart_ticks = RESTART_TICKS;
        log_info("房间 %d 本局结束，胜者 id = %d", r->id, winner);
        uint8_t wb[4];
        pr_put_i32(wb, winner);
        for (i = 0; i < SNAKE_MAX_PER_ROOM; i++) {
            player_t *p = &r->players[i];
            if (p->client_id < 0) continue;
            client_t *cc = client_by_id(p->client_id);
            if (cc) out_enqueue(cc, MSG_OVER, wb, sizeof(wb));
        }
    }
}

/* 广播一帧状态（二进制）到房间所有玩家 */
static void broadcast_state(room_t *r)
{
    static uint8_t buf[OBUF_SIZE];
    uint8_t *p = buf;
    int i, j, n;

    pr_put_u32(p, (uint32_t)(now_ms() % 1000000)); p += 4;

    /* 小食物 */
    n = 0;
    for (i = 0; i < r->food_count; i++) if (r->foods[i].kind == 0) n++;
    pr_put_u16(p, (uint16_t)n); p += 2;
    for (i = 0; i < r->food_count; i++) {
        if (r->foods[i].kind != 0) continue;
        pr_put_u16(p, (uint16_t)r->foods[i].x); p += 2;
        pr_put_u16(p, (uint16_t)r->foods[i].y); p += 2;
    }

    /* 大食物 */
    n = 0;
    for (i = 0; i < r->food_count; i++) if (r->foods[i].kind == 1) n++;
    pr_put_u8(p, (uint8_t)n); p += 1;
    for (i = 0; i < r->food_count; i++) {
        if (r->foods[i].kind != 1) continue;
        pr_put_u16(p, (uint16_t)r->foods[i].x); p += 2;
        pr_put_u16(p, (uint16_t)r->foods[i].y); p += 2;
    }

    /* 技能道具数（协议保留字段，恒为 0：技能改为吃食物获得，不再地图拾取） */
    n = 0;
    pr_put_u8(p, (uint8_t)n); p += 1;

    /* 蛇：折线点像素坐标（body[0]=蛇头，连续移动模型） */
    n = 0;
    for (i = 0; i < SNAKE_MAX_PER_ROOM; i++) {
        player_t *pl = &r->players[i];
        if (pl->client_id >= 0 && pl->alive) n++;
    }
    pr_put_u8(p, (uint8_t)n); p += 1;
    for (i = 0; i < SNAKE_MAX_PER_ROOM; i++) {
        player_t *pl = &r->players[i];
        if (pl->client_id < 0 || !pl->alive) continue;
        int bn;                                    /* 经典=格链长 len；环形=折线点数 */
        if (r->wrap) { bn = body_pts(pl); if (bn < 1) bn = 1; }
        else         { bn = pl->len; if (bn < 1) bn = 1; }
        pr_put_u32(p, (uint32_t)pl->client_id); p += 4;
        pr_put_u8(p, (uint8_t)pl->color); p += 1;
        pr_put_u16(p, (uint16_t)bn); p += 2;
        pr_put_u16(p, (uint16_t)pl->score); p += 2;
        pr_put_u8(p, (uint8_t)(pl->inv > 0 ? (pl->inv * TICK_MS) / 1000 : 0)); p += 1;
        pr_put_u8(p, (uint8_t)pl->small_eaten); p += 1;
        pr_put_u8(p, (uint8_t)pl->skills); p += 1;                    /* 持有技能位图 */
        pr_put_u8(p, (uint8_t)pl->skill_speed_ticks); p += 1;         /* 加速剩余 ticks */
        pr_put_u8(p, (uint8_t)pl->skill_shield_ticks); p += 1;        /* 护盾剩余 ticks */
        memcpy(p, pl->name, SNAKE_MAX_NAME); p += SNAKE_MAX_NAME;
        for (j = 0; j < bn; j++) {
            int px, py;
            if (r->wrap) { px = pl->path[j][0]; py = pl->path[j][1]; }
            else         { px = pl->gbody[j][0]; py = pl->gbody[j][1]; }
            pr_put_u16(p, (uint16_t)px); p += 2;
            pr_put_u16(p, (uint16_t)py); p += 2;
        }
    }

    uint16_t total = (uint16_t)(p - buf);
    for (i = 0; i < SNAKE_MAX_PER_ROOM; i++) {
        player_t *pl = &r->players[i];
        if (pl->client_id < 0) continue;
        client_t *cc = client_by_id(pl->client_id);
        if (cc) out_enqueue(cc, MSG_STATE, buf, total);
    }
}

/* ----------------------------------------------------------------- */
/* 客户端命令处理（二进制帧）                                        */
/* ----------------------------------------------------------------- */
static void handle_join(client_t *c, const uint8_t *p, uint16_t plen)
{
    int n = plen < SNAKE_MAX_NAME ? plen : SNAKE_MAX_NAME;
    if (n > 0) {
        memcpy(c->name, p, (size_t)n);
        c->name[n] = '\0';
    } else {
        c->name[0] = '\0';
    }
    c->joined = 1;
    log_info("玩家 %s(id=%d) 加入", c->name[0] ? c->name : "?", c->id);

    uint8_t w[8];
    pr_put_u32(w, (uint32_t)c->id);
    pr_put_u16(w + 4, (uint16_t)SNAKE_COLS);
    pr_put_u16(w + 6, (uint16_t)SNAKE_ROWS);
    out_enqueue(c, MSG_WELCOME, w, sizeof(w));
}

static void send_rooms_list(client_t *c)
{
    uint8_t buf[1 + MAX_ROOMS * 7];
    int n = 0, i;
    for (i = 0; i < MAX_ROOMS; i++) {
        if (!rooms[i].active || rooms[i].mode != 1) continue;   /* 只列多人房间 */
        uint8_t *e = buf + 1 + n * 7;
        pr_put_u32(e, (uint32_t)rooms[i].id);
        e[4] = (uint8_t)count_players(&rooms[i]);
        e[5] = (uint8_t)SNAKE_MAX_PER_ROOM;
        e[6] = (uint8_t)rooms[i].wrap;
        n++;
    }
    buf[0] = (uint8_t)n;
    out_enqueue(c, MSG_ROOMS, buf, 1 + n * 7);
}

static void handle_mode(client_t *c, const uint8_t *p, uint16_t plen)
{
    if (plen < 2) return;
    uint8_t m = p[0];
    uint8_t wrap = p[1] ? 1 : 0;
    if (m == 0) {                        /* 单人 */
        int i;
        room_t *r = NULL;
        for (i = 0; i < MAX_ROOMS; i++)
            if (!rooms[i].active) { r = &rooms[i]; break; }
        if (r) {
            room_reset(r);
            r->id = room_id_next++;
            r->mode = 0;
            room_set_map(r, wrap);
            r->active = 1;
            client_enter_room(c, r);
            uint8_t ro[10];
            pr_put_u32(ro, (uint32_t)r->id);
            ro[4] = 0;
            ro[5] = (uint8_t)r->wrap;
            pr_put_u16(ro + 6, (uint16_t)r->cols);
            pr_put_u16(ro + 8, (uint16_t)r->rows);
            out_enqueue(c, MSG_ROOM, ro, sizeof(ro));
        }
    } else {                             /* 多人 */
        c->mode = 2;
        send_rooms_list(c);
    }
}

static void handle_create_room(client_t *c, const uint8_t *p, uint16_t plen)
{
    int i;
    uint8_t wrap = (plen >= 1 && p[0]) ? 1 : 0;
    room_t *r = NULL;
    for (i = 0; i < MAX_ROOMS; i++)
        if (!rooms[i].active) { r = &rooms[i]; break; }
    if (!r) {
        out_enqueue(c, MSG_ERROR, "服务器已满", 15);
        return;
    }
    room_reset(r);
    r->id = room_id_next++;
    r->mode = 1;
    room_set_map(r, wrap);
    r->active = 1;
    client_enter_room(c, r);
    uint8_t ro[10];
    pr_put_u32(ro, (uint32_t)r->id);
    ro[4] = 1;
    ro[5] = (uint8_t)r->wrap;
    pr_put_u16(ro + 6, (uint16_t)r->cols);
    pr_put_u16(ro + 8, (uint16_t)r->rows);
    out_enqueue(c, MSG_ROOM, ro, sizeof(ro));
}

static void handle_join_room(client_t *c, const uint8_t *p, uint16_t plen)
{
    if (plen < 4) return;
    int rid = (int)pr_get_u32(p);
    room_t *r = room_by_id(rid);
    if (!r || r->mode != 1) {
        out_enqueue(c, MSG_ERROR, "房间不存在", 15);
        return;
    }
    if (count_players(r) >= SNAKE_MAX_PER_ROOM) {
        out_enqueue(c, MSG_ERROR, "房间已满", 12);
        return;
    }
    client_enter_room(c, r);
    uint8_t ro[10];
    pr_put_u32(ro, (uint32_t)r->id);
    ro[4] = 1;
    ro[5] = (uint8_t)r->wrap;
    pr_put_u16(ro + 6, (uint16_t)r->cols);
    pr_put_u16(ro + 8, (uint16_t)r->rows);
    out_enqueue(c, MSG_ROOM, ro, sizeof(ro));
}

static void handle_random_join(client_t *c)
{
    int i;
    room_t *r = NULL;
    for (i = 0; i < MAX_ROOMS; i++)
        if (rooms[i].active && rooms[i].mode == 1 &&
            count_players(&rooms[i]) < SNAKE_MAX_PER_ROOM) { r = &rooms[i]; break; }
    if (!r) { handle_create_room(c, NULL, 0); return; }
    client_enter_room(c, r);
    uint8_t ro[10];
    pr_put_u32(ro, (uint32_t)r->id);
    ro[4] = 1;
    ro[5] = (uint8_t)r->wrap;
    pr_put_u16(ro + 6, (uint16_t)r->cols);
    pr_put_u16(ro + 8, (uint16_t)r->rows);
    out_enqueue(c, MSG_ROOM, ro, sizeof(ro));
}

static void handle_dir(client_t *c, const uint8_t *p, uint16_t plen)
{
    if (plen < 1 || c->room < 0 || c->room >= MAX_ROOMS) return;
    if (p[0] > 7) return;                 /* dir_t 范围 0..7 */
    dir_t nd = (dir_t)p[0];
    room_t *r = &rooms[c->room];
    if (!r->wrap && nd > DIR_RIGHT) return;   /* 经典地图仅上下左右 4 方向 */
    int i;
    for (i = 0; i < SNAKE_MAX_PER_ROOM; i++) {
        player_t *pl = &r->players[i];
        if (pl->client_id == c->id && pl->alive) {
            if (nd != opposite_dir(pl->dir)) pl->ndir = nd;
            break;
        }
    }
}

static void handle_leave(client_t *c)
{
    if (c->room >= 0) client_leave_room(c);
}

/* 使用技能（仅环形地图）：skill 0=加速 1=护盾。
 * 须已持有该技能（skills 对应位）；使用后技能从"持有"转为"激活中"（一次性）。 */
static void handle_use_skill(client_t *c, const uint8_t *p, uint16_t plen)
{
    if (plen < 1 || c->room < 0 || c->room >= MAX_ROOMS) return;
    int skill = p[0];
    if (skill != SKILL_SPEED && skill != SKILL_SHIELD) return;
    room_t *r = &rooms[c->room];
    if (!r->wrap) return;                                  /* 经典地图无技能 */
    int i;
    for (i = 0; i < SNAKE_MAX_PER_ROOM; i++) {
        player_t *pl = &r->players[i];
        if (pl->client_id != c->id || !pl->alive) continue;
        if (skill == SKILL_SPEED) {
            if (!(pl->skills & SKILL_BIT_SPEED)) return;   /* 未持有 */
            pl->skills &= ~SKILL_BIT_SPEED;
            pl->skill_speed_ticks = SKILL_SPEED_TICKS;
        } else {
            if (!(pl->skills & SKILL_BIT_SHIELD)) return;
            pl->skills &= ~SKILL_BIT_SHIELD;
            pl->skill_shield_ticks = SKILL_SHIELD_TICKS;
        }
        break;
    }
}

/* ----------------------------------------------------------------- */
/* 消息解析：一帧二进制                                               */
/* ----------------------------------------------------------------- */
static void process_frame(client_t *c, uint8_t type, const uint8_t *p, uint16_t plen)
{
    switch (type) {
        case MSG_JOIN:        handle_join(c, p, plen); break;
        case MSG_MODE:        handle_mode(c, p, plen); break;
        case MSG_ROOM_LIST:   send_rooms_list(c); break;
        case MSG_CREATE_ROOM: handle_create_room(c, p, plen); break;
        case MSG_JOIN_ROOM:   handle_join_room(c, p, plen); break;
        case MSG_RANDOM_JOIN: handle_random_join(c); break;
        case MSG_DIR:         handle_dir(c, p, plen); break;
        case MSG_LEAVE:       handle_leave(c); break;
        case MSG_USE_SKILL:   handle_use_skill(c, p, plen); break;
        case MSG_BYE:         close_client(c); break;
        default:              break;
    }
}

/* ----------------------------------------------------------------- */
/* 数据接收                                                           */
/* ----------------------------------------------------------------- */
static void process_input(client_t *c)
{
    char tmp[1024];
    ssize_t n = recv(c->fd, tmp, sizeof(tmp), MSG_DONTWAIT);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return;
        close_client(c);
        return;
    }
    if (n == 0) { close_client(c); return; }
    if (c->rlen + n > PROTO_LINE_MAX - 1) c->rlen = 0;
    memcpy(c->rbuf + c->rlen, tmp, (size_t)n);
    c->rlen += (int)n;

    /* 切分二进制帧：帧头 3 字节（类型 + 负载长度） */
    int pos = 0;
    while (c->rlen - pos >= PROTO_HEADER_LEN) {
        uint8_t type = (uint8_t)c->rbuf[pos];
        uint16_t plen = (uint16_t)(((uint16_t)(uint8_t)c->rbuf[pos + 1] << 8) | (uint8_t)c->rbuf[pos + 2]);
        if (PROTO_HEADER_LEN + plen > PROTO_LINE_MAX) {   /* 非法长度，丢弃缓冲 */
            c->rlen = 0;
            return;
        }
        if (c->rlen - pos < PROTO_HEADER_LEN + plen) break;
        process_frame(c, type, (const uint8_t *)c->rbuf + pos + PROTO_HEADER_LEN, plen);
        if (c->fd < 0) return;
        pos += PROTO_HEADER_LEN + plen;
    }
    if (pos > 0) {
        c->rlen -= pos;
        memmove(c->rbuf, c->rbuf + pos, (size_t)c->rlen);
    }
}

/* ----------------------------------------------------------------- */
/* 连接管理                                                           */
/* ----------------------------------------------------------------- */
static void close_client(client_t *c)
{
    if (c->fd < 0) return;
    log_info("客户端 id=%d (%s) 断开", c->id, c->name[0] ? c->name : "?");
    if (c->room >= 0) client_leave_room(c);
    close(c->fd);
    c->fd = -1;
    c->joined = 0;
    c->rlen = 0;
    c->olen = 0;
    c->name[0] = '\0';
}

static void handle_sigint(int sig)
{
    (void)sig;
    running = 0;
}

/* ----------------------------------------------------------------- */
/* main                                                               */
/* ----------------------------------------------------------------- */
int main(int argc, char **argv)
{
    int port = (argc >= 2) ? atoi(argv[1]) : SERVER_PORT;
    int listen_fd, i, j;
    struct sockaddr_in addr;

    signal(SIGINT, handle_sigint);
    signal(SIGPIPE, SIG_IGN);
    srand((unsigned int)time(NULL));

    for (i = 0; i < MAX_CLIENTS; i++) {
        clients[i].fd = -1;
        clients[i].id = -1;
        clients[i].joined = 0;
        clients[i].rlen = 0;
        clients[i].olen = 0;
        clients[i].name[0] = '\0';
        clients[i].room = -1;
    }
    for (i = 0; i < MAX_ROOMS; i++) {
        rooms[i].active = 0;
        rooms[i].id = 0;
        rooms[i].mode = 0;
        rooms[i].round_started = 0;
        rooms[i].restart_ticks = 0;
        rooms[i].food_count = 0;
        for (j = 0; j < SNAKE_MAX_PER_ROOM; j++) rooms[i].players[j].client_id = -1;
    }

    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) { perror("socket"); return 1; }
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((unsigned short)port);
    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { perror("bind"); return 1; }
    if (listen(listen_fd, 8) < 0) { perror("listen"); return 1; }

    log_info("贪吃蛇服务端已启动（二进制协议），监听端口 %d", port);

    long long last_tick = now_ms();
    while (running) {
        fd_set rfds, wfds;
        FD_ZERO(&rfds);
        FD_ZERO(&wfds);
        int maxfd = listen_fd;
        FD_SET(listen_fd, &rfds);

        for (i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].fd < 0) continue;
            FD_SET(clients[i].fd, &rfds);
            if (clients[i].olen > 0) FD_SET(clients[i].fd, &wfds);
            if (clients[i].fd > maxfd) maxfd = clients[i].fd;
        }

        long long now = now_ms();
        long long remain = last_tick + TICK_MS - now;
        if (remain < 0) remain = 0;
        if (remain > 100) remain = 100;

        struct timeval tv;
        tv.tv_sec  = (long)(remain / 1000);
        tv.tv_usec = (long)((remain % 1000) * 1000);

        int ret = select(maxfd + 1, &rfds, &wfds, NULL, &tv);
        if (ret < 0) { if (errno == EINTR) continue; perror("select"); break; }

        if (FD_ISSET(listen_fd, &rfds)) {
            struct sockaddr_in caddr;
            socklen_t clen = sizeof(caddr);
            int cfd = accept(listen_fd, (struct sockaddr *)&caddr, &clen);
            if (cfd >= 0) {
                int slot = -1;
                for (i = 0; i < MAX_CLIENTS; i++) if (clients[i].fd < 0) { slot = i; break; }
                if (slot >= 0) {
                    clients[slot].fd = cfd;
                    clients[slot].id = client_id_next++;
                    clients[slot].joined = 0;
                    clients[slot].room = -1;
                    clients[slot].rlen = 0;
                    clients[slot].olen = 0;
                    clients[slot].name[0] = '\0';
                    log_info("新连接来自 %s:%u, 分配 id=%d",
                             inet_ntoa(caddr.sin_addr), (unsigned)ntohs(caddr.sin_port), clients[slot].id);
                } else {
                    log_info("连接已满，拒绝 %s", inet_ntoa(caddr.sin_addr));
                    close(cfd);
                }
            }
        }

        for (i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].fd < 0) continue;
            if (FD_ISSET(clients[i].fd, &rfds)) process_input(&clients[i]);
            if (clients[i].fd >= 0 && FD_ISSET(clients[i].fd, &wfds)) out_flush(&clients[i]);
        }

        now = now_ms();
        while (now - last_tick >= TICK_MS) {
            last_tick += TICK_MS;
            for (i = 0; i < MAX_ROOMS; i++) {
                room_t *r = &rooms[i];
                if (!r->active) continue;
                if (r->restart_ticks > 0) {
                    r->restart_ticks--;
                    if (r->restart_ticks == 0) {
                        for (j = 0; j < SNAKE_MAX_PER_ROOM; j++)
                            if (r->players[j].client_id >= 0) spawn_player(r, j);
                        init_foods(r);           /* 重新铺满小食物并生成大食物 */
                        r->round_started = 1;
                        /* 广播新一局开始 */
                        for (j = 0; j < SNAKE_MAX_PER_ROOM; j++) {
                            player_t *p = &r->players[j];
                            if (p->client_id < 0) continue;
                            client_t *cc = client_by_id(p->client_id);
                            if (cc) out_enqueue(cc, MSG_ROUND, NULL, 0);
                        }
                    }
                } else if (r->round_started) {
                    advance_tick(r);
                }
                broadcast_state(r);
            }
            now = now_ms();
        }
    }

    for (i = 0; i < MAX_CLIENTS; i++) if (clients[i].fd >= 0) close(clients[i].fd);
    close(listen_fd);
    log_info("服务端退出");
    return 0;
}

/*
 * snakeInit.c
 *
 * 贪吃蛇游戏入口（客户端协调器）
 *
 * 流程：用户名屏 -> 连接服务器(192.168.72.23) -> 主菜单(单人/多人) ->
 *       多人房间选择 -> 游戏屏(摇杆控制)。
 *
 * 负责：网络消息轮询、二进制帧收发、屏幕切换、把摇杆/按钮操作转成二进制消息。
 *
 * 模块分工：
 *   net    —— TCP + 后台接收线程 + 帧队列（纯二进制协议）
 *   snake  —— 蛇/世界的"数据结构 + STATE 帧解析"
 *   page   —— 界面布局（用户名/主菜单/房间/游戏屏 + 摇杆）
 *   本文件 —— 协调上面三者。
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "../lvgl/lvgl.h"

#include "net/net.h"
#include "net/protocol.h"
#include "snake/snake.h"
#include "page/inc/ui_page.h"

/* 服务器 IP（固定）与端口 */
#define SERVER_IP    "192.168.72.23"
#define NET_TIMEOUT_MS 3000

/* ---------------- 模块状态 ---------------- */
static snake_world_t s_world;
static lv_obj_t *s_name_scr = NULL;
static lv_obj_t *s_main_scr = NULL;
static lv_obj_t *s_room_scr = NULL;
static lv_obj_t *s_game_scr = NULL;

static char s_name[SNAKE_MAX_NAME + 1] = "玩家";
static int  s_joined = 0;        /* 已发送 join */
static int  s_connected = 0;     /* 连接成功（应在主界面） */
static int  s_room = -1;         /* 当前房间号 */
static int  s_wrap = 0;          /* 地图偏好：0=经典(撞墙死), 1=环形(穿墙) */

/* ---------------- 发送二进制帧 ---------------- */
static void send_join(void)
{
    uint8_t buf[SNAKE_MAX_NAME];
    memset(buf, 0, sizeof(buf));
    strncpy((char *)buf, s_name, sizeof(buf));
    net_send_msg(MSG_JOIN, buf, sizeof(buf));
}

static void send_mode(int single)
{
    uint8_t b[2];
    b[0] = single ? 0 : 1;      /* 0=single, 1=multi */
    b[1] = (uint8_t)s_wrap;     /* 0=经典, 1=环形 */
    net_send_msg(MSG_MODE, b, sizeof(b));
}

static void send_dir(snake_dir_t d)
{
    uint8_t b = (uint8_t)d;
    net_send_msg(MSG_DIR, &b, 1);
}

static void send_use_skill(int skill)
{
    uint8_t b = (uint8_t)skill;   /* 0=加速, 1=护盾 */
    net_send_msg(MSG_USE_SKILL, &b, 1);
}

static void send_room_list(void)    { net_send_msg(MSG_ROOM_LIST, NULL, 0); }
static void send_create_room(void)
{
    uint8_t b = (uint8_t)s_wrap;
    net_send_msg(MSG_CREATE_ROOM, &b, 1);
}
static void send_random_join(void)  { net_send_msg(MSG_RANDOM_JOIN, NULL, 0); }
static void send_leave(void)        { net_send_msg(MSG_LEAVE, NULL, 0); }

static void send_join_room(int room_id)
{
    uint8_t b[4];
    pr_put_u32(b, (uint32_t)room_id);
    net_send_msg(MSG_JOIN_ROOM, b, sizeof(b));
}

/* ---------------- 回调：用户名 ---------------- */
static void on_name(const char *name)
{
    snprintf(s_name, sizeof(s_name), "%s", name ? name : "玩家");
    s_joined = 0;
    s_room = -1;

    net_cfg_t cfg;
    cfg.host = SERVER_IP;
    cfg.port = SERVER_PORT;
    cfg.timeout_ms = NET_TIMEOUT_MS;
    net_connect(&cfg);
    ui_name_set_status(s_name_scr, "正在连接...");
}

/* ---------------- 回调：主菜单模式 ---------------- */
static void on_mode(const char *mode)
{
    if (!mode) return;
    if (strcmp(mode, MODE_SINGLE) == 0) {
        send_mode(1);
    } else if (strcmp(mode, MODE_MULTI) == 0) {
        send_mode(0);
        lv_scr_load(s_room_scr);
        ui_room_set_status(s_room_scr, "加载房间...");
    }
}

/* ---------------- 回调：主菜单地图选择 ---------------- */
static void on_map(int wrap)
{
    s_wrap = wrap ? 1 : 0;
}

/* ---------------- 回调：房间屏操作 ---------------- */
static void on_room(int action, int room_id)
{
    switch (action) {
        case UI_ROOM_CREATE:
            send_create_room();
            break;
        case UI_ROOM_JOIN:
            send_join_room(room_id);
            break;
        case UI_ROOM_RANDOM:
            send_random_join();
            break;
        case UI_ROOM_BACK:
            lv_scr_load(s_main_scr);
            break;
    }
}

/* ---------------- 回调：游戏屏 ---------------- */
static void on_quit(void)
{
    send_leave();
    s_room = -1;
    snake_world_reset_round(&s_world);
    if (s_game_scr) ui_game_clear_over(s_game_scr);
    lv_scr_load(s_main_scr);
}

static void on_dir(snake_dir_t d)
{
    send_dir(d);
}

static void on_skill(int skill)
{
    send_use_skill(skill);
}

/* ---------------- 断线 ---------------- */
static void handle_disconnect(void)
{
    s_joined = 0;
    s_connected = 0;
    s_room = -1;
    lv_scr_load(s_name_scr);
    ui_name_set_status(s_name_scr, "连接断开，请重试。");
}

/* ---------------- 消息分发（一帧二进制） ---------------- */
static void handle_message(uint8_t type, const uint8_t *p, int plen)
{
    switch (type) {
        case MSG_WELCOME: {
            if (plen < 8) break;
            s_world.my_id = (int)pr_get_u32(p);
            s_world.room = -1;
            s_world.cols = pr_get_u16(p + 4);   /* 默认地图尺寸（服务器按环形下发），进房后以房间为准 */
            s_world.rows = pr_get_u16(p + 6);
            ui_game_set_my_id(s_world.my_id);
            s_connected = 1;
            lv_scr_load(s_main_scr);
            ui_main_set_status(s_main_scr, "已连接");
            break;
        }
        case MSG_ROOMS: {
            if (plen < 1) break;
            int n = p[0], i;
            int ids[64], players[64], maxs[64], wraps[64];
            if (n > 64) n = 64;
            for (i = 0; i < n; i++) {
                if (1 + i * 7 + 7 > plen) break;
                const uint8_t *e = p + 1 + i * 7;
                ids[i]     = (int)pr_get_u32(e);
                players[i] = e[4];
                maxs[i]    = e[5];
                wraps[i]   = e[6];
            }
            ui_room_refresh(s_room_scr, ids, players, maxs, wraps, i);
            ui_room_set_status(s_room_scr, i ? "请选择房间" : "暂无房间，请创建");
            break;
        }
        case MSG_ROOM: {
            if (plen < 10) break;
            s_room = (int)pr_get_u32(p);
            s_world.room = s_room;
            s_world.wrap = p[5] ? 1 : 0;          /* 0=经典, 1=环形 */
            s_world.cols = pr_get_u16(p + 6);     /* 该房间地图尺寸 */
            s_world.rows = pr_get_u16(p + 8);
            snake_world_reset_round(&s_world);
            if (!s_game_scr) s_game_scr = ui_game_screen_create(NULL, on_quit, on_dir, on_skill);
            ui_game_set_my_id(s_world.my_id);
            ui_game_clear_over(s_game_scr);
            ui_game_update(s_game_scr, &s_world);
            lv_scr_load(s_game_scr);
            break;
        }
        case MSG_STATE: {
            if (snake_parse_state(&s_world, p, plen) == 0 && s_game_scr) {
                ui_game_update(s_game_scr, &s_world);
            }
            break;
        }
        case MSG_OVER: {
            if (plen < 4) break;
            s_world.winner_id = (int)pr_get_i32(p);
            s_world.game_over = 1;
            if (s_game_scr) ui_game_set_over(s_game_scr, &s_world);
            break;
        }
        case MSG_ROUND: {
            s_world.game_over = 0;
            snake_world_reset_round(&s_world);
            if (s_game_scr) ui_game_clear_over(s_game_scr);
            break;
        }
        case MSG_ERROR: {
            char msg[128];
            int n = plen < (int)sizeof(msg) - 1 ? plen : (int)sizeof(msg) - 1;
            if (n > 0) { memcpy(msg, p, (size_t)n); msg[n] = '\0'; }
            else { msg[0] = '\0'; }
            if (s_room >= 0 && s_game_scr) {
                /* 游戏里出错，忽略 */
            } else if (lv_scr_act() == s_room_scr) {
                ui_room_show_error(s_room_scr, msg);
            } else if (lv_scr_act() == s_main_scr) {
                ui_main_set_status(s_main_scr, msg);
            } else {
                ui_name_set_status(s_name_scr, msg);
            }
            break;
        }
        default:
            break;
    }
}

/* ---------------- 周期轮询 ---------------- */
static void poll_timer(lv_timer_t *timer)
{
    (void)timer;
    uint8_t type;
    uint8_t buf[16384];
    int plen;
    int r;

    while ((r = net_poll_msg(&type, buf, sizeof(buf), &plen)) != 0) {
        if (r < 0) { handle_disconnect(); break; }
        if (type == MSG_NETCLOSED) { handle_disconnect(); break; }
        handle_message(type, buf, plen);
    }

    /* 连接建立后立即发送 join（不能等 welcome，否则服务端不会回 welcome） */
    if (!s_joined && net_state() == NET_CONNECTED) {
        send_join();
        s_joined = 1;
    }
}

/* ---------------- 入口 ---------------- */
void snakeInit(void)
{
    snake_world_init(&s_world);
    net_init();

    s_name_scr = ui_name_screen_create(NULL, on_name, "玩家");
    s_main_scr = ui_main_screen_create(NULL, on_mode, on_map);
    s_room_scr = ui_room_screen_create(NULL, on_room);
    lv_scr_load(s_name_scr);

    lv_timer_create(poll_timer, 30, NULL);
}

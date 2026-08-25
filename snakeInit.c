/*
 * snakeInit.c
 *
 * 贪吃蛇游戏入口（客户端协调器）
 *
 * 流程：用户名屏 -> 连接服务器(192.168.72.23) -> 主菜单(单人/多人) ->
 *       多人房间选择 -> 游戏屏(摇杆控制)。
 *
 * 负责：网络消息轮询、JSON 收发、屏幕切换、把摇杆/按钮操作转成 JSON 消息。
 *
 * 模块分工：
 *   net    —— TCP + 后台接收线程 + 消息队列
 *   snake  —— 蛇/世界的“数据结构 + JSON STATE 解析”
 *   page   —— 界面布局（用户名/主菜单/房间/游戏屏 + 摇杆）
 *   本文件 —— 协调上面三者。
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "lvgl/lvgl.h"

#include "net/net.h"
#include "net/protocol.h"
#include "net/lib/cJSON.h"
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

static char s_name[SNAKE_MAX_NAME + 1] = "Player";
static int  s_joined = 0;        /* 已发送 join */
static int  s_connected = 0;     /* 连接成功（应在主界面） */
static int  s_room = -1;         /* 当前房间号 */

/* 从 JSON 对象取值（整数），缺省返回 def */
static int get_v(cJSON *o, const char *key, int def)
{
    cJSON *it = cJSON_GetObjectItem(o, key);
    return (cJSON_IsNumber(it)) ? it->valueint : def;
}

/* ---------------- 发送 JSON ---------------- */
static void send_json(cJSON *o)
{
    char *s = cJSON_PrintUnformatted(o);
    if (s) { net_send(s); free(s); }
    cJSON_Delete(o);
}

static void send_simple(const char *type)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", type);
    send_json(o);
}

static void send_join(void)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", MSG_JOIN);
    cJSON_AddStringToObject(o, "name", s_name);
    send_json(o);
}

static void send_mode(const char *m)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", MSG_MODE);
    cJSON_AddStringToObject(o, "mode", m);
    send_json(o);
}

static void send_dir(snake_dir_t d)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", MSG_DIR);
    const char *dir = DIR_STRING_RIGHT;
    switch (d) {
        case SNAKE_DIR_UP:    dir = DIR_STRING_UP;    break;
        case SNAKE_DIR_DOWN:  dir = DIR_STRING_DOWN;  break;
        case SNAKE_DIR_LEFT:  dir = DIR_STRING_LEFT;  break;
        case SNAKE_DIR_RIGHT: dir = DIR_STRING_RIGHT; break;
    }
    cJSON_AddStringToObject(o, "dir", dir);
    send_json(o);
}

/* ---------------- 回调：用户名 ---------------- */
static void on_name(const char *name)
{
    snprintf(s_name, sizeof(s_name), "%s", name ? name : "Player");
    s_joined = 0;
    s_room = -1;

    net_cfg_t cfg;
    cfg.host = SERVER_IP;
    cfg.port = SERVER_PORT;
    cfg.timeout_ms = NET_TIMEOUT_MS;
    net_connect(&cfg);
    ui_name_set_status(s_name_scr, "Connecting...");
}

/* ---------------- 回调：主菜单模式 ---------------- */
static void on_mode(const char *mode)
{
    if (!mode) return;
    if (strcmp(mode, MODE_SINGLE) == 0) {
        send_mode(MODE_SINGLE);
    } else if (strcmp(mode, MODE_MULTI) == 0) {
        send_mode(MODE_MULTI);
        lv_scr_load(s_room_scr);
        ui_room_set_status(s_room_scr, "Loading rooms...");
    }
}

/* ---------------- 回调：房间屏操作 ---------------- */
static void on_room(int action, int room_id)
{
    switch (action) {
        case UI_ROOM_CREATE:
            send_simple(MSG_CREATE_ROOM);
            break;
        case UI_ROOM_JOIN:
        {
            cJSON *o = cJSON_CreateObject();
            cJSON_AddStringToObject(o, "type", MSG_JOIN_ROOM);
            cJSON_AddNumberToObject(o, "room", room_id);
            send_json(o);
            break;
        }
        case UI_ROOM_RANDOM:
            send_simple(MSG_RANDOM_JOIN);
            break;
        case UI_ROOM_BACK:
            lv_scr_load(s_main_scr);
            break;
    }
}

/* ---------------- 回调：游戏屏 ---------------- */
static void on_quit(void)
{
    send_simple(MSG_LEAVE);
    s_room = -1;
    snake_world_reset_round(&s_world);
    if (s_game_scr) ui_game_clear_over(s_game_scr);
    lv_scr_load(s_main_scr);
}

static void on_dir(snake_dir_t d)
{
    send_dir(d);
}

/* ---------------- 断线 ---------------- */
static void handle_disconnect(void)
{
    s_joined = 0;
    s_connected = 0;
    s_room = -1;
    lv_scr_load(s_name_scr);
    ui_name_set_status(s_name_scr, "Connection lost. Try again.");
}

/* ---------------- 消息分发 ---------------- */
static void handle_message(char *line)
{
    cJSON *o = cJSON_Parse(line);
    if (!o) return;
    cJSON *t = cJSON_GetObjectItem(o, "type");
    if (!t || !cJSON_IsString(t)) { cJSON_Delete(o); return; }
    const char *type = t->valuestring;

    if (strcmp(type, MSG_WELCOME) == 0) {
        s_world.my_id = get_v(o, "id", -1);
        s_world.room = -1;
        ui_game_set_my_id(s_world.my_id);
        s_connected = 1;
        lv_scr_load(s_main_scr);
        ui_main_set_status(s_main_scr, "Connected");
    }
    else if (strcmp(type, MSG_ROOMS) == 0) {
        cJSON *arr = cJSON_GetObjectItem(o, "rooms");
        int ids[64], players[64], maxs[64];
        int n = 0;
        if (cJSON_IsArray(arr)) {
            cJSON *it;
            cJSON_ArrayForEach(it, arr) {
                if (n >= 64) break;
                ids[n]     = get_v(it, "id", 0);
                players[n] = get_v(it, "players", 0);
                maxs[n]    = get_v(it, "max", 8);
                n++;
            }
        }
        ui_room_refresh(s_room_scr, ids, players, maxs, n);
        ui_room_set_status(s_room_scr, n ? "Select a room" : "No rooms - Create or Random");
    }
    else if (strcmp(type, MSG_ROOM) == 0) {
        s_room = get_v(o, "room", -1);
        s_world.room = s_room;
        snake_world_reset_round(&s_world);
        if (!s_game_scr) s_game_scr = ui_game_screen_create(NULL, on_quit, on_dir);
        ui_game_set_my_id(s_world.my_id);
        ui_game_clear_over(s_game_scr);
        ui_game_update(s_game_scr, &s_world);
        lv_scr_load(s_game_scr);
    }
    else if (strcmp(type, MSG_STATE) == 0) {
        if (snake_parse_state(&s_world, line) == 0 && s_game_scr) {
            ui_game_update(s_game_scr, &s_world);
        }
    }
    else if (strcmp(type, MSG_OVER) == 0) {
        s_world.winner_id = get_v(o, "winner", -1);
        s_world.game_over = 1;
        if (s_game_scr) ui_game_set_over(s_game_scr, &s_world);
    }
    else if (strcmp(type, MSG_ROUND) == 0) {
        s_world.game_over = 0;
        snake_world_reset_round(&s_world);
        if (s_game_scr) ui_game_clear_over(s_game_scr);
    }
    else if (strcmp(type, MSG_ERROR) == 0) {
        cJSON *m = cJSON_GetObjectItem(o, "msg");
        const char *msg = (m && cJSON_IsString(m)) ? m->valuestring : "Error";
        if (s_room >= 0 && s_game_scr) {
            /* 游戏里出错，忽略 */
        } else if (lv_scr_act() == s_room_scr) {
            ui_room_set_status(s_room_scr, msg);
        } else if (lv_scr_act() == s_main_scr) {
            ui_main_set_status(s_main_scr, msg);
        } else {
            ui_name_set_status(s_name_scr, msg);
        }
    }
    cJSON_Delete(o);
}

/* ---------------- 周期轮询 ---------------- */
static void poll_timer(lv_timer_t *timer)
{
    (void)timer;
    char line[16384];
    int r;

    while ((r = net_poll(line, sizeof(line))) != 0) {
        if (r < 0) { handle_disconnect(); break; }
        if (strcmp(line, PROTO_NETCLOSED) == 0) { handle_disconnect(); break; }
        handle_message(line);
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

    s_name_scr = ui_name_screen_create(NULL, on_name, "Player");
    s_main_scr = ui_main_screen_create(NULL, on_mode);
    s_room_scr = ui_room_screen_create(NULL, on_room);
    lv_scr_load(s_name_scr);

    lv_timer_create(poll_timer, 30, NULL);
}

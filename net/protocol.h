/*
 * protocol.h  （客户端副本）
 *
 * 必须与 server/inc/protocol.h 保持完全一致，否则无法互通。
 * 详见服务端 protocol.h 中的 JSON 协议说明。
 */

#ifndef SNAKE_PROTOCOL_H
#define SNAKE_PROTOCOL_H

/* ---------------- JSON 消息类型 ---------------- */
#define MSG_JOIN        "join"
#define MSG_MODE        "mode"
#define MSG_ROOM_LIST   "room_list"
#define MSG_CREATE_ROOM "create_room"
#define MSG_JOIN_ROOM   "join_room"
#define MSG_RANDOM_JOIN "random_join"
#define MSG_DIR         "dir"
#define MSG_LEAVE       "leave"
#define MSG_BYE         "bye"

#define MSG_WELCOME     "welcome"
#define MSG_ROOMS       "rooms"
#define MSG_ROOM        "room"
#define MSG_STATE       "state"
#define MSG_OVER        "over"
#define MSG_ROUND       "round"
#define MSG_ERROR       "error"

/* 网络线程在连接断开时注入本内容，主线程据此感知断线（非 JSON）。 */
#define PROTO_NETCLOSED "__NETCLOSED__"

/* ---------------- 方向 ---------------- */
#define DIR_STRING_UP     "up"
#define DIR_STRING_DOWN   "down"
#define DIR_STRING_LEFT   "left"
#define DIR_STRING_RIGHT  "right"

/* ---------------- 模式 ---------------- */
#define MODE_SINGLE  "single"
#define MODE_MULTI   "multi"

/* ---------------- 地图 / 游戏常量 ---------------- */
#define SERVER_PORT          5000
#define SNAKE_COLS           36
#define SNAKE_ROWS           22
#define SNAKE_FOOD_COUNT     3
#define SNAKE_MAX_LEN        256
#define SNAKE_MAX_NAME       16
#define SNAKE_MAX_PER_ROOM   8
#define SNAKE_INV_TICKS      80      /* 8s 无敌（服务端数值，客户端仅用于展示） */

/* ---------------- 行缓冲 ---------------- */
#define PROTO_LINE_MAX       2048

#endif /* SNAKE_PROTOCOL_H */

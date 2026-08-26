/*
 * net.h
 *
 * 贪吃蛇客户端 —— 网络模块对外接口
 *
 * 网络模块在后台线程中维护一条到服务端的 TCP 连接：
 *   - net_connect() 建立连接并启动接收线程；
 *   - 接收线程不断地 recv 并把完整二进制帧放入线程安全队列；
 *   - 主线程（LVGL 线程）通过 net_poll_msg() 取出帧，通过 net_send_msg() 发送命令。
 *
 * 帧格式见 net/protocol.h：1 字节类型 + 2 字节负载长度(大端) + 负载。
 *
 * 注意：不要再接收线程里调用任何 LVGL API —— 绘图只能在主线程进行。
 */

#ifndef LUNAUI_NET_H
#define LUNAUI_NET_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 网络状态 */
typedef enum {
    NET_DISCONNECTED = 0,   /* 未连接 */
    NET_CONNECTING,         /* 正在建立连接 */
    NET_CONNECTED           /* 已连接 */
} net_state_t;

/* 连接目标：服务端的 IP 与端口，超时（毫秒，仅连接阶段有效） */
typedef struct {
    const char *host;
    int         port;
    int         timeout_ms;
} net_cfg_t;

/**
 * 初始化网络模块（创建互斥锁等）。使用其它函数前必须调用一次。
 * @return 0 成功
 */
int net_init(void);

/**
 * 连接服务端并启动接收线程。
 * @param cfg 连接配置
 * @return 0 表示连接成功；否则返回 -1（可调用 net_last_error() 查看原因）。
 *          本函数会阻塞，最长为 cfg->timeout_ms。
 */
int net_connect(const net_cfg_t *cfg);

/**
 * 断开连接并停止后台接收线程（可重连）。
 */
void net_close(void);

/**
 * 发送一帧二进制消息（自动附加帧头并发送）。
 * @param type    消息类型（见 protocol.h 的 MSG_*）
 * @param payload 负载数据，可为 NULL（plen 为 0）
 * @param plen    负载字节数（≤ PROTO_MAX_PAYLOAD）
 * @return 0 成功，-1 失败
 */
int net_send_msg(uint8_t type, const void *payload, uint16_t plen);

/**
 * 从接收队列取出一帧完整消息（不含帧头）。
 * @param type    输出消息类型；连接断开时置为 MSG_NETCLOSED
 * @param out     输出缓冲（负载数据，末尾补 '\0' 便于字符串负载使用）
 * @param outlen  输出缓冲大小
 * @param plen_out 输出负载实际长度（可为 NULL）
 * @return 1 取到一帧；0 暂无消息；-1 连接已断开
 */
int net_poll_msg(uint8_t *type, uint8_t *out, int outlen, int *plen_out);

/**
 * 查询当前连接状态。
 */
net_state_t net_state(void);

/**
 * 最近一次错误的描述（线程安全，仅作展示用）。
 */
const char *net_last_error(void);

#ifdef __cplusplus
}
#endif

#endif /* LUNAUI_NET_H */

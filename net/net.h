/*
 * net.h
 *
 * 贪吃蛇客户端 —— 网络模块对外接口
 *
 * 网络模块在后台线程中维护一条到服务端的 TCP 连接：
 *   - net_connect() 建立连接并启动接收线程；
 *   - 接收线程不断地 recv 并把完整消息放入线程安全队列；
 *   - 主线程（LVGL 线程）通过 net_poll() 取出消息，通过 net_send() 发送命令。
 *
 * 注意：不要再接收线程里调用任何 LVGL API —— 绘图只能在主线程进行。
 */

#ifndef LUNAUI_NET_H
#define LUNAUI_NET_H

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
 * 发送一行消息（内部自动追加 '\n' 并发送）。
 * @param line 一行文本（不含 '\n'）
 * @return 0 成功，-1 失败
 */
int net_send(const char *line);

/**
 * 从接收队列取出一条完整消息（不含 '\n'，以 '\0' 结尾）。
 * @param out    输出缓冲
 * @param outlen 输出缓冲大小
 * @return 1 取到一条；0 暂无消息；-1 连接已断开（此时 out 可能为
 *         PROTO_NETCLOSED 内容，应据此提示用户）
 */
int net_poll(char *out, int outlen);

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

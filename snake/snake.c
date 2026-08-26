/*
 * snake.c
 *
 * 贪吃蛇客户端 —— "蛇结构"数据模型实现
 *
 * 解析服务端下发的二进制 STATE 帧（负载，不含帧头），
 * 还原为 snake_world_t，供界面层（page/ui_game.c）直接使用其数据进行渲染。
 *
 * STATE 帧格式见 net/protocol.h。
 */

#include <stdio.h>
#include <string.h>

#include "snake.h"
#include "../net/protocol.h"

void snake_world_init(snake_world_t *w)
{
    if (!w) return;
    memset(w, 0, sizeof(*w));
    w->my_id = -1;
    w->winner_id = -1;
    w->my_score = 0;
    w->room = -1;
}

void snake_world_reset_round(snake_world_t *w)
{
    int i;
    if (!w) return;
    for (i = 0; i < SNAKE_MAX_PER_ROOM; i++) {
        w->snakes[i].id = -1;
        w->snakes[i].len = 0;
        w->snakes[i].alive = 0;
        w->snakes[i].score = 0;
        w->snakes[i].inv = 0;
        w->snakes[i].small_eaten = 0;
    }
    w->nsnakes = 0;
    w->food_count = 0;
    w->winner_id = -1;
    w->game_over = 0;
    w->my_score = 0;
}

/* 解析一帧二进制 STATE 负载：
 *   tick u32, small_n u16, small_n×{px u16,py u16}, big_n u8, big_n×{px u16,py u16},
 *   nsnakes u8, nsnakes×{id u32,color u8,len u16,score u16,inv u8,small_eaten u8,
 *                        name[16], body[len×{x u16,y u16}]}
 * 所有整数均为大端序。 */
int snake_parse_state(snake_world_t *w, const uint8_t *payload, int plen)
{
    const uint8_t *p = payload;
    int remain = plen;
    int i, cnt;

    if (!w || !payload || plen < 7) return -1;

    /* tick */
    w->tick = (int)pr_get_u32(p); p += 4; remain -= 4;

    /* 小食物 */
    cnt = pr_get_u16(p); p += 2; remain -= 2;
    if (cnt > SNAKE_SMALL_FOOD_MAX) cnt = SNAKE_SMALL_FOOD_MAX;
    w->food_count = 0;
    for (i = 0; i < cnt; i++) {
        if (remain < 4) return -1;
        snake_food_t *f = &w->foods[w->food_count++];
        f->x = pr_get_u16(p); p += 2; remain -= 2;
        f->y = pr_get_u16(p); p += 2; remain -= 2;
        f->kind = 0;
    }

    /* 大食物 */
    if (remain < 1) return -1;
    cnt = pr_get_u8(p); p += 1; remain -= 1;
    if (cnt > SNAKE_BIG_FOOD_MAX) cnt = SNAKE_BIG_FOOD_MAX;
    for (i = 0; i < cnt; i++) {
        if (remain < 4) return -1;
        snake_food_t *f = &w->foods[w->food_count++];
        f->x = pr_get_u16(p); p += 2; remain -= 2;
        f->y = pr_get_u16(p); p += 2; remain -= 2;
        f->kind = 1;
    }

    /* 蛇 */
    if (remain < 1) return -1;
    w->nsnakes = pr_get_u8(p); p += 1; remain -= 1;
    if (w->nsnakes > SNAKE_MAX_PER_ROOM) w->nsnakes = SNAKE_MAX_PER_ROOM;
    for (i = 0; i < w->nsnakes; i++) {
        snake_player_t *s = &w->snakes[i];
        int len, b;
        if (remain < 4 + 1 + 2 + 2 + 1 + 1 + SNAKE_MAX_NAME) return -1;
        s->id    = (int)pr_get_u32(p); p += 4; remain -= 4;
        s->color = pr_get_u8(p); p += 1; remain -= 1;
        len      = pr_get_u16(p); p += 2; remain -= 2;
        s->score = pr_get_u16(p); p += 2; remain -= 2;
        s->inv   = pr_get_u8(p); p += 1; remain -= 1;
        s->small_eaten = pr_get_u8(p); p += 1; remain -= 1;
        memcpy(s->name, p, SNAKE_MAX_NAME);
        s->name[SNAKE_MAX_NAME] = '\0';
        p += SNAKE_MAX_NAME; remain -= SNAKE_MAX_NAME;

        if (len < 1) len = 1;
        if (len > SNAKE_MAX_LEN) len = SNAKE_MAX_LEN;
        s->len = len;

        for (b = 0; b < len; b++) {
            if (remain < 4) return -1;
            s->body[b].x = pr_get_u16(p); p += 2; remain -= 2;
            s->body[b].y = pr_get_u16(p); p += 2; remain -= 2;
        }
        s->alive = 1;
    }

    /* 更新本机得分 */
    for (i = 0; i < w->nsnakes; i++) {
        if (w->snakes[i].id == w->my_id) { w->my_score = w->snakes[i].score; break; }
    }

    return 0;
}

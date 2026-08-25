/*
 * snake.c
 *
 * 贪吃蛇客户端 —— “蛇结构”数据模型实现
 *
 * 用 cJSON 解析服务端下发的 JSON STATE 帧，还原为 snake_world_t，
 * 供界面层（page/ui_game.c）直接使用其数据进行渲染。
 */

#include <stdio.h>
#include <string.h>

#include "snake.h"
#include "../net/protocol.h"
#include "../net/lib/cJSON.h"

/* 从 JSON 对象取值（整数），缺省返回 def */
static int get_value(cJSON *o, const char *key, int def)
{
    cJSON *it = cJSON_GetObjectItem(o, key);
    return (cJSON_IsNumber(it)) ? it->valueint : def;
}

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
    }
    w->nsnakes = 0;
    w->food_count = 0;
    w->winner_id = -1;
    w->game_over = 0;
    w->my_score = 0;
}

/* 解析一帧 JSON STATE：
 * {"type":"state","tick":..,"foods":[{"x":..,"y":..},...],
 *  "snakes":[{"id":..,"name":..,"color":..,"len":..,"score":..,"inv":..,
 *             "body":[{"x":..,"y":..},...]},...]} */
int snake_parse_state(snake_world_t *w, const char *json)
{
    cJSON *root, *foodsArr, *snakesArr, *it;
    int i, cnt;

    if (!w || !json) return -1;
    root = cJSON_Parse(json);
    if (!root) return -1;

    it = cJSON_GetObjectItem(root, "type");
    if (!it || !cJSON_IsString(it) || strcmp(it->valuestring, MSG_STATE) != 0) {
        cJSON_Delete(root);
        return -1;
    }

    w->tick = get_value(root, "tick", 0);

    /* 食物 */
    w->food_count = 0;
    foodsArr = cJSON_GetObjectItem(root, "foods");
    if (cJSON_IsArray(foodsArr)) {
        cnt = 0;
        cJSON_ArrayForEach(it, foodsArr) {
            if (cnt >= SNAKE_FOOD_COUNT) break;
            cJSON *jx = cJSON_GetObjectItem(it, "x");
            cJSON *jy = cJSON_GetObjectItem(it, "y");
            if (cJSON_IsNumber(jx) && cJSON_IsNumber(jy)) {
                w->foods[cnt].x = jx->valueint;
                w->foods[cnt].y = jy->valueint;
                cnt++;
            }
        }
        w->food_count = cnt;
    }

    /* 蛇 */
    w->nsnakes = 0;
    snakesArr = cJSON_GetObjectItem(root, "snakes");
    if (cJSON_IsArray(snakesArr)) {
        cJSON_ArrayForEach(it, snakesArr) {
            if (w->nsnakes >= SNAKE_MAX_PER_ROOM) break;
            snake_player_t *s = &w->snakes[w->nsnakes];
            s->id    = get_value(it, "id", -1);
            s->color = get_value(it, "color", 0);
            s->len   = get_value(it, "len", 0);
            s->score = get_value(it, "score", 0);
            s->inv   = get_value(it, "inv", 0);
            cJSON *nm = cJSON_GetObjectItem(it, "name");
            if (cJSON_IsString(nm)) { snprintf(s->name, sizeof(s->name), "%s", nm->valuestring); }
            else s->name[0] = '\0';
            if (s->len < 1) s->len = 1;
            if (s->len > SNAKE_MAX_LEN) s->len = SNAKE_MAX_LEN;

            cJSON *body = cJSON_GetObjectItem(it, "body");
            int b = 0;
            if (cJSON_IsArray(body)) {
                cJSON *bm;
                cJSON_ArrayForEach(bm, body) {
                    if (b >= s->len) break;
                    cJSON *bx = cJSON_GetObjectItem(bm, "x");
                    cJSON *by = cJSON_GetObjectItem(bm, "y");
                    if (cJSON_IsNumber(bx) && cJSON_IsNumber(by)) {
                        s->body[b].x = bx->valueint;
                        s->body[b].y = by->valueint;
                        b++;
                    }
                }
            }
            s->alive = 1;
            w->nsnakes++;
        }
    }

    /* 更新本机得分 */
    for (i = 0; i < w->nsnakes; i++) {
        if (w->snakes[i].id == w->my_id) { w->my_score = w->snakes[i].score; break; }
    }

    cJSON_Delete(root);
    return 0;
}

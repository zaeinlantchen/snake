/*
 * font.c
 *
 * LunaUI（贪吃蛇客户端）—— FreeType 中文字体加载实现
 *
 * 原理：借助 LVGL 的 FreeType 扩展（lv_freetype）把外部 .ttf 文件渲染成
 * 位图字形，并以 lv_font_t 的形式提供给界面使用；一个字体文件可同时加载
 * 多档字号（weight）。加载失败的档位回退到内置 montserrat 字体。
 */

#include "font.h"

#include <stdio.h>
#include <string.h>

#if LV_USE_FREETYPE
#include "../lvgl/src/extra/libs/freetype/lv_freetype.h"
#endif

/* 各档字号，顺序与 s_fonts 对应 */
static lv_font_t *s_fonts[4] = { NULL, NULL, NULL, NULL };
static const uint16_t s_sizes[4] = { 14, 24, 26, 48 };

/* 字体文件候选路径（按顺序尝试，取第一个能成功打开并加载的）：
 *   - UI_FONT_PATH：编译期可改（默认 ./data/font/DroidSansFallbackFull.ttf，
 *     即 Pecpet 复制+重命名的部署位置）；
 *   - /fonts/STXINWEI.TTF：开发板 /fonts 目录下的华文新魏字体（绝对路径，
 *     与程序运行目录无关，Pecpet 同样使用该字体）。 */

int luna_font_init(void)
{
#if LV_USE_FREETYPE
    /* max_faces=1（仅一个字体文件），max_sizes=4（四档字号），
     * 位图缓存大小由 lv_conf.h 的 LV_FREETYPE_CACHE_SIZE 决定 */
    lv_freetype_init(1, 4, LV_FREETYPE_CACHE_SIZE);

        int any = 0;
        for (int i = 0; i < 4; i++) {
            lv_ft_info_t info;
            memset(&info, 0, sizeof(info));
            info.name   = UI_FONT_PATH;   /* 字体文件路径 */
            info.weight = s_sizes[i];         /* 字体大小（像素高） */
            info.style  = FT_FONT_STYLE_NORMAL;
            if (lv_ft_font_init(&info) && info.font) { s_fonts[i] = info.font; any = 1; }
        }
        if (any) {
            return 1;
        }
    return 0;
#else
    return 0;
#endif
}

lv_font_t *luna_font_small(void)  { return s_fonts[0] ? s_fonts[0] : &lv_font_montserrat_14; }
lv_font_t *luna_font_normal(void) { return s_fonts[1] ? s_fonts[1] : &lv_font_montserrat_24; }
lv_font_t *luna_font_medium(void) { return s_fonts[2] ? s_fonts[2] : &lv_font_montserrat_26; }
lv_font_t *luna_font_title(void)  { return s_fonts[3] ? s_fonts[3] : &lv_font_montserrat_48; }

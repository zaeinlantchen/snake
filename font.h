/*
 * font.h
 *
 * LunaUI（贪吃蛇客户端）—— FreeType 中文字体加载
 *
 * 通过 LVGL 的 FreeType 扩展把外部 .ttf 字体文件加载为多档 lv_font_t，
 * 供界面上的中文文案使用（内置 montserrat 字体不含中文字形）。
 *
 * 字体文件路径可修改：
 *   - 默认按候选顺序自动探测（见 font.c）："./data/font/DroidSansFallbackFull.ttf"
 *     （Pecpet 复制+重命名的部署位置）→ "/fonts/STXINWEI.TTF"（开发板字体目录）；
 *   - 或在编译时通过 -DUI_FONT_PATH="/path/to/xxx.ttf" 指定首选路径。
 */

#ifndef LUNAUI_FONT_H
#define LUNAUI_FONT_H

#include "../lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 中文字体文件路径（可修改，见文件头注释） */
#ifndef UI_FONT_PATH
#define UI_FONT_PATH "" // 配置Freetype字体路径
#endif

/* 初始化 FreeType 中文字体（需在 lv_init() 之后、创建界面之前调用）。
 * @return 1 = 至少成功创建一档字体；0 = 失败（界面回退到内置拉丁字体）。 */
int  luna_font_init(void);

/* 各档字号（加载失败时回退到内置 montserrat 字体，仅能显示拉丁字符） */
lv_font_t *luna_font_small(void);    /* 14 */
lv_font_t *luna_font_normal(void);   /* 24 */
lv_font_t *luna_font_medium(void);   /* 26 */
lv_font_t *luna_font_title(void);    /* 48 */

#ifdef __cplusplus
}
#endif

#endif /* LUNAUI_FONT_H */

/**
 * @file fbdev.h
 *
 */

#ifndef FBDEV_H
#define FBDEV_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#ifndef LV_DRV_NO_CONF
#ifdef LV_CONF_INCLUDE_SIMPLE
#include "lv_drv_conf.h"
#else
#include "../../lv_drv_conf.h"
#endif
#endif

#if USE_FBDEV || USE_BSD_FBDEV

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 * GLOBAL PROTOTYPES
 **********************/
void fbdev_init(void);
void fbdev_exit(void);
void fbdev_flush(lv_disp_drv_t * drv, const lv_area_t * area, lv_color_t * color_p);
void fbdev_get_sizes(uint32_t *width, uint32_t *height);

/** 刷屏统计：供 UI 显示 FPS / 局部或整屏 */
typedef struct {
    uint32_t fps;              /* 近 1s 内完整刷新周期次数 */
    uint32_t flush_last_w;     /* 最近一次 flush 脏区宽 */
    uint32_t flush_last_h;     /* 最近一次 flush 脏区高 */
    uint32_t flush_per_frame;  /* 上一完整周期内 flush 次数（>1 多为局部分块） */
    uint8_t  last_was_full;    /* 上一完整周期是否曾出现整屏脏区 */
    uint8_t  mode_partial;     /* 1=局部刷屏机制；0=驱动配置为整屏 */
} fbdev_perf_t;

void fbdev_get_perf(fbdev_perf_t * out);

/** 1：已成功启用 yres_virtual×2 + FBIOPAN_DISPLAY */
int fbdev_dblbuf_enabled(void);

/**
 * Set the X and Y offset in the variable framebuffer info.
 * @param xoffset horizontal offset
 * @param yoffset vertical offset
 */
void fbdev_set_offset(uint32_t xoffset, uint32_t yoffset);


/**********************
 *      MACROS
 **********************/

#endif  /*USE_FBDEV*/

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /*FBDEV_H*/

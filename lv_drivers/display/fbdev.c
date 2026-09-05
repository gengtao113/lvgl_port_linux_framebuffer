/**
 * @file fbdev.c
 *
 * Linux framebuffer：支持双缓冲 + FBIOPAN_DISPLAY。
 * 绘制写入「后台页」，一帧 flush 结束后 pan 到前台，减轻撕裂。
 */

/*********************
 *      INCLUDES
 *********************/
#include "fbdev.h"
#if USE_FBDEV || USE_BSD_FBDEV

#include <stdlib.h>
#include <unistd.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#if USE_BSD_FBDEV
#include <sys/fcntl.h>
#include <sys/time.h>
#include <sys/consio.h>
#include <sys/fbio.h>
#else
#include <linux/fb.h>
#endif

/*********************
 *      DEFINES
 *********************/
#ifndef FBDEV_PATH
#define FBDEV_PATH  "/dev/fb0"
#endif

/**********************
 *      STRUCTURES
 **********************/
struct bsd_fb_var_info {
    uint32_t xoffset;
    uint32_t yoffset;
    uint32_t xres;
    uint32_t yres;
    int bits_per_pixel;
};

struct bsd_fb_fix_info {
    long int line_length;
    long int smem_len;
};

/**********************
 *  STATIC VARIABLES
 **********************/
#if USE_BSD_FBDEV
static struct bsd_fb_var_info vinfo;
static struct bsd_fb_fix_info finfo;
#else
static struct fb_var_screeninfo vinfo;
static struct fb_fix_screeninfo finfo;
#endif

static char *fbp = 0;
static long int screensize = 0;
static int fbfd = 0;

/* 双缓冲 + pan */
static int s_dblbuf = 0;       /* 1：yres_virtual>=2*yres 且 pan 可用 */
static int s_front_page = 0;   /* 当前显示页 0/1 */
static long s_page_bytes = 0;  /* 单页字节数 line_length * yres */

/* 刷屏性能统计 */
static uint32_t s_fps = 0;              /**< 滑动窗口 FPS：每秒统计一次的帧数 */
static uint32_t s_frame_cnt_win = 0;    /**< 当前 1 秒统计窗口内累计的帧数 */
static uint32_t s_win_start_ms = 0;     /**< 当前统计窗口起始时间戳（lv_tick 毫秒） */
static uint32_t s_flush_in_frame = 0;   /**< 当前帧已发生的 flush 块数（帧末清零） */
static uint32_t s_flush_per_frame = 0;  /**< 上一帧的 flush 块数（帧末保存） */
static uint32_t s_flush_last_w = 0;     /**< 最近一次 flush 的脏区宽度（像素） */
static uint32_t s_flush_last_h = 0;     /**< 最近一次 flush 的脏区高度（像素） */
static uint8_t  s_frame_had_full = 0;   /**< 当前帧是否出现过整屏脏区（帧末清零） */
static uint8_t  s_last_was_full = 0;    /**< 上一帧是否整屏刷新（帧末保存） */

#if USE_BSD_FBDEV
#define FBIOBLANK FBIO_BLANK
#endif

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void fbdev_init(void)
{
    fbfd = open(FBDEV_PATH, O_RDWR);
    if(fbfd == -1) {
        perror("Error: cannot open framebuffer device");
        return;
    }
    LV_LOG_INFO("The framebuffer device was opened successfully");

    if(ioctl(fbfd, FBIOBLANK, FB_BLANK_UNBLANK) != 0) {
        perror("ioctl(FBIOBLANK)");
        return;
    }

#if USE_BSD_FBDEV
    struct fbtype fb;
    unsigned line_length;

    if(ioctl(fbfd, FBIOGTYPE, &fb) != 0) {
        perror("ioctl(FBIOGTYPE)");
        return;
    }
    if(ioctl(fbfd, FBIO_GETLINEWIDTH, &line_length) != 0) {
        perror("ioctl(FBIO_GETLINEWIDTH)");
        return;
    }

    vinfo.xres = (unsigned)fb.fb_width;
    vinfo.yres = (unsigned)fb.fb_height;
    vinfo.bits_per_pixel = fb.fb_depth;
    vinfo.xoffset = 0;
    vinfo.yoffset = 0;
    finfo.line_length = line_length;
    finfo.smem_len = finfo.line_length * vinfo.yres;
    s_dblbuf = 0;
#else
    if(ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo) == -1) {
        perror("Error reading fixed information");
        return;
    }
    if(ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo) == -1) {
        perror("Error reading variable information");
        return;
    }

    /* 申请双倍虚拟高度，供 FBIOPAN_DISPLAY 翻页 */
    vinfo.xres_virtual = vinfo.xres;
    vinfo.yres_virtual = vinfo.yres * 2;
    vinfo.xoffset = 0;
    vinfo.yoffset = 0;
    if(ioctl(fbfd, FBIOPUT_VSCREENINFO, &vinfo) == -1) {
        perror("FBIOPUT_VSCREENINFO (double buffer)");
        /* 回退：重新读回当前模式，单缓冲 */
        ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo);
        s_dblbuf = 0;
    }
    else {
        ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo);
        ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo);
        s_dblbuf = (vinfo.yres_virtual >= vinfo.yres * 2) ? 1 : 0;
        if(!s_dblbuf) {
            fprintf(stderr, "fbdev: yres_virtual=%u < 2*%u, pan disabled\n",
                    vinfo.yres_virtual, vinfo.yres);
        }
    }
#endif

    LV_LOG_INFO("%dx%d, %dbpp, yvirt=%u dblbuf=%d",
                vinfo.xres, vinfo.yres, vinfo.bits_per_pixel,
                (unsigned)vinfo.yres_virtual, s_dblbuf);

    screensize = finfo.smem_len;
    s_page_bytes = (long)finfo.line_length * (long)vinfo.yres;
    s_front_page = 0;

    fbp = (char *)mmap(0, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
    if((intptr_t)fbp == -1) {
        perror("Error: failed to map framebuffer device to memory");
        fbp = NULL;
        return;
    }

    /* 清两页，避免翻页时闪旧内容 */
    memset(fbp, 0, (size_t)screensize);

    LV_LOG_INFO("fb mapped %ld bytes, page=%ld, dblbuf+pan=%s",
                screensize, s_page_bytes, s_dblbuf ? "ON" : "OFF");
}

void fbdev_exit(void)
{
    if(fbp && fbp != (char *)-1) {
        munmap(fbp, screensize);
        fbp = NULL;
    }
    if(fbfd > 0) {
        close(fbfd);
        fbfd = 0;
    }
}

/**
 * 写入后台页（双缓冲）或当前页（单缓冲）；最后一块 flush 时 pan。
 */
void fbdev_flush(lv_disp_drv_t * drv, const lv_area_t * area, lv_color_t * color_p)
{
    if(fbp == NULL ||
            area->x2 < 0 ||
            area->y2 < 0 ||
            area->x1 > (int32_t)vinfo.xres - 1 ||
            area->y1 > (int32_t)vinfo.yres - 1) {
        lv_disp_flush_ready(drv);
        return;
    }

    int32_t act_x1 = area->x1 < 0 ? 0 : area->x1;
    int32_t act_y1 = area->y1 < 0 ? 0 : area->y1;
    int32_t act_x2 = area->x2 > (int32_t)vinfo.xres - 1 ? (int32_t)vinfo.xres - 1 : area->x2;
    int32_t act_y2 = area->y2 > (int32_t)vinfo.yres - 1 ? (int32_t)vinfo.yres - 1 : area->y2;

    lv_coord_t w = (act_x2 - act_x1 + 1);

    /* 画到后台页：与当前显示页相反 */
    int back_page = s_dblbuf ? (1 - s_front_page) : 0;
    int32_t y_base = (int32_t)(back_page * (int)vinfo.yres);

    long int location = 0;

    if(vinfo.bits_per_pixel == 32 || vinfo.bits_per_pixel == 24) {
        uint32_t * fbp32 = (uint32_t *)fbp;
        int32_t y;
        for(y = act_y1; y <= act_y2; y++) {
            location = (act_x1) + (y + y_base) * finfo.line_length / 4;
            memcpy(&fbp32[location], (uint32_t *)color_p, (act_x2 - act_x1 + 1) * 4);
            color_p += w;
        }
    }
    else if(vinfo.bits_per_pixel == 16) {
        uint16_t * fbp16 = (uint16_t *)fbp;
        int32_t y;
        for(y = act_y1; y <= act_y2; y++) {
            location = (act_x1) + (y + y_base) * finfo.line_length / 2;
            memcpy(&fbp16[location], color_p, (act_x2 - act_x1 + 1) * 2);
            color_p += w;
        }
    }
    else if(vinfo.bits_per_pixel == 8) {
        uint8_t * fbp8 = (uint8_t *)fbp;
        int32_t y;
        for(y = act_y1; y <= act_y2; y++) {
            location = (act_x1) + (y + y_base) * finfo.line_length;
            memcpy(&fbp8[location], color_p, (act_x2 - act_x1 + 1));
            color_p += w;
        }
    }
    else if(vinfo.bits_per_pixel == 1) {
        uint8_t * fbp8 = (uint8_t *)fbp;
        int32_t x, y;
        unsigned char bit_location;
        long int byte_location;
        for(y = act_y1; y <= act_y2; y++) {
            for(x = act_x1; x <= act_x2; x++) {
                location = x + (y + y_base) * vinfo.xres;
                byte_location = location / 8;
                bit_location = location % 8;
                fbp8[byte_location] &= ~(((uint8_t)(1)) << bit_location);
                fbp8[byte_location] |= ((uint8_t)(color_p->full)) << bit_location;
                color_p++;
            }
            color_p += area->x2 - act_x2;
        }
    }

    /* 统计 */
    {
        uint32_t aw = (uint32_t)(act_x2 - act_x1 + 1);
        uint32_t ah = (uint32_t)(act_y2 - act_y1 + 1);
        s_flush_last_w = aw;
        s_flush_last_h = ah;
        s_flush_in_frame++;
        if(aw >= vinfo.xres && ah >= vinfo.yres) {
            s_frame_had_full = 1;
        }
    }

    /* 本帧最后一块：pan 显示后台页 */
    if(lv_disp_flush_is_last(drv)) {
#if !USE_BSD_FBDEV
        if(s_dblbuf) {
            vinfo.yoffset = (unsigned)(back_page * (int)vinfo.yres);
            vinfo.xoffset = 0;
            if(ioctl(fbfd, FBIOPAN_DISPLAY, &vinfo) == -1) {
                perror("FBIOPAN_DISPLAY");
            }
            else {
                s_front_page = back_page;
            }
        }
#endif
        s_flush_per_frame = s_flush_in_frame;
        s_last_was_full = s_frame_had_full;
        s_flush_in_frame = 0;
        s_frame_had_full = 0;

        uint32_t now = lv_tick_get();
        if(s_win_start_ms == 0) {
            s_win_start_ms = now;
        }
        s_frame_cnt_win++;
        if(now - s_win_start_ms >= 1000U) {
            s_fps = s_frame_cnt_win;
            s_frame_cnt_win = 0;
            s_win_start_ms = now;
        }
    }

    lv_disp_flush_ready(drv);
}

void fbdev_get_perf(fbdev_perf_t * out)
{
    if(out == NULL) {
        return;
    }
    out->fps = s_fps;
    out->flush_last_w = s_flush_last_w;
    out->flush_last_h = s_flush_last_h;
    out->flush_per_frame = s_flush_per_frame;
    out->last_was_full = s_last_was_full;
    /* 0=双缓冲+pan（或整屏）；1=局部单缓冲 */
    out->mode_partial = s_dblbuf ? 0 : 1;
}

int fbdev_dblbuf_enabled(void)
{
    return s_dblbuf;
}

void fbdev_get_sizes(uint32_t * width, uint32_t * height)
{
    if(width)
        *width = vinfo.xres;
    if(height)
        *height = vinfo.yres;
}

void fbdev_set_offset(uint32_t xoffset, uint32_t yoffset)
{
    vinfo.xoffset = xoffset;
    vinfo.yoffset = yoffset;
}

#endif

#include "lvgl/lvgl.h"
#include "lv_drivers/display/fbdev.h"
#include "lv_drivers/indev/evdev.h"
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>

#define HOR_RES 1024
#define VER_RES 600

/* 整屏双缓冲（LVGL 侧）；配合 fbdev 的 yres_virtual×2 + FBIOPAN_DISPLAY */
#define DISP_BUF_PIXELS (HOR_RES * VER_RES)

/* 板上路径；宿主机素材在工程 GIF/ 目录，需 cp 到 NFS /root/（见文档 03 G2） */
#define GIF_PATH "A:/root/susan-lu4esm-wallpaper-1492_512.gif"

static lv_obj_t * s_perf_label;

static void perf_timer_cb(lv_timer_t * t)
{
    LV_UNUSED(t);
    fbdev_perf_t p;
    fbdev_get_perf(&p);

    char buf[128];
    snprintf(buf, sizeof(buf),
             "FPS:%lu  mode:%s  last:%lux%lu  flushes/f:%lu",
             (unsigned long)p.fps,
             fbdev_dblbuf_enabled() ? "dblbuf+pan" : "single",
             (unsigned long)p.flush_last_w,
             (unsigned long)p.flush_last_h,
             (unsigned long)p.flush_per_frame);

    if(s_perf_label) {
        lv_label_set_text(s_perf_label, buf);
    }
}

int main(void)
{
    lv_init();

    fbdev_init();

    /* 两块整屏 draw buffer：LVGL 渲染一块时另一块可被 flush */
    static lv_color_t buf_1[DISP_BUF_PIXELS];
    static lv_color_t buf_2[DISP_BUF_PIXELS];
    static lv_disp_draw_buf_t disp_buf;
    lv_disp_draw_buf_init(&disp_buf, buf_1, buf_2, DISP_BUF_PIXELS);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.draw_buf     = &disp_buf;
    disp_drv.flush_cb     = fbdev_flush;
    disp_drv.hor_res      = HOR_RES;
    disp_drv.ver_res      = VER_RES;
    disp_drv.full_refresh = 1; /* 每帧整屏绘完再 pan，后台页内容完整 */
    lv_disp_drv_register(&disp_drv);

    evdev_init();
    static lv_indev_drv_t indev_drv_1;
    lv_indev_drv_init(&indev_drv_1);
    indev_drv_1.type = LV_INDEV_TYPE_POINTER;
    indev_drv_1.read_cb = evdev_read;
    lv_indev_drv_register(&indev_drv_1);

    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_black(), 0);
    lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, 0);

    lv_obj_t * gif = lv_gif_create(lv_scr_act());
    lv_gif_set_src(gif, GIF_PATH);
    lv_obj_center(gif);

    s_perf_label = lv_label_create(lv_layer_top());
    lv_obj_set_style_text_color(s_perf_label, lv_color_hex(0x00FF88), 0);
    lv_obj_set_style_bg_color(s_perf_label, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_perf_label, LV_OPA_70, 0);
    lv_obj_set_style_pad_all(s_perf_label, 6, 0);
    lv_label_set_text(s_perf_label,
                      fbdev_dblbuf_enabled() ? "FPS:--  mode:dblbuf+pan" : "FPS:--  mode:single");
    lv_obj_align(s_perf_label, LV_ALIGN_TOP_LEFT, 8, 8);
    lv_timer_create(perf_timer_cb, 500, NULL);

    while(1) {
        lv_timer_handler();
        usleep(5000);
    }

    return 0;
}

uint32_t custom_tick_get(void)
{
    static uint64_t start_ms = 0;
    if(start_ms == 0) {
        struct timeval tv_start;
        gettimeofday(&tv_start, NULL);
        start_ms = (tv_start.tv_sec * 1000000 + tv_start.tv_usec) / 1000;
    }

    struct timeval tv_now;
    gettimeofday(&tv_now, NULL);
    uint64_t now_ms;
    now_ms = (tv_now.tv_sec * 1000000 + tv_now.tv_usec) / 1000;

    uint32_t time_ms = now_ms - start_ms;
    return time_ms;
}

/**
 * @file lv_demo_widgets.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_demo_widgets.h"
#include <stdio.h>

#if LV_USE_DEMO_WIDGETS

#if LV_MEM_CUSTOM == 0 && LV_MEM_SIZE < (38ul * 1024ul)
    #error Insufficient memory for lv_demo_widgets. Please set LV_MEM_SIZE to at least 38KB (38ul * 1024ul).  48KB is recommended.
#endif

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/
typedef enum {
    DISP_SMALL,
    DISP_MEDIUM,
    DISP_LARGE,
} disp_size_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/

/**********************
 *  STATIC VARIABLES
 **********************/
static disp_size_t disp_size;


static const lv_font_t * font_large;
static const lv_font_t * font_normal;

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
 
/**********************
 * STATIC VARIABLES
 **********************/
// 全局句柄，方便后续在其他任务中动态更新UI内容
static lv_obj_t * record_indicator; // 录像红点图标
static lv_obj_t * record_time_label; // 录像时间文本
static lv_obj_t * sd_info_label;	 // SD卡信息文本

/* DVP 预览层：全屏图像作为底层，仅由 LVGL 刷新，避免与 DVP flush 抢屏闪烁 */
static lv_obj_t * dvp_preview_img = NULL;
static uint8_t dvp_preview_placeholder_pixel[2] = { 0x00, 0x00 }; /* RGB565 黑 */
static lv_img_dsc_t dvp_preview_img_dsc = {
    .header = {
        .cf = LV_IMG_CF_TRUE_COLOR,
        .always_zero = 0,
        .reserved = 0,
        .w = 1,
        .h = 1,
    },
    .data_size = 2u,
    .data = dvp_preview_placeholder_pixel,
};

void ui_update_record_time(uint32_t seconds);
void ui_update_sd_capacity(uint32_t free_mb, uint32_t total_mb);

/**********************
 * ANIMATION CB
 **********************/
// 控制红点透明度实现呼吸闪烁效果的回调函数
static void blink_anim_cb(void * var, int32_t v)
{
	lv_obj_set_style_bg_opa((lv_obj_t *)var, v, 0);
}

void lv_demo_widgets(void)
{
    if(LV_HOR_RES <= 320) disp_size = DISP_SMALL;
    else if(LV_HOR_RES < 720) disp_size = DISP_MEDIUM;
    else disp_size = DISP_LARGE;

    font_large = LV_FONT_DEFAULT;
    font_normal = LV_FONT_DEFAULT;

    
    if(disp_size == DISP_LARGE) {
       
#if LV_FONT_MONTSERRAT_24
        font_large     = &lv_font_montserrat_24;
#else
        LV_LOG_WARN("LV_FONT_MONTSERRAT_24 is not enabled for the widgets demo. Using LV_FONT_DEFAULT instead.");
#endif
#if LV_FONT_MONTSERRAT_16
        font_normal    = &lv_font_montserrat_16;
#else
        LV_LOG_WARN("LV_FONT_MONTSERRAT_16 is not enabled for the widgets demo. Using LV_FONT_DEFAULT instead.");
#endif
    }
    else if(disp_size == DISP_MEDIUM) {
       
#if LV_FONT_MONTSERRAT_20
        font_large     = &lv_font_montserrat_20;
#else
        LV_LOG_WARN("LV_FONT_MONTSERRAT_20 is not enabled for the widgets demo. Using LV_FONT_DEFAULT instead.");
#endif
#if LV_FONT_MONTSERRAT_14
        font_normal    = &lv_font_montserrat_14;
#else
        LV_LOG_WARN("LV_FONT_MONTSERRAT_14 is not enabled for the widgets demo. Using LV_FONT_DEFAULT instead.");
#endif
    }
    else {   /* disp_size == DISP_SMALL */
       
#if LV_FONT_MONTSERRAT_18
        font_large     = &lv_font_montserrat_18;
#else
        LV_LOG_WARN("LV_FONT_MONTSERRAT_18 is not enabled for the widgets demo. Using LV_FONT_DEFAULT instead.");
#endif
#if LV_FONT_MONTSERRAT_12
        font_normal    = &lv_font_montserrat_12;
#else
        LV_LOG_WARN("LV_FONT_MONTSERRAT_12 is not enabled for the widgets demo. Using LV_FONT_DEFAULT instead.");
#endif
    }

#if LV_USE_THEME_DEFAULT
    lv_theme_default_init(NULL, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED), LV_THEME_DEFAULT_DARK,
                          font_normal);
#endif

	/*
    lv_style_init(&style_text_muted);
    lv_style_set_text_opa(&style_text_muted, LV_OPA_50);

    lv_style_init(&style_title);
    lv_style_set_text_font(&style_title, font_large);

    lv_style_init(&style_icon);
    lv_style_set_text_color(&style_icon, lv_theme_get_color_primary(NULL));
    lv_style_set_text_font(&style_icon, font_large);

    lv_style_init(&style_bullet);
    lv_style_set_border_width(&style_bullet, 0);
    lv_style_set_radius(&style_bullet, LV_RADIUS_CIRCLE);

    tv = lv_tabview_create(lv_scr_act(), LV_DIR_TOP, tab_h);

    lv_obj_set_style_text_font(lv_scr_act(), font_normal, 0);

    if(disp_size == DISP_LARGE) {
        lv_obj_t * tab_btns = lv_tabview_get_tab_btns(tv);
        lv_obj_set_style_pad_left(tab_btns, LV_HOR_RES / 2, 0);
        lv_obj_t * logo = lv_img_create(tab_btns);
        LV_IMG_DECLARE(img_lvgl_logo);
        lv_img_set_src(logo, &img_lvgl_logo);
        lv_obj_align(logo, LV_ALIGN_LEFT_MID, -LV_HOR_RES / 2 + 25, 0);

        lv_obj_t * label = lv_label_create(tab_btns);
        lv_obj_add_style(label, &style_title, 0);
        lv_label_set_text(label, "LVGL v8");
        lv_obj_align_to(label, logo, LV_ALIGN_OUT_RIGHT_TOP, 10, 0);

        label = lv_label_create(tab_btns);
        lv_label_set_text(label, "Widgets demo");
        lv_obj_add_style(label, &style_text_muted, 0);
        lv_obj_align_to(label, logo, LV_ALIGN_OUT_RIGHT_BOTTOM, 10, 0);
    }

    lv_obj_t * t1 = lv_tabview_add_tab(tv, "Profile");
    lv_obj_t * t2 = lv_tabview_add_tab(tv, "Analytics");
    lv_obj_t * t3 = lv_tabview_add_tab(tv, "Shop");
    profile_create(t1);
    analytics_create(t2);
    shop_create(t3);

    color_changer_create(tv);
    */
	
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, 0);

    /* DVP 预览图：全屏底层，DVP 开启前显示 1x1 黑色占位 */
    dvp_preview_img = lv_img_create(lv_scr_act());
    lv_img_set_src(dvp_preview_img, &dvp_preview_img_dsc);
    lv_obj_set_pos(dvp_preview_img, 0, 0);

    // 创建顶部半透明状态栏 (Top Bar)
    lv_obj_t * top_bar = lv_obj_create(lv_scr_act());
    lv_obj_set_size(top_bar, LV_PCT(100), 40); // 宽度100%，高度40
    lv_obj_align(top_bar, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_set_style_bg_color(top_bar, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_50, 0);
    lv_obj_set_style_border_width(top_bar, 0, 0);   // 去除边框
    lv_obj_set_style_radius(top_bar, 0, 0);         // 去除圆角
    lv_obj_clear_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE); // 禁用滚动

    // 3. 创建录像指示红点 (Record Indicator)
    record_indicator = lv_obj_create(top_bar);
    lv_obj_set_size(record_indicator, 16, 16);
    lv_obj_align(record_indicator, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_set_style_bg_color(record_indicator, lv_color_make(255, 0, 0), 0); // 红色
    lv_obj_set_style_radius(record_indicator, LV_RADIUS_CIRCLE, 0); // 设置为圆形
    lv_obj_set_style_border_width(record_indicator, 0, 0);

    // 为红点添加无限循环的闪烁动画
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, record_indicator);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_time(&a, 800); // 渐变时间 800ms
    lv_anim_set_playback_time(&a, 800);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&a, blink_anim_cb);
    lv_anim_start(&a);

    // 4. 创建录像时间文本 (Time Label)
    record_time_label = lv_label_create(top_bar);
    lv_label_set_text(record_time_label, "00:00:00");
    lv_obj_set_style_text_color(record_time_label, lv_color_white(), 0);
    // 对齐到红点的右边
    lv_obj_align_to(record_time_label, record_indicator, LV_ALIGN_OUT_RIGHT_MID, 10, 0);

    // 5. 创建 SD 卡容量信息 (SD Card Label)
    sd_info_label = lv_label_create(top_bar);
    // 使用 LVGL 内置图标 LV_SYMBOL_SAVE (软盘/保存) 代表存储卡
    lv_label_set_text(sd_info_label, LV_SYMBOL_SAVE " SD: 14.5G / 32.0G");
    lv_obj_set_style_text_color(sd_info_label, lv_color_white(), 0);
    lv_obj_align(sd_info_label, LV_ALIGN_RIGHT_MID, -10, 0);
	

}


/* ====================================================================
 * 下方为供应用层（如 video_recorder_cli.c）调用的更新接口
 * ==================================================================== */

// 更新录像时间 (参数传入秒数)
void ui_update_record_time(uint32_t seconds)
{
    if (record_time_label == NULL) return;

    uint32_t h = seconds / 3600;
    uint32_t m = (seconds % 3600) / 60;
    uint32_t s = seconds % 60;

    char buf[32];
    snprintf(buf, sizeof(buf), "%02u:%02u:%02u", h, m, s);
    
    // 如果 LVGL 跑在独立 Task 中，更新 UI 时记得加锁
    // bk_lvgl_lock();
    lv_label_set_text(record_time_label, buf);
    // bk_lvgl_unlock();
}

// 更新 SD 卡容量显示 (参数传入剩余和总容量的MB数)
void ui_update_sd_capacity(uint32_t free_mb, uint32_t total_mb)
{
    if (sd_info_label == NULL) return;

    float free_gb = (float)free_mb / 1024.0f;
    float total_gb = (float)total_mb / 1024.0f;

    char buf[64];
    snprintf(buf, sizeof(buf), LV_SYMBOL_SAVE " SD: %.1fG / %.1fG", free_gb, total_gb);

    // bk_lvgl_lock();
    lv_label_set_text(sd_info_label, buf);
    // bk_lvgl_unlock();
}

/* DVP 与 LVGL 共存：设置预览图使用的 RGB565 缓冲，仅 LVGL 刷新，避免整屏闪烁 */
void lv_dvp_preview_set_buffer(void *buf, uint32_t w, uint32_t h)
{
    if (dvp_preview_img == NULL) return;
    if (buf == NULL || w == 0 || h == 0) {
        dvp_preview_img_dsc.header.w = 1;
        dvp_preview_img_dsc.header.h = 1;
        dvp_preview_img_dsc.data_size = 2u;
        dvp_preview_img_dsc.data = dvp_preview_placeholder_pixel;
        lv_obj_set_pos(dvp_preview_img, 0, 0);
    } else {
        dvp_preview_img_dsc.header.w = (uint32_t)w;
        dvp_preview_img_dsc.header.h = (uint32_t)h;
        dvp_preview_img_dsc.data_size = w * h * 2u;
        dvp_preview_img_dsc.data = (const uint8_t *)buf;
        /* 若图比屏宽（如 864 vs 800），居中显示，LVGL 自动裁剪越界部分 */
        lv_coord_t x_off = -((lv_coord_t)w - LV_HOR_RES) / 2;
        lv_coord_t y_off = -((lv_coord_t)h - LV_VER_RES) / 2;
        lv_obj_set_pos(dvp_preview_img, (x_off < 0) ? x_off : 0,
                                         (y_off < 0) ? y_off : 0);
    }
    lv_img_set_src(dvp_preview_img, &dvp_preview_img_dsc);
}

void lv_dvp_preview_invalidate(void)
{
    if (dvp_preview_img != NULL)
        lv_obj_invalidate(dvp_preview_img);
}


#endif

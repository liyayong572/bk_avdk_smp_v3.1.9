#include <os/os.h>
#include <components/avdk_utils/avdk_error.h>
#include "cli.h"
#include <os/str.h>
#include "components/bk_dma2d.h"
#include "components/media_types.h"
#include <driver/gpio.h>
#include "gpio_driver.h"
#include "lcd_panel_devices.h"
#include "components/bk_display.h"
#include "dma2d_test.h"
#include "driver/pwr_clk.h"
#include "frame_buffer.h"
#include "blend_assets/blend.h"
#include "dma2d_yuv_blend.h"
#define TAG "dma2d_cli"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)

#define LOGV(...) BK_LOGV(TAG, ##__VA_ARGS__)

#define LCD_DEVICE_SWITCH 0

#define BL_PIN  GPIO_7
#define LCD_LDO_PIN GPIO_13
static bk_dma2d_ctlr_handle_t dma2d_handle1 = NULL;
static bk_dma2d_ctlr_handle_t dma2d_handle2 = NULL;
#if LCD_DEVICE_SWITCH
static const lcd_device_t *lcd_device =  &lcd_device_st7701s;
#endif 
bk_display_ctlr_handle_t lcd_display_handle = NULL;

static avdk_err_t dma2d_cli_display_frame_free_cb(void *frame)
{
    frame_buffer_display_free((frame_buffer_t *)frame);
    return AVDK_ERR_OK;
}

static bool dma2d_parse_yuv_fmt(const char *s, pixel_format_t *out_fmt, input_color_mode_t *out_dma2d_mode)
{
    if (s == NULL || out_fmt == NULL || out_dma2d_mode == NULL) {
        return false;
    }

    if (os_strcmp(s, "YUYV") == 0) {
        *out_fmt = PIXEL_FMT_YUYV;
        *out_dma2d_mode = DMA2D_INPUT_YUYV;
        return true;
    }
    if (os_strcmp(s, "UYVY") == 0) {
        *out_fmt = PIXEL_FMT_UYVY;
        *out_dma2d_mode = DMA2D_INPUT_UYVY;
        return true;
    }
    if (os_strcmp(s, "YYUV") == 0) {
        *out_fmt = PIXEL_FMT_YYUV;
        *out_dma2d_mode = DMA2D_INPUT_YYUV;
        return true;
    }
    if (os_strcmp(s, "UVYY") == 0) {
        *out_fmt = PIXEL_FMT_UVYY;
        *out_dma2d_mode = DMA2D_INPUT_UVYY;
        return true;
    }
    if (os_strcmp(s, "VUYY") == 0) {
        *out_fmt = PIXEL_FMT_VUYY;
        *out_dma2d_mode = DMA2D_INPUT_VUYY;
        return true;
    }
    return false;
}

static void dma2d_fill_yuv422(uint8_t *buf, uint32_t width, uint32_t height, pixel_format_t fmt, uint8_t y, uint8_t u, uint8_t v)
{
    uint32_t pixels = width * height;
    uint8_t *p = buf;

    for (uint32_t i = 0; i < pixels; i += 2) {
        switch (fmt) {
            case PIXEL_FMT_YUYV:
                *p++ = y;
                *p++ = u;
                *p++ = y;
                *p++ = v;
                break;
            case PIXEL_FMT_UYVY:
                *p++ = u;
                *p++ = y;
                *p++ = v;
                *p++ = y;
                break;
            case PIXEL_FMT_YYUV:
                *p++ = y;
                *p++ = y;
                *p++ = u;
                *p++ = v;
                break;
            case PIXEL_FMT_UVYY:
                *p++ = u;
                *p++ = v;
                *p++ = y;
                *p++ = y;
                break;
            case PIXEL_FMT_VUYY:
                *p++ = v;
                *p++ = u;
                *p++ = y;
                *p++ = y;
                break;
            default:
                *p++ = y;
                *p++ = u;
                *p++ = y;
                *p++ = v;
                break;
        }
    }
}

static const bk_blend_t *dma2d_pick_icon(const char *name)
{
    if (name == NULL) {
        return &img_battery1;
    }
    if (os_strcmp(name, "battery") == 0) {
        return &img_battery1;
    }
    if (os_strcmp(name, "wifi0") == 0) {
        return &img_wifi_rssi0;
    }
    if (os_strcmp(name, "wifi4") == 0) {
        return &img_wifi_rssi4;
    }
    if (os_strcmp(name, "cloud") == 0) {
        return &img_cloudy_to_sunny;
    }
	if (os_strcmp(name, "video_record") == 0) {
        return &img_video_record;
    }
	if (os_strcmp(name, "img_card") == 0) {
        return &img_card;
    }
    return &img_battery1;
}

static avdk_err_t dma2d_yuv422_to_rgb565(bk_dma2d_ctlr_handle_t handle,
                                        const frame_buffer_t *yuv_frame,
                                        frame_buffer_t *rgb565_frame,
                                        input_color_mode_t dma2d_yuv_mode,
                                        bool is_sync)
{
    dma2d_pfc_memcpy_config_t pfc_cfg = {0};
    pfc_cfg.pfc.mode = DMA2D_M2M_PFC;
    pfc_cfg.pfc.input_addr = yuv_frame->frame;
    pfc_cfg.pfc.src_frame_width = yuv_frame->width;
    pfc_cfg.pfc.src_frame_height = yuv_frame->height;
    pfc_cfg.pfc.src_frame_xpos = 0;
    pfc_cfg.pfc.src_frame_ypos = 0;
    pfc_cfg.pfc.input_color_mode = dma2d_yuv_mode;
    pfc_cfg.pfc.src_pixel_byte = TWO_BYTES;
    pfc_cfg.pfc.input_data_reverse = NO_REVERSE;
    pfc_cfg.pfc.input_red_blue_swap = DMA2D_RB_REGULAR;
    pfc_cfg.pfc.input_alpha = 0xFF;
    pfc_cfg.pfc.output_alpha = 0xFF;

    pfc_cfg.pfc.output_addr = rgb565_frame->frame;
    pfc_cfg.pfc.dst_frame_width = rgb565_frame->width;
    pfc_cfg.pfc.dst_frame_height = rgb565_frame->height;
    pfc_cfg.pfc.dst_frame_xpos = 0;
    pfc_cfg.pfc.dst_frame_ypos = 0;
    pfc_cfg.pfc.output_color_mode = DMA2D_OUTPUT_RGB565;
    pfc_cfg.pfc.dst_pixel_byte = TWO_BYTES;
    pfc_cfg.pfc.output_red_blue_swap = DMA2D_RB_REGULAR;
    pfc_cfg.pfc.out_byte_by_byte_reverse = NO_REVERSE;
    pfc_cfg.pfc.dma2d_width = rgb565_frame->width;
    pfc_cfg.pfc.dma2d_height = rgb565_frame->height;
    pfc_cfg.is_sync = is_sync;

    return bk_dma2d_pixel_conversion(handle, &pfc_cfg);
}

static avdk_err_t dma2d_blend_icon_argb8888_on_rgb565(bk_dma2d_ctlr_handle_t handle,
                                                      frame_buffer_t *rgb565_frame,
                                                      const bk_blend_t *icon,
                                                      bool is_sync)
{
    dma2d_blend_config_t blend_cfg = {0};
    blend_cfg.blend.pfg_addr = (char *)icon->image.data;
    blend_cfg.blend.pbg_addr = (char *)rgb565_frame->frame;
    blend_cfg.blend.pdst_addr = (char *)rgb565_frame->frame;
    blend_cfg.blend.fg_color_mode = DMA2D_INPUT_ARGB8888;
    blend_cfg.blend.bg_color_mode = DMA2D_INPUT_RGB565;
    blend_cfg.blend.dst_color_mode = DMA2D_OUTPUT_RGB565;
    blend_cfg.blend.fg_red_blue_swap = DMA2D_RB_SWAP;
    blend_cfg.blend.bg_red_blue_swap = DMA2D_RB_REGULAR;
    blend_cfg.blend.dst_red_blue_swap = DMA2D_RB_REGULAR;

    blend_cfg.blend.fg_frame_width = icon->width;
    blend_cfg.blend.fg_frame_height = icon->height;
    blend_cfg.blend.bg_frame_width = rgb565_frame->width;
    blend_cfg.blend.bg_frame_height = rgb565_frame->height;
    blend_cfg.blend.dst_frame_width = rgb565_frame->width;
    blend_cfg.blend.dst_frame_height = rgb565_frame->height;

    blend_cfg.blend.fg_frame_xpos = 0;
    blend_cfg.blend.fg_frame_ypos = 0;
    blend_cfg.blend.bg_frame_xpos = icon->xpos;
    blend_cfg.blend.bg_frame_ypos = icon->ypos;
    blend_cfg.blend.dst_frame_xpos = icon->xpos;
    blend_cfg.blend.dst_frame_ypos = icon->ypos;

    blend_cfg.blend.fg_pixel_byte = FOUR_BYTES;
    blend_cfg.blend.bg_pixel_byte = TWO_BYTES;
    blend_cfg.blend.dst_pixel_byte = TWO_BYTES;

    blend_cfg.blend.dma2d_width = icon->width;
    blend_cfg.blend.dma2d_height = icon->height;

    blend_cfg.blend.fg_alpha_mode = DMA2D_NO_MODIF_ALPHA;
    blend_cfg.blend.bg_alpha_mode = DMA2D_NO_MODIF_ALPHA;
    blend_cfg.blend.out_byte_by_byte_reverse = NO_REVERSE;
    blend_cfg.blend.input_data_reverse = NO_REVERSE;
    blend_cfg.is_sync = is_sync;

    return bk_dma2d_blend(handle, &blend_cfg);
}

static avdk_err_t dma2d_rgb565_byte_swap_copy(bk_dma2d_ctlr_handle_t handle,
                                              const frame_buffer_t *src_rgb565,
                                              frame_buffer_t *dst_rgb565,
                                              bool is_sync)
{
    dma2d_memcpy_config_t memcpy_cfg = {0};
    memcpy_cfg.memcpy.mode = DMA2D_M2M;
    memcpy_cfg.memcpy.input_addr = src_rgb565->frame;
    memcpy_cfg.memcpy.src_frame_width = src_rgb565->width;
    memcpy_cfg.memcpy.src_frame_height = src_rgb565->height;
    memcpy_cfg.memcpy.src_frame_xpos = 0;
    memcpy_cfg.memcpy.src_frame_ypos = 0;
    memcpy_cfg.memcpy.input_color_mode = DMA2D_INPUT_RGB565;
    memcpy_cfg.memcpy.src_pixel_byte = TWO_BYTES;
    memcpy_cfg.memcpy.input_data_reverse = NO_REVERSE;
    memcpy_cfg.memcpy.input_red_blue_swap = DMA2D_RB_REGULAR;
    memcpy_cfg.memcpy.input_alpha = 0xFF;
    memcpy_cfg.memcpy.output_alpha = 0xFF;

    memcpy_cfg.memcpy.output_addr = dst_rgb565->frame;
    memcpy_cfg.memcpy.dst_frame_width = dst_rgb565->width;
    memcpy_cfg.memcpy.dst_frame_height = dst_rgb565->height;
    memcpy_cfg.memcpy.dst_frame_xpos = 0;
    memcpy_cfg.memcpy.dst_frame_ypos = 0;
    memcpy_cfg.memcpy.output_color_mode = DMA2D_OUTPUT_RGB565;
    memcpy_cfg.memcpy.dst_pixel_byte = TWO_BYTES;
    memcpy_cfg.memcpy.output_red_blue_swap = DMA2D_RB_REGULAR;
    memcpy_cfg.memcpy.out_byte_by_byte_reverse = BYTE_BY_BYTE_REVERSE;
    memcpy_cfg.memcpy.dma2d_width = dst_rgb565->width;
    memcpy_cfg.memcpy.dma2d_height = dst_rgb565->height;
    memcpy_cfg.is_sync = is_sync;

    return bk_dma2d_memcpy(handle, &memcpy_cfg);
}


static void cli_dma2d_cmd(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
    avdk_err_t ret = AVDK_ERR_GENERIC;
    char *msg = NULL;
    frame_buffer_t *yuv_frame = NULL;
    frame_buffer_t *rgb565_frame = NULL;
    frame_buffer_t *rgb565_bg = NULL;
    // 修复open命令中的句柄赋值问题
    if (os_strcmp(argv[1], "open") == 0) {
        if (os_strcmp(argv[2], "module1") == 0) 
        {
            ret = bk_dma2d_new(&dma2d_handle1);
            AVDK_RETURN_VOID_ON_ERROR(ret, TAG, "bk_dma2d_new failed! ");
            LOGD("bk_dma2d_new module1 success! %p\n", dma2d_handle1);
            ret = bk_dma2d_open(dma2d_handle1);
            AVDK_RETURN_VOID_ON_ERROR(ret, TAG, "bk_dma2d_open failed!\n");
            LOGD("bk_dma2d_open module1 success! %p\n", dma2d_handle1);
        }
        else if (os_strcmp(argv[2], "module2") == 0)
        {
            ret = bk_dma2d_new(&dma2d_handle2);
            AVDK_RETURN_VOID_ON_ERROR(ret, TAG, "bk_dma2d_new failed! ");
            LOGD("bk_dma2d_new module2 success! %p\n", dma2d_handle2);
            ret = bk_dma2d_open(dma2d_handle2);
            AVDK_RETURN_VOID_ON_ERROR(ret, TAG, "bk_dma2d_open failed!\n");
            LOGD("bk_dma2d_open module2 success! %p\n", dma2d_handle2);
        }
        if (lcd_display_handle == NULL)
        {
        	#if LCD_DEVICE_SWITCH
            bk_display_rgb_ctlr_config_t lcd_display_config = {0};
            lcd_display_config.lcd_device = lcd_device;
            lcd_display_config.clk_pin = GPIO_0;
            lcd_display_config.cs_pin = GPIO_12;
            lcd_display_config.sda_pin = GPIO_1;
            lcd_display_config.rst_pin = GPIO_6;
            ret = bk_display_rgb_new(&lcd_display_handle, &lcd_display_config);
			#else
			bk_display_spi_ctlr_config_t spi_ctlr_config = {
			    .lcd_device = &lcd_device_st7735S,
			    .spi_id = 0,
			    .dc_pin = GPIO_17,
			    .reset_pin = GPIO_18,
			    .te_pin = 0,
			};
            // Create LCD display controller
			ret = bk_display_spi_new(&lcd_display_handle, &spi_ctlr_config);
			#endif 
			
            AVDK_GOTO_VOID_ON_FALSE(ret == AVDK_ERR_OK, exit, TAG, "bk_display_rgb_new failed!\n");
            LOGD("bk_display_rgb_new success!\n");


            gpio_dev_unmap(BL_PIN);
            BK_LOG_ON_ERR(bk_gpio_enable_output(BL_PIN));
            bk_gpio_set_output_high(BL_PIN);

            bk_pm_module_vote_ctrl_external_ldo(GPIO_CTRL_LDO_MODULE_LCD, LCD_LDO_PIN, GPIO_OUTPUT_STATE_HIGH);
            ret = bk_display_open(lcd_display_handle);
            AVDK_GOTO_VOID_ON_FALSE(ret == AVDK_ERR_OK, exit, TAG, "bk_display_open failed!\n");
        }
    }

    else if (os_strcmp(argv[1], "close") == 0) {
        if (os_strcmp(argv[2], "module1") == 0 && dma2d_handle1 != NULL)
        {
            ret = bk_dma2d_close(dma2d_handle1);
            AVDK_RETURN_VOID_ON_ERROR(ret, TAG, "bk_dma2d_close failed!\n");
            LOGD("bk_dma2d_close module1 success!\n");
    
            ret = bk_dma2d_delete(dma2d_handle1);
            AVDK_RETURN_VOID_ON_ERROR(ret, TAG, "bk_dma2d_delete failed!\n");
            dma2d_handle1 = NULL;
            LOGD("bk_dma2d_delete module1 success!\n");
        } 
        else if (os_strcmp(argv[2], "module2") == 0 && dma2d_handle2 != NULL)
        {
            ret = bk_dma2d_close(dma2d_handle2);
            AVDK_RETURN_VOID_ON_ERROR(ret, TAG, "bk_dma2d_close failed!\n");
            LOGD("bk_dma2d_close module2 success!\n");
    
            ret = bk_dma2d_delete(dma2d_handle2);
            AVDK_RETURN_VOID_ON_ERROR(ret, TAG, "bk_dma2d_delete failed!\n");
            dma2d_handle2 = NULL;
            LOGD("bk_dma2d_delete module2 success!\n");
        }
        if (lcd_display_handle != NULL)
        {
            if (dma2d_handle2 == NULL && dma2d_handle1 == NULL)
            {
                ret = bk_display_close(lcd_display_handle);
                AVDK_GOTO_VOID_ON_FALSE(ret == AVDK_ERR_OK, exit, TAG, "bk_display_close failed!\n");
                ret = bk_display_delete(lcd_display_handle);
                AVDK_GOTO_VOID_ON_FALSE(ret == AVDK_ERR_OK, exit, TAG, "bk_display_delete failed!\n");
                lcd_display_handle = NULL;
                bk_pm_module_vote_ctrl_external_ldo(GPIO_CTRL_LDO_MODULE_LCD, LCD_LDO_PIN, GPIO_OUTPUT_STATE_LOW);
                bk_gpio_set_output_low(BL_PIN);
            }
        }
    }
    
    else if (os_strcmp(argv[1], "fill") == 0) {
        if (argc < 10) {
            LOGE("Usage: dma2d fill <format> <color> <frame_width> <frame_height> <xpos> <ypos> <fill_width> <fill_height>\n");
            return;
        }
        if (dma2d_handle1 != NULL)
        {
            ret = dma2d_fill_test(dma2d_handle1, argv[2], os_strtoul(argv[3], NULL, 16),
                                    os_strtoul(argv[4], NULL, 10), os_strtoul(argv[5], NULL, 10),
                                    os_strtoul(argv[6], NULL, 10), os_strtoul(argv[7], NULL, 10),
                                    os_strtoul(argv[8], NULL, 10), os_strtoul(argv[9], NULL, 10));
            
            if (ret != AVDK_ERR_OK) {
                LOGE("dma2d_fill_test module1 failed!\n");
            }
        }
    }
    else if (os_strcmp(argv[1], "fill_module2") == 0) {
        if (argc < 10) {
            LOGE("Usage: dma2d fill <format> <color> <frame_width> <frame_height> <xpos> <ypos> <fill_width> <fill_height>\n");
            return;
        }
        if (dma2d_handle2 != NULL)
        {
            ret = dma2d_fill_test(dma2d_handle2, argv[2], os_strtoul(argv[3], NULL, 16),
                            os_strtoul(argv[4], NULL, 10), os_strtoul(argv[5], NULL, 10),
                            os_strtoul(argv[6], NULL, 10), os_strtoul(argv[7], NULL, 10),
                            os_strtoul(argv[8], NULL, 10), os_strtoul(argv[9], NULL, 10));  
        
            if (ret != AVDK_ERR_OK) {
                LOGE("dma2d_fill_test failed!\n");
            }
        }
    }
    //dma2d memcpy RGB565,0xf800 32,48,32,48,0,0,0,0,32,48
    else if (os_strcmp(argv[1], "memcpy") == 0) {
        if (argc < 14) {
            LOGE("Usage: dma2d memcpy <format> <color> <src_width> <src_height> <dst_width> <dst_height> <src_x> <src_y> <dst_x> <dst_y> <width> <height>\n");
            return;
        }
        if (dma2d_handle1 != NULL)
        {
            ret = dma2d_memcpy_test(dma2d_handle1, argv[2], os_strtoul(argv[3], NULL, 16),
                                os_strtoul(argv[4], NULL, 10), os_strtoul(argv[5], NULL, 10),
                                os_strtoul(argv[6], NULL, 10), os_strtoul(argv[7], NULL, 10),
                                os_strtoul(argv[8], NULL, 10), os_strtoul(argv[9], NULL, 10),
                                os_strtoul(argv[10], NULL, 10), os_strtoul(argv[11], NULL, 10),
                                os_strtoul(argv[12], NULL, 10), os_strtoul(argv[13], NULL, 10));
            if (ret != AVDK_ERR_OK) {
                LOGE("dma2d_memcpy_test failed!\n");
            }
        }
    }
    else if (os_strcmp(argv[1], "memcpy_module2") == 0) {
        if (argc < 14) {
            LOGE("Usage: dma2d memcpy <format> <color> <src_width> <src_height> <dst_width> <dst_height> <src_x> <src_y> <dst_x> <dst_y> <width> <height>\n");
            return;
        }   
        if  (dma2d_handle2 != NULL)
        {
            ret = dma2d_memcpy_test(dma2d_handle2, argv[2], os_strtoul(argv[3], NULL, 16),
                            os_strtoul(argv[4], NULL, 10), os_strtoul(argv[5], NULL, 10),
                            os_strtoul(argv[6], NULL, 10), os_strtoul(argv[7], NULL, 10),
                            os_strtoul(argv[8], NULL, 10), os_strtoul(argv[9], NULL, 10),
                            os_strtoul(argv[10], NULL, 10), os_strtoul(argv[11], NULL, 10),
                            os_strtoul(argv[12], NULL, 10), os_strtoul(argv[13], NULL, 10));
            if (ret != AVDK_ERR_OK) {
                LOGE("dma2d_memcpy_test failed!\n");
            }
        }
    }
    //dma2d pfc RGB565,RGB888,0xf800 32,48,32,48,0,0,0,0,32,48
    else if (os_strcmp(argv[1], "pfc") == 0) {
        if (argc < 15) {
            LOGE("Usage: dma2d pfc <input_format> <output_format> <color> <src_width> <src_height> <dst_width> <dst_height> <src_x> <src_y> <dst_x> <dst_y> <width> <height>\n");
            return;
        }
        if (dma2d_handle1 != NULL) {
            ret = dma2d_pfc_test(dma2d_handle1, argv[2], argv[3], os_strtoul(argv[4], NULL, 16),
                                os_strtoul(argv[5], NULL, 10), os_strtoul(argv[6], NULL, 10),
                                os_strtoul(argv[7], NULL, 10), os_strtoul(argv[8], NULL, 10),
                                os_strtoul(argv[9], NULL, 10), os_strtoul(argv[10], NULL, 10),
                                os_strtoul(argv[11], NULL, 10), os_strtoul(argv[12], NULL, 10),
                                os_strtoul(argv[13], NULL, 10), os_strtoul(argv[14], NULL, 10));
            
            if (ret != AVDK_ERR_OK) {
                LOGE("dma2d_pfc_test failed!\n");
            }
        }
    }
    else if (os_strcmp(argv[1], "pfc_module2") == 0) {
        if (argc < 15) {
            LOGE("Usage: dma2d pfc <input_format> <output_format> <color> <src_width> <src_height> <dst_width> <dst_height> <src_x> <src_y> <dst_x> <dst_y> <width> <height>\n");
            return;
        }
        if (dma2d_handle2 != NULL) {
            ret = dma2d_pfc_test(dma2d_handle2, argv[2], argv[3], os_strtoul(argv[4], NULL, 16),
                        os_strtoul(argv[5], NULL, 10), os_strtoul(argv[6], NULL, 10),
                        os_strtoul(argv[7], NULL, 10), os_strtoul(argv[8], NULL, 10),
                        os_strtoul(argv[9], NULL, 10), os_strtoul(argv[10], NULL, 10),
                        os_strtoul(argv[11], NULL, 10), os_strtoul(argv[12], NULL, 10),
                        os_strtoul(argv[13], NULL, 10), os_strtoul(argv[14], NULL, 10));
            
            if (ret != AVDK_ERR_OK) {
                LOGE("dma2d_pfc_test failed!\n");
            }
        }
    }
    //dma2d blend RGB565,RGB888,RGB888,0xf800,0x07e0,32,48,32,48,32,48,0,0,0,0,0,0,32,48,FF
    else if (os_strcmp(argv[1], "blend") == 0) {
        if (argc < 4) {
            LOGE("Usage: dma2d blend <fg_format> <bg_format> <output_format> <fg_color> <bg_color> <bg_width> <bg_height> <fg_width> <fg_height> <dst_width> <dst_height> <bg_x> <bg_y> <fg_x> <fg_y> <dst_x> <dst_y> <width> <height> <alpha>\n");
            return;
        }
        if (dma2d_handle1 != NULL)
        {
            ret = dma2d_blend_test(dma2d_handle1, argv[2],os_strtoul(argv[3], NULL, 16),
                                os_strtoul(argv[4], NULL, 10), os_strtoul(argv[5], NULL, 10), os_strtoul(argv[6], NULL, 10));
            
            if (ret != AVDK_ERR_OK) {
                LOGE("dma2d_blend_test failed!\n");
            }
        }
    }
    else if (os_strcmp(argv[1], "blend_module2") == 0) {
        if (argc < 4) {
            LOGE("Usage: dma2d blend <fg_format> <bg_format> <output_format> <fg_color> <bg_color> <bg_width> <bg_height> <fg_width> <fg_height> <dst_width> <dst_height> <bg_x> <bg_y> <fg_x> <fg_y> <dst_x> <dst_y> <width> <height> <alpha>\n");
            return;
        }
        if (dma2d_handle2 != NULL) {
            ret = dma2d_blend_test(dma2d_handle2, argv[2],os_strtoul(argv[3], NULL, 16),
                                os_strtoul(argv[4], NULL, 10), os_strtoul(argv[5], NULL, 10), os_strtoul(argv[6], NULL, 10));
            
            if (ret != AVDK_ERR_OK) {
                LOGE("dma2d_blend_test failed!\n");
            }
        }
    }
    else if (os_strcmp(argv[1], "yuv_blend") == 0) {
        if (argc < 9) {
            LOGE("Usage: dma2d yuv_blend <YUYV|UYVY|YYUV|UVYY|VUYY> <width> <height> <Y> <U> <V> <swap:0|1> [icon1] [icon2] [sync:0|1]\n");
            return;
        }
        if (dma2d_handle1 == NULL || lcd_display_handle == NULL) {
            LOGE("Please run: dma2d open module1\n");
            //return;

			ret = bk_dma2d_new(&dma2d_handle1);
            AVDK_RETURN_VOID_ON_ERROR(ret, TAG, "bk_dma2d_new failed! ");
            LOGD("bk_dma2d_new module1 success! %p\n", dma2d_handle1);
            ret = bk_dma2d_open(dma2d_handle1);
            AVDK_RETURN_VOID_ON_ERROR(ret, TAG, "bk_dma2d_open failed!\n");
            LOGD("bk_dma2d_open module1 success! %p\n", dma2d_handle1);

			if (lcd_display_handle == NULL)
	        {
	        	#if LCD_DEVICE_SWITCH
	            bk_display_rgb_ctlr_config_t lcd_display_config = {0};
	            lcd_display_config.lcd_device = lcd_device;
	            lcd_display_config.clk_pin = GPIO_0;
	            lcd_display_config.cs_pin = GPIO_12;
	            lcd_display_config.sda_pin = GPIO_1;
	            lcd_display_config.rst_pin = GPIO_6;
	            ret = bk_display_rgb_new(&lcd_display_handle, &lcd_display_config);
				#else
				bk_display_spi_ctlr_config_t spi_ctlr_config = {
				    .lcd_device = &lcd_device_st7735S,
				    .spi_id = 0,
				    .dc_pin = GPIO_17,
				    .reset_pin = GPIO_18,
				    .te_pin = 0,
				};
	            // Create LCD display controller
				ret = bk_display_spi_new(&lcd_display_handle, &spi_ctlr_config);
				#endif 
				
	            AVDK_GOTO_VOID_ON_FALSE(ret == AVDK_ERR_OK, exit, TAG, "bk_display_rgb_new failed!\n");
	            LOGD("bk_display_rgb_new success!\n");


	            gpio_dev_unmap(BL_PIN);
	            BK_LOG_ON_ERR(bk_gpio_enable_output(BL_PIN));
	            bk_gpio_set_output_high(BL_PIN);

	            bk_pm_module_vote_ctrl_external_ldo(GPIO_CTRL_LDO_MODULE_LCD, LCD_LDO_PIN, GPIO_OUTPUT_STATE_HIGH);
	            ret = bk_display_open(lcd_display_handle);
	            AVDK_GOTO_VOID_ON_FALSE(ret == AVDK_ERR_OK, exit, TAG, "bk_display_open failed!\n");
	        }
			
        }

        pixel_format_t yuv_fmt = PIXEL_FMT_YUYV;
        input_color_mode_t dma2d_yuv_mode = DMA2D_INPUT_YUYV;
        if (!dma2d_parse_yuv_fmt(argv[2], &yuv_fmt, &dma2d_yuv_mode)) {
            LOGE("Unsupported yuv fmt: %s\n", argv[2]);
            return;
        }

        uint16_t width = os_strtoul(argv[3], NULL, 10);
        uint16_t height = os_strtoul(argv[4], NULL, 10);
        if (width == 0 || height == 0 || (width & 0x1)) {
            LOGE("Invalid width/height (YUV422 requires even width)\n");
            return;
        }

        uint8_t y = (uint8_t)os_strtoul(argv[5], NULL, 16);
        uint8_t u = (uint8_t)os_strtoul(argv[6], NULL, 16);
        uint8_t v = (uint8_t)os_strtoul(argv[7], NULL, 16);

        uint32_t swap = os_strtoul(argv[8], NULL, 10);
        const char *icon_name = (argc >= 10) ? argv[9] : "battery";
        const char *icon2_name = NULL;
        bool is_sync = true;
        if (argc >= 12) {
            icon2_name = argv[10];
            is_sync = os_strtoul(argv[11], NULL, 10) ? true : false;
        } else if (argc >= 11) {
            is_sync = os_strtoul(argv[10], NULL, 10) ? true : false;
        }

        yuv_frame = frame_buffer_display_malloc(width * height * 2);
        AVDK_GOTO_VOID_ON_FALSE(yuv_frame != NULL, exit, TAG, "malloc yuv frame failed\n");
        yuv_frame->width = width;
        yuv_frame->height = height;
        yuv_frame->fmt = yuv_fmt;
        yuv_frame->length = width * height * 2;

        dma2d_fill_yuv422(yuv_frame->frame, width, height, yuv_fmt, y, u, v);

        rgb565_bg = frame_buffer_display_malloc(width * height * 2);
        AVDK_GOTO_VOID_ON_FALSE(rgb565_bg != NULL, exit, TAG, "malloc rgb565 bg failed\n");
        rgb565_bg->width = width;
        rgb565_bg->height = height;
        rgb565_bg->fmt = PIXEL_FMT_RGB565;
        rgb565_bg->length = width * height * 2;

        ret = dma2d_yuv422_to_rgb565(dma2d_handle1, yuv_frame, rgb565_bg, dma2d_yuv_mode, is_sync);
        AVDK_GOTO_VOID_ON_FALSE(ret == AVDK_ERR_OK, exit, TAG, "dma2d pfc yuv->rgb565 failed\n");

        const bk_blend_t *icon = dma2d_pick_icon(icon_name);
        ret = dma2d_blend_icon_argb8888_on_rgb565(dma2d_handle1, rgb565_bg, icon, is_sync);
        AVDK_GOTO_VOID_ON_FALSE(ret == AVDK_ERR_OK, exit, TAG, "dma2d blend icon failed\n");

        if (icon2_name) {
            const bk_blend_t *icon2 = dma2d_pick_icon(icon2_name);
            ret = dma2d_blend_icon_argb8888_on_rgb565(dma2d_handle1, rgb565_bg, icon2, is_sync);
            AVDK_GOTO_VOID_ON_FALSE(ret == AVDK_ERR_OK, exit, TAG, "dma2d blend icon2 failed\n");
        }


        if (swap) {
            rgb565_frame = frame_buffer_display_malloc(width * height * 2);
            AVDK_GOTO_VOID_ON_FALSE(rgb565_frame != NULL, exit, TAG, "malloc rgb565 out failed\n");
            rgb565_frame->width = width;
            rgb565_frame->height = height;
            rgb565_frame->fmt = PIXEL_FMT_RGB565;
            rgb565_frame->length = width * height * 2;

            ret = dma2d_rgb565_byte_swap_copy(dma2d_handle1, rgb565_bg, rgb565_frame, is_sync);
            AVDK_GOTO_VOID_ON_FALSE(ret == AVDK_ERR_OK, exit, TAG, "dma2d memcpy swap failed\n");

            frame_buffer_display_free(rgb565_bg);
            rgb565_bg = NULL;
        } else {
            rgb565_frame = rgb565_bg;
        }

        ret = bk_display_flush(lcd_display_handle, rgb565_frame, dma2d_cli_display_frame_free_cb);
        AVDK_GOTO_VOID_ON_FALSE(ret == AVDK_ERR_OK, exit, TAG, "bk_display_flush failed\n");

        frame_buffer_display_free(yuv_frame);
        ret = AVDK_ERR_OK;
        goto exit;
    }
    else {
        LOGE("%s, %d, not found this cmd!\n", __func__, __LINE__);
    }

   exit: 

    if (ret != AVDK_ERR_OK) {
        if (yuv_frame) {
            frame_buffer_display_free(yuv_frame);
        }
        if (rgb565_frame && rgb565_frame != rgb565_bg) {
            frame_buffer_display_free(rgb565_frame);
        }
        if (rgb565_bg) {
            frame_buffer_display_free(rgb565_bg);
        }
    }

    if (ret != AVDK_ERR_OK) {
        msg = CLI_CMD_RSP_ERROR;
    } else {
        msg = CLI_CMD_RSP_SUCCEED;
    }

    os_memcpy(pcWriteBuffer, msg, os_strlen(msg));
}

#define CMDS_COUNT  (sizeof(s_dma2d_commands) / sizeof(struct cli_command))

static const struct cli_command s_dma2d_commands[] = {
    {"dma2d", "open/close/fill/blend/memcpy/pfc ", cli_dma2d_cmd},
};

int cli_dma2d_init(void)
{
    return cli_register_commands(s_dma2d_commands, CMDS_COUNT);
}

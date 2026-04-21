#include <common/bk_include.h>
#include <components/bk_display.h>
#include <os/os.h>
#include <driver/pwr_clk.h>
#include "lcd_panel_devices.h"

#include "sport_dv_hw.h"
#include "sport_dv_common.h"
#include "sport_dv_display.h"

#include "components/bk_dma2d.h"
#include "components/bk_dma2d_types.h"
#include "driver/dma2d.h"

#include "driver/hw_scale.h"
#include <driver/hw_scale_types.h>
#include <driver/psram.h>

#define TAG "sport_dv_disp"
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

#define LCD_DEVICE_DISP_SWITCH 0

static bk_display_ctlr_handle_t s_disp_lcd = NULL;
static beken_queue_t s_disp_queue = NULL;
static beken_thread_t s_disp_thread = NULL;
static bool s_disp_thread_running = false;
static uint32_t s_disp_refcnt = 0;


#define DVP_SCALE_PREVIEW_W  	160
#define DVP_SCALE_PREVIEW_H  	128

static beken_semaphore_t g_scale0_sem = NULL;
frame_buffer_t *small_frame = NULL;
//static int key_capture = 0;

// ==========================================================
// [模块 A]：HW Scale 硬件拉伸引擎
// ==========================================================
static void app_scale0_callback(void *param)
{
    if (g_scale0_sem != NULL) {
        rtos_set_semaphore(&g_scale0_sem); // 释放信号量，唤醒主线程
    }

	/*
	bk_err_t ret = rtos_push_to_queue(&video_display_frame_queue, &small_frame, 0);
	
    if (ret != BK_OK) {
        frame_buffer_display_free(small_frame); // 队列满则释放
    }
    */
    
    /*
	avdk_err_t ret = bk_display_flush(video_recorder_lcd_handle, small_frame, display_frame_free_cb);
	if (ret != AVDK_ERR_OK)
	{
		LOGE("%s: bk_display_flush failed, ret:%d\n", __func__, ret);
		// Free frame on error
		frame_buffer_display_free(small_frame);
	}
	*/
	
}

bk_err_t video_hw_scale_frame(uint8_t *src_addr, uint16_t src_w, uint16_t src_h,
                              uint8_t *dst_addr, uint16_t dst_w, uint16_t dst_h)
{
    if (g_scale0_sem == NULL) {
        rtos_init_semaphore_ex(&g_scale0_sem, 1, 0);
        bk_hw_scale_driver_init(0); // 初始化 SCALE0
        bk_hw_scale_isr_register(0, app_scale0_callback, NULL);
    }

    scale_drv_config_t scale_config = {0};
    scale_config.src_addr = src_addr;
    scale_config.src_width = src_w;
    scale_config.src_height = src_h;
    scale_config.pixel_fmt = PIXEL_FMT_YUYV; 
    scale_config.dst_addr = dst_addr;
    scale_config.dst_width = dst_w;
    scale_config.dst_height = dst_h;
    scale_config.scale_mode = FRAME_SCALE;
	/*
	bk_psram_enable_write_through(0, (uint32_t)dst_addr,
		(uint32_t)(dst_addr + dst_w*dst_h*2));
	*/
		
    if (hw_scale_frame(0, &scale_config) != BK_OK) return BK_FAIL;

    // 挂起 CPU，等待硬件敲门 (超时 1000ms)
    rtos_get_semaphore(&g_scale0_sem, 1000); 
    return BK_OK;
}

// ==========================================================
// 专门用于 LCD 刷屏的异步线程与队列资源
// ==========================================================

void video_sw_scale_yuyv_nearest(uint8_t* src_img, int src_w, int src_h, 
							   uint8_t* dst_img, int dst_w, int dst_h) 
{
  // 1. 计算缩放比例 (使用 16 位定点数代替 float 浮点数，极大提升 CPU 速度！)
  // 原理：(src / dst) * 65536
  int x_ratio = (int)((src_w << 16) / dst_w) + 1;
  int y_ratio = (int)((src_h << 16) / dst_h) + 1;
  
  int src_x, src_y;
  
  // 2. 按行遍历目标图像
  for (int i = 0; i < dst_h; i++) {
	  // 计算当前目标行，对应源图像的哪一行
	  src_y = (i * y_ratio) >> 16;
	  
	  // 算出源和目标每一行的起始物理地址
	  // YUYV 格式下，1个像素占 2 字节，所以宽度要 * 2
	  uint8_t* src_row = src_img + (src_y * src_w * 2);
	  uint8_t* dst_row = dst_img + (i * dst_w * 2);
	  
	  // 3. 按列遍历目标图像 (极其关键：每次步进 2 个像素！)
	  // 为了保证 [Y0 U0 Y1 V1] 宏像素不被破坏，我们每次处理 4 个字节
	  for (int j = 0; j < dst_w; j += 2) {
		  
		  // 计算当前目标列，对应源图像的哪一列
		  src_x = (j * x_ratio) >> 16;
		  
		  // 【安全锁】强制将源 X 坐标对齐到偶数边界 (宏像素边界)
		  // 例如：算出是 3，强行变成 2；算出是 5，强行变成 4。
		  src_x &= ~1; 
		  
		  // 4. 指针转换：直接以 32位 (4字节) 为单位进行搬运，榨干 CPU 总线位宽
		  uint32_t* src_macro_pixel = (uint32_t*)(src_row + (src_x * 2));
		  uint32_t* dst_macro_pixel = (uint32_t*)(dst_row + (j * 2));
		  
		  // 完美复制 2 个像素的亮度与共享色度
		  *dst_macro_pixel = *src_macro_pixel;
	  }
  }
}

/**
* @brief 究极优化版：纯软件 YUYV 缩放 (查表法 + 循环展开 + 32位对齐)
*/
#define PREFETCH(addr) __builtin_prefetch((const void *)(addr), 0, 0)
void video_sw_scale_yuyv_extreme(uint8_t* src_img, int src_w, int src_h, 
								uint8_t* dst_img, int dst_w, int dst_h) 
{
   int x_ratio = (int)((src_w << 16) / dst_w) + 1;
   int y_ratio = (int)((src_h << 16) / dst_h) + 1;
   
   // ==========================================================
   // 优化 1：空间换时间 (LUT 查表法)
   // ==========================================================
   // 目标宽度 dst_w 是 480，每次处理 2 个像素（宏像素），内部循环要跑 240 次。
   // 我们没必要在内循环里每次都做乘法和位移！
   // 提前算好这 240 个宏像素在源图像一行的【字节偏移量】，存在栈内存里。
   // 240 * 4 字节 = 960 字节，RTOS 栈空间完全足够。
   int macro_count = dst_w / 2;
   uint32_t src_x_offset_lut[macro_count];
   
   for (int j = 0; j < dst_w; j += 2) {
	   int src_x = (j * x_ratio) >> 16;
	   src_x &= ~1; // 偶数对齐
	   src_x_offset_lut[j >> 1] = src_x * 2; // 直接存储字节偏移量
   }
   
   // ==========================================================
   // 优化 2 & 3：外层计算剥离 + 内循环展开 (Loop Unroll)
   // ==========================================================
   for (int i = 0; i < dst_h; i++) {
	   int src_y = (i * y_ratio) >> 16;
	   
	   uint8_t* src_row = src_img + (src_y * src_w * 2);
	   // 直接将目标行指针强制转换为 32 位指针，方便后续写入
	   uint32_t* dst_row_32 = (uint32_t*)(dst_img + (i * dst_w * 2));
	   
	   int k = 0;
	   
	   // 【核心内循环：每次处理 4 个宏像素 (8 个像素)】
	   // 这样做极大地减少了 for 循环的条件判断次数 (i++)，
	   // 同时让 CPU 内部的流水线可以连续发射多条内存读写指令！
	   for (; k <= macro_count - 4; k += 4) {
		   
		   // 优化 4：硬件预取 (Prefetch) [可选黑科技]
		   // 告诉 Cache："我马上要用后面的数据了，你提前去 PSRAM 拿回来！"
		   // 这能极大缓解跳跃读取导致的 Cache Miss 卡顿。
		   PREFETCH(src_row + src_x_offset_lut[k + 4]); 

		   // 一气呵成：查表 -> 取源数据 -> 写目标内存
		   dst_row_32[k]	 = *(uint32_t*)(src_row + src_x_offset_lut[k]);
		   dst_row_32[k + 1] = *(uint32_t*)(src_row + src_x_offset_lut[k + 1]);
		   dst_row_32[k + 2] = *(uint32_t*)(src_row + src_x_offset_lut[k + 2]);
		   dst_row_32[k + 3] = *(uint32_t*)(src_row + src_x_offset_lut[k + 3]);
	   }
	   
	   // 处理尾部剩余的像素 (防止 dst_w 不是 8 的倍数)
	   for (; k < macro_count; k++) {
		   dst_row_32[k] = *(uint32_t*)(src_row + src_x_offset_lut[k]);
	   }
   }
}

static frame_buffer_t *sport_dv_dma2d_yuyv_to_rgb565(frame_buffer_t *src_frame, uint16_t out_w, uint16_t out_h)
{
    if (src_frame == NULL || src_frame->frame == NULL || out_w == 0 || out_h == 0) {
        return NULL;
    }

    frame_buffer_t *rgb565_frame = frame_buffer_display_malloc(out_w * out_h * 2);
    if (rgb565_frame == NULL) {
        return NULL;
    }

    rgb565_frame->width = out_w;
    rgb565_frame->height = out_h;
    rgb565_frame->length = out_w * out_h * 2;
    rgb565_frame->fmt = PIXEL_FMT_RGB565;

    dma2d_memcpy_pfc_t dma2d_pfc = {0};
    dma2d_pfc.input_addr = src_frame->frame;
    dma2d_pfc.mode = DMA2D_M2M_PFC;
    dma2d_pfc.input_color_mode = DMA2D_INPUT_YUYV;
    dma2d_pfc.src_pixel_byte = TWO_BYTES;

    dma2d_pfc.output_addr = rgb565_frame->frame;
    dma2d_pfc.output_color_mode = DMA2D_OUTPUT_RGB565;
    dma2d_pfc.dst_pixel_byte = TWO_BYTES;
    dma2d_pfc.out_byte_by_byte_reverse = BYTE_BY_BYTE_REVERSE;

    dma2d_pfc.dma2d_width = out_w;
    dma2d_pfc.dma2d_height = out_h;
    dma2d_pfc.src_frame_width = out_w;
    dma2d_pfc.src_frame_height = out_h;
    dma2d_pfc.dst_frame_width = out_w;
    dma2d_pfc.dst_frame_height = out_h;

    bk_dma2d_memcpy_or_pixel_convert(&dma2d_pfc);
    bk_dma2d_start_transfer();
    while (bk_dma2d_is_transfer_busy()) {
    }

    return rgb565_frame;
}


static void sport_dv_display_task(beken_thread_arg_t arg)
{
    (void)arg;

    while (s_disp_thread_running) {
        frame_buffer_t *frame = NULL;
        bk_err_t ret = rtos_pop_from_queue(&s_disp_queue, &frame, BEKEN_WAIT_FOREVER);
        if (ret != kNoErr || frame == NULL) {
            continue;
        }

        if (s_disp_lcd != NULL) {

			frame_buffer_t *small_frame = frame_buffer_display_malloc(DVP_SCALE_PREVIEW_W * DVP_SCALE_PREVIEW_H * 2);
			if (small_frame != NULL) {

				small_frame->length = DVP_SCALE_PREVIEW_H * DVP_SCALE_PREVIEW_W * 2;
	            // 2. 硬件缩放：直接把画面缩放到新申请的 small_frame->frame 里
	            //video_hw_scale_frame(frame->frame, frame->width, frame->height, small_frame->frame, DVP_SCALE_PREVIEW_W, DVP_SCALE_PREVIEW_H);
	            
				video_sw_scale_yuyv_nearest(frame->frame, frame->width, frame->height, small_frame->frame, DVP_SCALE_PREVIEW_W, DVP_SCALE_PREVIEW_H);
				//video_sw_scale_yuyv_extreme(frame->frame, frame->width, frame->height, small_frame->frame, DVP_SCALE_PREVIEW_W, DVP_SCALE_PREVIEW_H);
				frame_buffer_display_free(frame);
			
	            small_frame->width = DVP_SCALE_PREVIEW_W;
	            small_frame->height = DVP_SCALE_PREVIEW_H;
	            small_frame->fmt = PIXEL_FMT_YUYV;

                frame_buffer_t *rgb565_frame = sport_dv_dma2d_yuyv_to_rgb565(small_frame, DVP_SCALE_PREVIEW_W, DVP_SCALE_PREVIEW_H);
                if (rgb565_frame != NULL) {
                    frame_buffer_display_free(small_frame);
                    small_frame = rgb565_frame;
                }

				avdk_err_t ret = bk_display_flush(s_disp_lcd, small_frame, sport_dv_display_frame_free_cb);
				if (ret != AVDK_ERR_OK)
				{
					LOGE("%s: bk_display_flush failed, ret:%d\n", __func__, ret);
					// Free frame on error
					frame_buffer_display_free(small_frame);
				}
	        }
			else
			{
                frame_buffer_t *rgb565_frame = sport_dv_dma2d_yuyv_to_rgb565(frame, frame->width, frame->height);
                if (rgb565_frame != NULL) {
                    frame_buffer_display_free(frame);
                    frame = rgb565_frame;
                } else {
                    frame->fmt = PIXEL_FMT_YUYV;
                }

                avdk_err_t flush_ret = bk_display_flush(s_disp_lcd, frame, sport_dv_display_frame_free_cb);
                
                if (flush_ret != AVDK_ERR_OK)
                {
                    LOGE("%s: bk_display_flush failed, ret:%d\n", __func__, flush_ret);
                    frame_buffer_display_free(frame); // 刷屏失败，手动释放内存
                }
			}	
			

			/*
            avdk_err_t flush_ret = bk_display_flush(s_disp_lcd, frame, sport_dv_display_frame_free_cb);
            if (flush_ret != AVDK_ERR_OK) {
                frame_buffer_display_free(frame);
            }
            */
        } else {
            frame_buffer_display_free(frame);
        }
    }

    s_disp_thread = NULL;
    rtos_delete_thread(NULL);
}

static avdk_err_t sport_dv_display_open_lcd(void)
{
    if (s_disp_lcd != NULL) {
        return AVDK_ERR_OK;
    }

    avdk_err_t ret = bk_pm_module_vote_ctrl_external_ldo(GPIO_CTRL_LDO_MODULE_LCD, SPORT_DV_LCD_LDO_PIN, GPIO_OUTPUT_STATE_HIGH);
    if (ret != AVDK_ERR_OK) {
        return ret;
    }

	#if LCD_DEVICE_DISP_SWITCH
    bk_display_rgb_ctlr_config_t lcd_display_config = {0};
    lcd_display_config.lcd_device = lcd_device;
    lcd_display_config.clk_pin = GPIO_0;
    lcd_display_config.cs_pin = GPIO_12;
    lcd_display_config.sda_pin = GPIO_1;
    lcd_display_config.rst_pin = GPIO_6;
    ret = bk_display_rgb_new(&s_disp_lcd, &lcd_display_config);
	#else
	bk_display_spi_ctlr_config_t spi_ctlr_config = {
	    .lcd_device = &lcd_device_st7735S,
	    .spi_id = 0,
	    .dc_pin = GPIO_17,
	    .reset_pin = GPIO_18,
	    .te_pin = 0,
	};
    // Create LCD display controller
	ret = bk_display_spi_new(&s_disp_lcd, &spi_ctlr_config);
	#endif 

    if (ret != AVDK_ERR_OK) {
        bk_pm_module_vote_ctrl_external_ldo(GPIO_CTRL_LDO_MODULE_LCD, SPORT_DV_LCD_LDO_PIN, GPIO_OUTPUT_STATE_LOW);
        s_disp_lcd = NULL;
        return ret;
    }

    ret = sport_dv_lcd_backlight_open(GPIO_7);
    if (ret != AVDK_ERR_OK) {
        bk_display_delete(s_disp_lcd);
        s_disp_lcd = NULL;
        bk_pm_module_vote_ctrl_external_ldo(GPIO_CTRL_LDO_MODULE_LCD, SPORT_DV_LCD_LDO_PIN, GPIO_OUTPUT_STATE_LOW);
        return ret;
    }

    ret = bk_display_open(s_disp_lcd);
    if (ret != AVDK_ERR_OK) {
        sport_dv_lcd_backlight_close(GPIO_7);
        bk_display_delete(s_disp_lcd);
        s_disp_lcd = NULL;
        bk_pm_module_vote_ctrl_external_ldo(GPIO_CTRL_LDO_MODULE_LCD, SPORT_DV_LCD_LDO_PIN, GPIO_OUTPUT_STATE_LOW);
        return ret;
    }

    return AVDK_ERR_OK;
}

static void sport_dv_display_close_lcd(void)
{
    if (s_disp_lcd == NULL) {
        return;
    }
    bk_display_close(s_disp_lcd);
    sport_dv_lcd_backlight_close(GPIO_7);
    bk_display_delete(s_disp_lcd);
    s_disp_lcd = NULL;
    bk_pm_module_vote_ctrl_external_ldo(GPIO_CTRL_LDO_MODULE_LCD, SPORT_DV_LCD_LDO_PIN, GPIO_OUTPUT_STATE_LOW);
}

avdk_err_t sport_dv_display_start(void)
{
    if (s_disp_refcnt == 0) {
        avdk_err_t ret = sport_dv_display_open_lcd();
        if (ret != AVDK_ERR_OK) {
            return ret;
        }

        if (s_disp_queue == NULL) {
            bk_err_t qret = rtos_init_queue(&s_disp_queue, "dv_lcd_q", sizeof(frame_buffer_t *), 3);
            if (qret != BK_OK) {
                sport_dv_display_close_lcd();
                s_disp_queue = NULL;
                return AVDK_ERR_GENERIC;
            }
        }

        if (s_disp_thread == NULL) {
            s_disp_thread_running = true;
            bk_err_t tret = rtos_create_thread(&s_disp_thread, 6, "dv_lcd", sport_dv_display_task, 2048, NULL);
            if (tret != BK_OK) {
                s_disp_thread_running = false;
                rtos_deinit_queue(&s_disp_queue);
                s_disp_queue = NULL;
                sport_dv_display_close_lcd();
                return AVDK_ERR_GENERIC;
            }
        }
    }

    s_disp_refcnt++;
    return AVDK_ERR_OK;
}

avdk_err_t sport_dv_display_stop(void)
{
    if (s_disp_refcnt == 0) {
        return AVDK_ERR_OK;
    }

    s_disp_refcnt--;
    if (s_disp_refcnt > 0) {
        return AVDK_ERR_OK;
    }

    if (s_disp_thread != NULL) {
        s_disp_thread_running = false;
        frame_buffer_t *exit_frame = NULL;
        (void)rtos_push_to_queue(&s_disp_queue, &exit_frame, 0);
        rtos_delay_milliseconds(20);
    }

    if (s_disp_queue != NULL) {
        rtos_deinit_queue(&s_disp_queue);
        s_disp_queue = NULL;
    }

    sport_dv_display_close_lcd();
    return AVDK_ERR_OK;
}

avdk_err_t sport_dv_display_push(frame_buffer_t *frame)
{
    if (frame == NULL) {
        return AVDK_ERR_INVAL;
    }

    if (s_disp_queue == NULL) {
        frame_buffer_display_free(frame);
        return AVDK_ERR_SHUTDOWN;
    }

    bk_err_t ret = rtos_push_to_queue(&s_disp_queue, &frame, 0);
    if (ret != kNoErr) {
        frame_buffer_display_free(frame);
        return AVDK_ERR_BUSY;
    }

    return AVDK_ERR_OK;
}

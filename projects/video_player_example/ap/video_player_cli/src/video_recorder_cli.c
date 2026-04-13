#include <common/bk_include.h>
#include <components/avdk_utils/avdk_error.h>
#include <components/bk_display.h>
#include <components/bk_camera_ctlr.h>
#include "audio_recorder_device.h"
#include <stdint.h>
#include <os/str.h>
#include <os/os.h>
#include <os/mem.h>
#include <driver/gpio.h>
#include <driver/pwr_clk.h>
#include <components/bk_audio/audio_pipeline/audio_types.h>
#include <components/bk_audio/audio_encoders/aac_encoder.h>
#include "cli.h"
#include "video_recorder_cli.h"
#include "video_player_common.h"
#include "module_test_cli.h"  // This includes extern declarations for voice handles
#include "frame_buffer.h"
#include "lcd_panel_devices.h"
#include <components/bk_video_recorder.h>
#include "components/bk_video_recorder_types.h"
#include "bk_video_recorder_ctlr.h"
#include "driver/h264_types.h"

#if CONFIG_LVGL
#include "lv_vendor.h"
#if CONFIG_LV_USE_DEMO_WIDGETS
#include "lv_demo_widgets.h"
#include <modules/image_scale.h>

#endif
#endif

#include "components/bk_dma2d.h"
#include "components/bk_dma2d_types.h"
#include "driver/dma2d.h"

#include "driver/hw_scale.h"
#include <driver/hw_scale_types.h>
#include <driver/psram.h>


#define TAG "video_recorder_cli"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)

#define LCD_DEVICE_SWITCH 0

#if LCD_DEVICE_SWITCH
static const lcd_device_t *lcd_device = &lcd_device_st7701s;// lcd_device_st7282
#endif 

// Static handles for video recording (DVP + MIC)
static bk_video_recorder_handle_t video_recorder_handle = NULL;
static bk_camera_ctlr_handle_t video_recorder_dvp_handle = NULL;
static bk_display_ctlr_handle_t video_recorder_lcd_handle = NULL;  // LCD handle for display during recording
static audio_recorder_device_handle_t video_recorder_audio_recorder_handle = NULL;
// Frame buffer queue for recording (frames from DVP)
#define VIDEO_RECORDER_FRAME_QUEUE_SIZE 4  // Queue size for frame buffers (increased to reduce frame drops)
static beken_queue_t video_recorder_frame_queue = NULL;  // Queue to store frame_buffer_t pointers
static image_format_t video_recorder_frame_format = IMAGE_MJPEG;  // Format of frames for proper release
// Statistics for frame drops
static uint32_t video_recorder_frame_drop_count = 0;  // Count of dropped frames due to queue full
static uint32_t video_recorder_frame_total_count = 0;  // Total frames from DVP
// Audio recorder device handle is used instead of circular buffer
// Static buffer for audio data reading (avoid stack overflow)
static uint8_t video_recorder_audio_temp_buffer[4096];

// Record output file path buffer (avoid using temporary stack buffer for file extension changes).
static char video_recorder_output_path[256] = {0};

// Audio codec is configured per start command. CLI always passes raw encoded frames to recorder.

// =============================================================================
// Recording duration limit
// =============================================================================

// Maximum recording duration: 5 minutes.
// Use a one-shot timer to auto-stop recording and avoid unbounded file growth.
#define VIDEO_RECORDER_MAX_DURATION_MS   (5 * MINUTES)

#if CONFIG_LVGL
/** When true, dvp_display opened its own display; when false, display is shared with LVGL. */

#if CONFIG_LV_USE_DEMO_WIDGETS
static bool dvp_display_owns_lcd = false;
static frame_buffer_t *dvp_preview_frame = NULL;
static void *dvp_preview_rgb565_buf = NULL;
static uint32_t dvp_preview_buf_w = 0;
static uint32_t dvp_preview_buf_h = 0;
#define DVP_PREVIEW_MAX_W  864
#define DVP_PREVIEW_MAX_H  480
extern beken_semaphore_t g_dvp_dma2d_sem;
static bool process_lvgl_preview_frame(frame_buffer_t *frame);
#endif
#endif


#define DVP_SCALE_PREVIEW_W  	160
#define DVP_SCALE_PREVIEW_H  	128

static beken2_timer_t video_recorder_auto_stop_timer = {0};
static uint32_t video_recorder_session_id = 0;
static beken_thread_t video_recorder_auto_stop_thread = NULL;
static bool video_recorder_auto_stop_timer_inited = false;
static bool video_recorder_auto_stop_timer_started = false;


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
static beken_queue_t g_lcd_display_queue = NULL;
static beken_thread_t g_lcd_display_thread = NULL;
static bool g_lcd_display_thread_running = false;

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


static void lcd_display_task(beken_thread_arg_t arg)
{
    frame_buffer_t *frame = NULL;
    bk_err_t ret;

    while (g_lcd_display_thread_running)
    {
        // 1. 从队列中挂起等待，直到收到新的画面帧（永久等待）
        ret = rtos_pop_from_queue(&g_lcd_display_queue, &frame, BEKEN_WAIT_FOREVER);
        
        // 2. 严密的出队校验：成功出队 且 帧不为空（包含收到退出信号的 NULL 帧）
        if (ret == kNoErr && frame != NULL)
        {
            if (video_recorder_lcd_handle != NULL)
            {
            	#if 1
            	frame_buffer_t *small_frame = frame_buffer_display_malloc(DVP_SCALE_PREVIEW_W * DVP_SCALE_PREVIEW_H * 2);
				if (small_frame != NULL) {

					small_frame->length = DVP_SCALE_PREVIEW_H * DVP_SCALE_PREVIEW_W * 2;
		            // 2. 硬件缩放：直接把画面缩放到新申请的 small_frame->frame 里
		            //video_hw_scale_frame(frame->frame, frame->width, frame->height, small_frame->frame, DVP_SCALE_PREVIEW_W, DVP_SCALE_PREVIEW_H);
		            
					//video_sw_scale_yuyv_nearest(frame->frame, frame->width, frame->height, small_frame->frame, DVP_SCALE_PREVIEW_W, DVP_SCALE_PREVIEW_H);
					video_sw_scale_yuyv_extreme(frame->frame, frame->width, frame->height, small_frame->frame, DVP_SCALE_PREVIEW_W, DVP_SCALE_PREVIEW_H);
					frame_buffer_display_free(frame);
				
		            small_frame->width = DVP_SCALE_PREVIEW_W;
		            small_frame->height = DVP_SCALE_PREVIEW_H;
		            small_frame->fmt = PIXEL_FMT_YUYV;

					#if CONFIG_LVGL && CONFIG_LV_USE_DEMO_WIDGETS
					if (process_lvgl_preview_frame(small_frame) == true) {
				        // 返回 true 说明它内部已经完全处理完毕，并顺手帮你把 frame 给 free 掉了！
				        // 主函数直接 return 或 continue 即可。
				        continue; 
				    }
					#endif

                    // Use DMA2D to convert YUYV to RGB565 and perform hardware byte-swap for ST7735S (Big-Endian SPI LCD)
                    frame_buffer_t *rgb565_frame = frame_buffer_display_malloc(DVP_SCALE_PREVIEW_W * DVP_SCALE_PREVIEW_H * 2);
                    if (rgb565_frame != NULL) {
                        rgb565_frame->width = DVP_SCALE_PREVIEW_W;
                        rgb565_frame->height = DVP_SCALE_PREVIEW_H;
                        rgb565_frame->length = DVP_SCALE_PREVIEW_W * DVP_SCALE_PREVIEW_H * 2;
                        rgb565_frame->fmt = PIXEL_FMT_RGB565;

                        dma2d_memcpy_pfc_t dma2d_pfc = {0};
                        dma2d_pfc.input_addr = small_frame->frame;
                        dma2d_pfc.mode = DMA2D_M2M_PFC;
                        dma2d_pfc.input_color_mode = DMA2D_INPUT_YUYV;
                        dma2d_pfc.src_pixel_byte = TWO_BYTES;
                        
                        dma2d_pfc.output_addr = rgb565_frame->frame;
                        dma2d_pfc.output_color_mode = DMA2D_OUTPUT_RGB565;
                        dma2d_pfc.dst_pixel_byte = TWO_BYTES;
                        dma2d_pfc.out_byte_by_byte_reverse = BYTE_BY_BYTE_REVERSE; // HW byte swap for SPI LCD
                        
                        dma2d_pfc.dma2d_width = DVP_SCALE_PREVIEW_W;
                        dma2d_pfc.dma2d_height = DVP_SCALE_PREVIEW_H;
                        dma2d_pfc.src_frame_width = DVP_SCALE_PREVIEW_W;
                        dma2d_pfc.src_frame_height = DVP_SCALE_PREVIEW_H;
                        dma2d_pfc.dst_frame_width = DVP_SCALE_PREVIEW_W;
                        dma2d_pfc.dst_frame_height = DVP_SCALE_PREVIEW_H;

                        bk_dma2d_memcpy_or_pixel_convert(&dma2d_pfc);
                        bk_dma2d_start_transfer();
                        while(bk_dma2d_is_transfer_busy()) {}

                        frame_buffer_display_free(small_frame); // Free the YUYV scaled frame
                        small_frame = rgb565_frame;             // Use the new RGB565 frame for display

						small_frame->fmt = PIXEL_FMT_RGB565;
                    }

					avdk_err_t ret = bk_display_flush(video_recorder_lcd_handle, small_frame, display_frame_free_cb);
					if (ret != AVDK_ERR_OK)
					{
						LOGE("%s: bk_display_flush failed, ret:%d\n", __func__, ret);
						// Free frame on error
						frame_buffer_display_free(small_frame);
					}
		        }
				else
				{
                    // Use DMA2D to convert original YUYV to RGB565 and perform hardware byte-swap for ST7735S
                    frame_buffer_t *rgb565_frame = frame_buffer_display_malloc(frame->width * frame->height * 2);
                    if (rgb565_frame != NULL) {
                        rgb565_frame->width = frame->width;
                        rgb565_frame->height = frame->height;
                        rgb565_frame->length = frame->width * frame->height * 2;
                        rgb565_frame->fmt = PIXEL_FMT_RGB565;

                        dma2d_memcpy_pfc_t dma2d_pfc = {0};
                        dma2d_pfc.input_addr = frame->frame;
                        dma2d_pfc.mode = DMA2D_M2M_PFC;
                        dma2d_pfc.input_color_mode = DMA2D_INPUT_YUYV;
                        dma2d_pfc.src_pixel_byte = TWO_BYTES;
                        
                        dma2d_pfc.output_addr = rgb565_frame->frame;
                        dma2d_pfc.output_color_mode = DMA2D_OUTPUT_RGB565;
                        dma2d_pfc.dst_pixel_byte = TWO_BYTES;
                        dma2d_pfc.out_byte_by_byte_reverse = BYTE_BY_BYTE_REVERSE; // HW byte swap for SPI LCD
                        
                        dma2d_pfc.dma2d_width = frame->width;
                        dma2d_pfc.dma2d_height = frame->height;
                        dma2d_pfc.src_frame_width = frame->width;
                        dma2d_pfc.src_frame_height = frame->height;
                        dma2d_pfc.dst_frame_width = frame->width;
                        dma2d_pfc.dst_frame_height = frame->height;

                        bk_dma2d_memcpy_or_pixel_convert(&dma2d_pfc);
                        bk_dma2d_start_transfer();
                        while(bk_dma2d_is_transfer_busy()) {}

                        frame_buffer_display_free(frame); // Free the original YUYV frame
                        frame = rgb565_frame;             // Use the new RGB565 frame for display
                    } else {
	                    frame->fmt = PIXEL_FMT_YUYV;
                    }

	                avdk_err_t flush_ret = bk_display_flush(video_recorder_lcd_handle, frame, display_frame_free_cb);
	                
	                if (flush_ret != AVDK_ERR_OK)
	                {
	                    LOGE("%s: bk_display_flush failed, ret:%d\n", __func__, flush_ret);
	                    frame_buffer_display_free(frame); // 刷屏失败，手动释放内存
	                }
				}

                // 刷屏成功的话，底层驱动会通过 display_frame_free_cb 自动释放，这里不再 free
                #else
				
                // Use DMA2D to convert original YUYV to RGB565 and perform hardware byte-swap for ST7735S
                frame_buffer_t *rgb565_frame = frame_buffer_display_malloc(frame->width * frame->height * 2);
                if (rgb565_frame != NULL) {
                    rgb565_frame->width = frame->width;
                    rgb565_frame->height = frame->height;
                    rgb565_frame->length = frame->width * frame->height * 2;
                    rgb565_frame->fmt = PIXEL_FMT_RGB565;

                    dma2d_memcpy_pfc_t dma2d_pfc = {0};
                    dma2d_pfc.input_addr = frame->frame;
                    dma2d_pfc.mode = DMA2D_M2M_PFC;
                    dma2d_pfc.input_color_mode = DMA2D_INPUT_YUYV;
                    dma2d_pfc.src_pixel_byte = TWO_BYTES;
                    
                    dma2d_pfc.output_addr = rgb565_frame->frame;
                    dma2d_pfc.output_color_mode = DMA2D_OUTPUT_RGB565;
                    dma2d_pfc.dst_pixel_byte = TWO_BYTES;
                    dma2d_pfc.out_byte_by_byte_reverse = BYTE_BY_BYTE_REVERSE; // HW byte swap for SPI LCD
                    
                    dma2d_pfc.dma2d_width = frame->width;
                    dma2d_pfc.dma2d_height = frame->height;
                    dma2d_pfc.src_frame_width = frame->width;
                    dma2d_pfc.src_frame_height = frame->height;
                    dma2d_pfc.dst_frame_width = frame->width;
                    dma2d_pfc.dst_frame_height = frame->height;

                    bk_dma2d_memcpy_or_pixel_convert(&dma2d_pfc);
                    bk_dma2d_start_transfer();
                    while(bk_dma2d_is_transfer_busy()) {}

                    frame_buffer_display_free(frame); // Free the original YUYV frame
                    frame = rgb565_frame;             // Use the new RGB565 frame for display
                } else {
                    frame->fmt = PIXEL_FMT_YUYV;
                }

                avdk_err_t flush_ret = bk_display_flush(video_recorder_lcd_handle, frame, display_frame_free_cb);
                
                if (flush_ret != AVDK_ERR_OK)
                {
                    LOGE("%s: bk_display_flush failed, ret:%d\n", __func__, flush_ret);
                    frame_buffer_display_free(frame); // 刷屏失败，手动释放内存
                }
                // 刷屏成功的话，底层驱动会通过 display_frame_free_cb 自动释放，这里不再 free
				#endif 
            }
            else
            {
                // 如果 LCD 被关了，丢弃该帧并释放内存
                frame_buffer_display_free(frame);
            }
        }
    }

    // 线程退出时的清理动作
    LOGI("LCD display task exit.\n");
    g_lcd_display_thread = NULL;
    rtos_delete_thread(NULL);
}

static bk_err_t lcd_display_thread_init(void)
{
    // 1. 创建队列：深度设为 3~4 足矣，存的是指针 (4 字节)
    if (g_lcd_display_queue == NULL) {
        rtos_init_queue(&g_lcd_display_queue, "lcd_disp_q", sizeof(frame_buffer_t *), 3);
    }

    // 2. 创建刷屏专用的后台线程
    if (g_lcd_display_thread == NULL) {
        g_lcd_display_thread_running = true;
        // 优先级可以设为 4 或 5 (一般低于音视频编码任务，高于 UI 任务)
        rtos_create_thread(&g_lcd_display_thread, 6, "lcd_disp_task", lcd_display_task, 2048, NULL);
    }
    return BK_OK;
}

#if CONFIG_LV_USE_DEMO_WIDGETS
/**
 * @brief 处理 LVGL 界面的摄像头预览帧 (YUYV 转 RGB565)
 * * @param frame 包含原始 YUYV 数据的帧结构体指针
 * @return true  该帧属于 LVGL 预览分支，已被处理并释放，调用者可直接 return。
 * @return false 该帧不属于 LVGL 预览分支，调用者需要继续走其他处理逻辑（如推流等）。
 */
static bool process_lvgl_preview_frame(frame_buffer_t *frame)
{
    // 如果不是 LVGL 预览句柄，直接返回 false，让外部代码接管这帧数据
    if (video_recorder_lcd_handle != lv_vendor_get_display_handle()) {
        return false;
    }

    // 严密校验：缓存存在 且 帧尺寸合法 且 帧尺寸没有超过我们申请的最大缓存
    if (dvp_preview_rgb565_buf != NULL && frame->width > 0 && frame->height > 0
        && frame->width <= (int)dvp_preview_buf_w && frame->height <= (int)dvp_preview_buf_h)
    {
        uint16_t lcd_w = lcd_device->width;
        uint16_t lcd_h = lcd_device->height;

        dma2d_memcpy_pfc_t dma2d_pfc = {0};
        
        // 1. 配置输入层 (DVP 产生的 YUV 原始帧)
        dma2d_pfc.input_addr = frame->frame;
        dma2d_pfc.mode = DMA2D_M2M_PFC;
        dma2d_pfc.input_color_mode = DMA2D_INPUT_YUYV;
        dma2d_pfc.src_pixel_byte = TWO_BYTES;
        
        // 2. 配置输出层 (LVGL 绑定的 RGB565 背景图内存)
        dma2d_pfc.output_addr = dvp_preview_rgb565_buf;
        dma2d_pfc.output_color_mode = DMA2D_OUTPUT_RGB565;
        dma2d_pfc.dst_pixel_byte = TWO_BYTES;
        
        // 3. 配置尺寸与居中裁剪
        dma2d_pfc.dma2d_width = (frame->width < lcd_w) ? frame->width : lcd_w;
        dma2d_pfc.dma2d_height = (frame->height < lcd_h) ? frame->height : lcd_h;
        
        dma2d_pfc.src_frame_width = frame->width;
        dma2d_pfc.src_frame_height = frame->height;
        // 计算 X Y 偏移量实现居中切图
        dma2d_pfc.src_frame_xpos = (frame->width > lcd_w) ? (frame->width - lcd_w) / 2 : 0;
        dma2d_pfc.src_frame_ypos = (frame->height > lcd_h) ? (frame->height - lcd_h) / 2 : 0;
        
        dma2d_pfc.dst_frame_width = lcd_w;
        dma2d_pfc.dst_frame_height = lcd_h;
        dma2d_pfc.dst_frame_xpos = 0;
        dma2d_pfc.dst_frame_ypos = 0;

        // 4. 装载配置并启动 DMA2D 硬件流水线
        bk_dma2d_memcpy_or_pixel_convert(&dma2d_pfc);
        bk_dma2d_start_transfer();
        
        // ==========================================================
        // 【挂起等待】：交出 CPU 使用权，阻塞等待硬件干完活
        // ==========================================================
        if (g_dvp_dma2d_sem != NULL) {
            // 超时时间设为 1000ms 防死锁
            rtos_get_semaphore(&g_dvp_dma2d_sem, 1000); 
        } else {
            // 兜底保护：如果信号量没建成功，则退回到轮询模式
            while(bk_dma2d_is_transfer_busy()) {
                // CPU 死等
            }
        }
        
        // 5. 通知 LVGL 引擎进行脏矩形重绘
        lv_vendor_disp_lock();
        lv_dvp_preview_set_buffer(dvp_preview_rgb565_buf,
                                  (uint32_t)frame->width, (uint32_t)frame->height);
        lv_dvp_preview_invalidate();
        lv_vendor_disp_unlock();
        lv_vendor_request_refresh();
    }

    // 【极其关键的收尾】：只要是 LVGL 的业务分支，无论刚才的 if 进没进去，
    // 原版大图走到这里就已经失去利用价值了，必须立刻释放，防止内存泄漏！
    frame_buffer_display_free(frame);
    
    return true; 
}
#endif


// Note:
// For AVI, ADTS header is prepended inside AVI write path (bk_video_recorder_avi.c).
// The CLI layer always passes raw AAC access units to the recorder.

// DVP frame malloc callback for recording
static frame_buffer_t *video_recorder_dvp_frame_malloc(image_format_t format, uint32_t size)
{
    frame_buffer_t *frame = NULL;

    // For recording, we prefer encoded formats (MJPEG/H264) to reduce file size
    if (format == IMAGE_MJPEG || format == IMAGE_H264 || format == IMAGE_H265)
    {
        frame = frame_buffer_encode_malloc(size);
    }
    else if (format == IMAGE_YUV)
    {
        frame = frame_buffer_display_malloc(size);
    }
    else
    {
        LOGE("%s: unsupported format: %d\n", __func__, format);
        return NULL;
    }

    if (frame)
    {
        frame->sequence = 0;
        frame->length = 0;
        frame->timestamp = 0;
        frame->size = size;
    }

    return frame;
}

// DVP frame complete callback for recording
// - YUV frames: display to LCD
// - Encoded frames (MJPEG/H264): push to recording queue
static void video_recorder_dvp_frame_complete(image_format_t format, frame_buffer_t *frame, int result)
{
    if (frame == NULL)
    {
        return;
    }

    // Only process valid frames
    if (result == AVDK_ERR_OK && frame->frame != NULL && frame->length > 0)
    {
        if (format == IMAGE_YUV)
        {
            // Display YUV frame to LCD
            if (video_recorder_lcd_handle != NULL && g_lcd_display_queue != NULL)
            {
                // 将指针塞入队列 (不阻塞，0 超时)
                bk_err_t push_ret = rtos_push_to_queue(&g_lcd_display_queue, &frame, 0);
                if (push_ret != kNoErr)
                {
                    // 队列满了说明刷屏线程处理不过来了 (背压)。
                    // 此时必须主动丢弃这一帧，防止内存爆掉！
                    LOGW("LCD Queue full, drop frame!\n"); // 调试用，量产可注释
                    frame_buffer_display_free(frame);
                }
            }
            else
            {
                // LCD not opened, free frame directly
                frame_buffer_display_free(frame);
            }
        }
        else if (format == IMAGE_MJPEG || format == IMAGE_H264 || format == IMAGE_H265)
        {
            // Save format for later use
            video_recorder_frame_format = format;

            // Push encoded frame to recording queue (non-blocking, must return quickly from DVP callback)
            if (video_recorder_frame_queue != NULL)
            {
                video_recorder_frame_total_count++;
                // Non-blocking push (timeout=0) to avoid delaying DVP callback
                // If queue is full, drop frame to prevent blocking
                bk_err_t ret = rtos_push_to_queue(&video_recorder_frame_queue, &frame, 0);
                if (ret != kNoErr)
                {
                    // Queue is full, free this frame to avoid memory leak
                    video_recorder_frame_drop_count++;
                    if (video_recorder_frame_drop_count % 30 == 0)  // Log every 30 dropped frames to avoid spam
                    {
                        LOGW("%s: Frame queue full, dropped %u frames (total: %u, drop rate: %.1f%%)\n",
                             __func__, video_recorder_frame_drop_count, video_recorder_frame_total_count,
                             (video_recorder_frame_drop_count * 100.0f) / video_recorder_frame_total_count);
                    }
                    frame_buffer_encode_free(frame);
                }
            }
            else
            {
                // Queue not initialized, free frame
                frame_buffer_encode_free(frame);
            }
        }
        else
        {
            // Unsupported format, free frame
            if (format == IMAGE_MJPEG || format == IMAGE_H264 || format == IMAGE_H265)
            {
                frame_buffer_encode_free(frame);
            }
            else if (format == IMAGE_YUV)
            {
                frame_buffer_display_free(frame);
            }
        }
    }
    else
    {
        // Free frame on error
        if (format == IMAGE_MJPEG || format == IMAGE_H264 || format == IMAGE_H265)
        {
            frame_buffer_encode_free(frame);
        }
        else if (format == IMAGE_YUV)
        {
            frame_buffer_display_free(frame);
        }
    }
}

static const bk_dvp_callback_t video_recorder_dvp_cbs = {
    .malloc = video_recorder_dvp_frame_malloc,
    .complete = video_recorder_dvp_frame_complete,
};


// Video record callback: get frame data from queue
static int video_recorder_get_frame_cb(void *user_data, video_recorder_frame_data_t *frame_data)
{
    if (frame_data == NULL || video_recorder_frame_queue == NULL)
    {
        return -1;
    }

    // Pop frame from queue (non-blocking, return immediately if no frame available)
    frame_buffer_t *frame = NULL;
    bk_err_t ret = rtos_pop_from_queue(&video_recorder_frame_queue, &frame, 0);
    if (ret != kNoErr || frame == NULL)
    {
        // No frame available, recording thread will retry in next loop
        return -1;
    }

    // Verify frame is valid
    if (frame->frame == NULL || frame->length == 0)
    {
        // Invalid frame, free it and return error
        if (video_recorder_frame_format == IMAGE_MJPEG ||
            video_recorder_frame_format == IMAGE_H264 ||
            video_recorder_frame_format == IMAGE_H265)
        {
            frame_buffer_encode_free(frame);
        }
        else if (video_recorder_frame_format == IMAGE_YUV)
        {
            frame_buffer_display_free(frame);
        }
        return -1;
    }

    // Fill frame data structure
    frame_data->data = frame->frame;
    frame_data->length = frame->length;
    frame_data->width = frame->width;
    frame_data->height = frame->height;
    frame_data->frame_buffer = frame;  // Store frame buffer pointer for later release
    // Use driver-provided H264 type flag to mark key frames for recording start.
    if (video_recorder_frame_format == IMAGE_H264)
    {
        frame_data->is_key_frame = (frame->h264_type & (1U << H264_NAL_I_FRAME)) ||
                                   (frame->h264_type & (1U << H264_NAL_IDR_SLICE));
    }
    else
    {
        frame_data->is_key_frame = 1;
    }

    return 0;  // Success
}

// Video record callback: get audio data from MIC
// Note: This callback is called by recording thread to get audio data
// The audio_data structure should be filled with a pointer to audio buffer and its length
// The buffer will be freed in release_audio_cb
static int video_recorder_get_audio_cb(void *user_data, video_recorder_audio_data_t *audio_data)
{
    if (audio_data == NULL || video_recorder_audio_recorder_handle == NULL)
    {
        return -1;
    }

    // Read audio data from audio recorder device
    uint32_t data_len = 0;
    avdk_err_t ret = audio_recorder_device_read(video_recorder_audio_recorder_handle, video_recorder_audio_temp_buffer, sizeof(video_recorder_audio_temp_buffer), &data_len);

    if (ret == AVDK_ERR_OK && data_len > 0)
    {
        // Always pass raw audio frames to recorder:
        // - PCM: raw PCM
        // - AAC: raw AAC access unit (no ADTS); AVI write path will prepend ADTS if needed
        uint8_t *audio_buffer = psram_malloc(data_len);
        if (audio_buffer == NULL)
        {
            LOGE("%s: Failed to allocate audio buffer\n", __func__);
            return -1;
        }
        os_memcpy(audio_buffer, video_recorder_audio_temp_buffer, data_len);
        
        audio_data->data = audio_buffer;
        audio_data->length = data_len;
        return 0;  // Success
    }
    else if (ret == AVDK_ERR_TIMEOUT || ret == AVDK_ERR_EOF)
    {
        // No data available (timeout or end of stream), return -1 to indicate no data
        return -1;
    }
    else
    {
        // Error reading audio data
        LOGE("%s: audio_recorder_device_read failed, ret=%d\n", __func__, ret);
        return -1;
    }
}

// Video record callback: release frame data after writing
static void video_recorder_release_frame_cb(void *user_data, video_recorder_frame_data_t *frame_data)
{
    if (frame_data == NULL)
    {
        return;
    }

    // Get frame buffer pointer from frame_data
    frame_buffer_t *frame = (frame_buffer_t *)frame_data->frame_buffer;
    if (frame != NULL)
    {
        // Free frame buffer based on format
        if (video_recorder_frame_format == IMAGE_MJPEG ||
            video_recorder_frame_format == IMAGE_H264 ||
            video_recorder_frame_format == IMAGE_H265)
        {
            frame_buffer_encode_free(frame);
        }
        else if (video_recorder_frame_format == IMAGE_YUV)
        {
            frame_buffer_display_free(frame);
        }
    }

    // Clear frame_data structure
    frame_data->data = NULL;
    frame_data->length = 0;
    frame_data->frame_buffer = NULL;
}

// Video record callback: release audio data after writing
static void video_recorder_release_audio_cb(void *user_data, video_recorder_audio_data_t *audio_data)
{
    // Free the audio buffer allocated in get_audio_cb
    if (audio_data != NULL && audio_data->data != NULL)
    {
        psram_free(audio_data->data);
        audio_data->data = NULL;
        audio_data->length = 0;
    }
}

static void video_recorder_cancel_auto_stop_timer(void)
{
    // Best-effort stop/deinit without calling rtos_is_* helpers on an uninitialized timer.
    if (video_recorder_auto_stop_timer_started)
    {
        (void)rtos_stop_oneshot_timer(&video_recorder_auto_stop_timer);
        video_recorder_auto_stop_timer_started = false;
    }
    if (video_recorder_auto_stop_timer_inited)
    {
        (void)rtos_deinit_oneshot_timer(&video_recorder_auto_stop_timer);
        video_recorder_auto_stop_timer_inited = false;
    }
}

static avdk_err_t video_recorder_stop_internal(bool from_timeout)
{
    avdk_err_t ret = AVDK_ERR_OK;

    if (video_recorder_handle == NULL)
    {
        return AVDK_ERR_INVAL;
    }

    // Cancel timer first to prevent re-entrancy.
    video_recorder_cancel_auto_stop_timer();

    if (from_timeout)
    {
        LOGW("%s: Auto stopping recording after %u ms\n", __func__, (unsigned)VIDEO_RECORDER_MAX_DURATION_MS);
    }

    // Stop recording
    ret = bk_video_recorder_stop(video_recorder_handle);
    if (ret != AVDK_ERR_OK)
    {
        LOGE("%s: bk_video_recorder_stop failed, ret=%d\n", __func__, ret);
    }

    // Print frame drop statistics
    if (video_recorder_frame_total_count > 0)
    {
        float drop_rate = (video_recorder_frame_drop_count * 100.0f) / video_recorder_frame_total_count;
        LOGI("%s: Frame statistics - Total: %u, Dropped: %u (%.1f%%), Recorded: %u\n",
             __func__, video_recorder_frame_total_count, video_recorder_frame_drop_count,
             drop_rate, video_recorder_frame_total_count - video_recorder_frame_drop_count);
    }

    // Reset statistics
    video_recorder_frame_total_count = 0;
    video_recorder_frame_drop_count = 0;

    // Close video record
    avdk_err_t tmp = bk_video_recorder_close(video_recorder_handle);
    if (tmp != AVDK_ERR_OK)
    {
        LOGE("%s: bk_video_recorder_close failed, ret=%d\n", __func__, tmp);
        if (ret == AVDK_ERR_OK)
        {
            ret = tmp;
        }
    }

    // Delete video record
    tmp = bk_video_recorder_delete(video_recorder_handle);
    if (tmp != AVDK_ERR_OK)
    {
        LOGE("%s: bk_video_recorder_delete failed, ret=%d\n", __func__, tmp);
        if (ret == AVDK_ERR_OK)
        {
            ret = tmp;
        }
    }
    video_recorder_handle = NULL;

    // Stop and close audio recorder
    if (video_recorder_audio_recorder_handle != NULL)
    {
        audio_recorder_device_stop(video_recorder_audio_recorder_handle);
        audio_recorder_device_deinit(video_recorder_audio_recorder_handle);
        video_recorder_audio_recorder_handle = NULL;
    }

    // Close DVP camera
    if (video_recorder_dvp_handle != NULL)
    {
        tmp = bk_camera_close(video_recorder_dvp_handle);
        if (tmp != AVDK_ERR_OK)
        {
            LOGE("%s: bk_camera_close failed, ret=%d\n", __func__, tmp);
            if (ret == AVDK_ERR_OK)
            {
                ret = tmp;
            }
        }

        tmp = bk_camera_delete(video_recorder_dvp_handle);
        if (tmp != AVDK_ERR_OK)
        {
            LOGE("%s: bk_camera_delete failed, ret=%d\n", __func__, tmp);
            if (ret == AVDK_ERR_OK)
            {
                ret = tmp;
            }
        }
        video_recorder_dvp_handle = NULL;

        // Power off DVP camera
        if (DVP_POWER_GPIO_ID != GPIO_INVALID_ID)
        {
            GPIO_DOWN(DVP_POWER_GPIO_ID);
        }
    }

    // Close LCD display
    if (video_recorder_lcd_handle != NULL)
    {
        tmp = bk_display_close(video_recorder_lcd_handle);
        if (tmp != AVDK_ERR_OK)
        {
            LOGE("%s: bk_display_close failed, ret=%d\n", __func__, tmp);
            if (ret == AVDK_ERR_OK)
            {
                ret = tmp;
            }
        }

        lcd_backlight_close(GPIO_7);

        tmp = bk_display_delete(video_recorder_lcd_handle);
        if (tmp != AVDK_ERR_OK)
        {
            LOGE("%s: bk_display_delete failed, ret=%d\n", __func__, tmp);
            if (ret == AVDK_ERR_OK)
            {
                ret = tmp;
            }
        }
        video_recorder_lcd_handle = NULL;

        // Power off LCD LDO (best-effort).
        (void)bk_pm_module_vote_ctrl_external_ldo(GPIO_CTRL_LDO_MODULE_LCD, LCD_LDO_PIN, GPIO_OUTPUT_STATE_LOW);
    }

    // Clean up frame queue - free all remaining frames
    if (video_recorder_frame_queue != NULL)
    {
        frame_buffer_t *frame = NULL;
        while (rtos_pop_from_queue(&video_recorder_frame_queue, &frame, 0) == kNoErr && frame != NULL)
        {
            if (video_recorder_frame_format == IMAGE_MJPEG ||
                video_recorder_frame_format == IMAGE_H264 ||
                video_recorder_frame_format == IMAGE_H265)
            {
                frame_buffer_encode_free(frame);
            }
            else if (video_recorder_frame_format == IMAGE_YUV)
            {
                frame_buffer_display_free(frame);
            }
        }

        rtos_deinit_queue(&video_recorder_frame_queue);
        video_recorder_frame_queue = NULL;
    }

    LOGD("%s: Video recording stopped successfully\n", __func__);
    return ret;
}

static void video_recorder_auto_stop_thread_entry(beken_thread_arg_t arg)
{
    uint32_t sid = (uint32_t)(uintptr_t)arg;

    // Only stop if this is the latest recording session and recording is still active.
    if (sid == video_recorder_session_id && video_recorder_handle != NULL)
    {
        (void)video_recorder_stop_internal(true);
    }

    video_recorder_auto_stop_thread = NULL;
    rtos_delete_thread(NULL);
}

static void video_recorder_auto_stop_timeout(void *Larg, void *Rarg)
{
    (void)Rarg;
    uint32_t sid = (uint32_t)(uintptr_t)Larg;

    // One-shot timer has fired. Mark it as not running.
    video_recorder_auto_stop_timer_started = false;

    // Avoid creating multiple stop threads.
    if (video_recorder_auto_stop_thread != NULL)
    {
        return;
    }

    // Defer heavy stop operations to a dedicated thread (do not block timer context).
    bk_err_t ret = rtos_create_thread(&video_recorder_auto_stop_thread,
                                     BEKEN_APPLICATION_PRIORITY,
                                     "vid_rec_auto_stop",
                                     video_recorder_auto_stop_thread_entry,
                                     2048,
                                     (beken_thread_arg_t)(uintptr_t)sid);
    if (ret != BK_OK)
    {
        LOGE("%s: create auto stop thread failed, ret=%d\n", __func__, ret);
        video_recorder_auto_stop_thread = NULL;
    }
}

static bk_err_t video_recorder_start_auto_stop_timer(uint32_t sid)
{
    // Re-init for each start to ensure a clean state.
    video_recorder_cancel_auto_stop_timer();

    bk_err_t ret = rtos_init_oneshot_timer(&video_recorder_auto_stop_timer,
                                          VIDEO_RECORDER_MAX_DURATION_MS,
                                          video_recorder_auto_stop_timeout,
                                          (void *)(uintptr_t)sid,
                                          NULL);
    if (ret != BK_OK)
    {
        return ret;
    }
    video_recorder_auto_stop_timer_inited = true;

    ret = rtos_start_oneshot_timer(&video_recorder_auto_stop_timer);
    if (ret != BK_OK)
    {
        (void)rtos_deinit_oneshot_timer(&video_recorder_auto_stop_timer);
        video_recorder_auto_stop_timer_inited = false;
        video_recorder_auto_stop_timer_started = false;
        return ret;
    }
    video_recorder_auto_stop_timer_started = true;
    return ret;
}

// Video record command handler - record DVP data and MIC data
void cli_video_record_cmd(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
    char *msg = CLI_CMD_RSP_ERROR;
    avdk_err_t ret = AVDK_ERR_GENERIC;
    uint16_t output_format = IMAGE_MJPEG;
    uint32_t record_type = VIDEO_RECORDER_TYPE_AVI;
    // Note: keep function logic linear; no need for a separate "start_ok" flag.

    if (argc < 2)
    {
        LOGE("%s: insufficient arguments\n", __func__);
        goto exit;
    }

    if (os_strcmp(argv[1], "start") == 0)
    {
        // Check if already recording
        if (video_recorder_handle != NULL)
        {
            LOGE("%s: video recording already started\n", __func__);
            goto exit;
        }

        // Mount SD card before recording (required for saving video file)
        int mount_ret = sd_card_mount();
        if (mount_ret != BK_OK)
        {
            LOGE("%s: Failed to mount SD card, ret=%d. Please mount SD card first\n", __func__, mount_ret);
            msg = CLI_CMD_RSP_ERROR;
            goto exit;
        }
        LOGD("%s: SD card mounted successfully\n", __func__);

        // Parse arguments: start [file_path] [width] [height] [format] [type]
        // Default values
        char *file_path = "/sd0/record.avi";
        uint32_t width = 480;
        uint32_t height = 320;

        // Audio format selection (default: PCM)
        uint32_t audio_format = VIDEO_RECORDER_AUDIO_FORMAT_PCM;
        if (cmd_contain(argc, argv, "aac") || cmd_contain(argc, argv, "AAC"))
        {
            audio_format = VIDEO_RECORDER_AUDIO_FORMAT_AAC;
        }
        else if (cmd_contain(argc, argv, "g711u") || cmd_contain(argc, argv, "G711U") ||
                 cmd_contain(argc, argv, "ulaw") || cmd_contain(argc, argv, "ULAW"))
        {
            audio_format = VIDEO_RECORDER_AUDIO_FORMAT_MULAW;
        }
        else if (cmd_contain(argc, argv, "g711a") || cmd_contain(argc, argv, "G711A") ||
                 cmd_contain(argc, argv, "alaw") || cmd_contain(argc, argv, "ALAW") ||
                 cmd_contain(argc, argv, "g711") || cmd_contain(argc, argv, "G711"))
        {
            // Default G.711 to A-law.
            audio_format = VIDEO_RECORDER_AUDIO_FORMAT_ALAW;
        }
        else if (cmd_contain(argc, argv, "g722") || cmd_contain(argc, argv, "G722"))
        {
            audio_format = VIDEO_RECORDER_AUDIO_FORMAT_G722;
        }
        else if (cmd_contain(argc, argv, "mp3") || cmd_contain(argc, argv, "MP3"))
        {
            audio_format = VIDEO_RECORDER_AUDIO_FORMAT_MP3;
        }
        if (argc >= 4)
        {
            //file_path = argv[2];
            width = os_strtoul(argv[2], NULL, 10);
            height = os_strtoul(argv[3], NULL, 10);
        }

        // Copy to persistent buffer (file_path may point to argv/const, but we may modify extension).
        os_memset(video_recorder_output_path, 0, sizeof(video_recorder_output_path));
        os_strncpy(video_recorder_output_path, file_path, sizeof(video_recorder_output_path) - 1);
        file_path = video_recorder_output_path;

        if (argc >= 5)
        {
        	file_path = argv[4];
            //width = os_strtoul(argv[3], NULL, 10);
           // height = os_strtoul(argv[4], NULL, 10);
        }
        if (cmd_contain(argc, argv, "mp4"))
        {
            record_type = VIDEO_RECORDER_TYPE_MP4;
            if (os_strstr(file_path, ".avi") != NULL)
            {
                // Replace .avi with .mp4
                char *ext = os_strstr(video_recorder_output_path, ".avi");
                if (ext != NULL)
                {
                    os_strncpy(ext, ".mp4", 4);
                }
            }
        }
        else
        {
            // Default is AVI. If user passes a .mp4 path without 'mp4' keyword, normalize extension.
            if (os_strstr(file_path, ".mp4") != NULL)
            {
                char *ext = os_strstr(video_recorder_output_path, ".mp4");
                if (ext != NULL)
                {
                    os_strncpy(ext, ".avi", 4);
                }
            }
        }

        // For AAC, CLI always provides raw AAC access units.
        // AVI mux will prepend ADTS header inside avi_write_audio_data().
        (void)record_type;
        if (cmd_contain(argc, argv, "h264") || cmd_contain(argc, argv, "H264"))
        {
            output_format = IMAGE_H264;
        }
        else if (cmd_contain(argc, argv, "mjpeg") || cmd_contain(argc, argv, "MJPEG") || cmd_contain(argc, argv, "jpeg"))
        {
            output_format = IMAGE_MJPEG;
        }

        // Initialize frame queue
        if (video_recorder_frame_queue == NULL)
        {
            ret = rtos_init_queue(&video_recorder_frame_queue, "video_recorder_frame_q",
                                  sizeof(frame_buffer_t *), VIDEO_RECORDER_FRAME_QUEUE_SIZE);
            if (ret != BK_OK)
            {
                LOGE("%s: Failed to init frame queue, ret=%d\n", __func__, ret);
                goto exit;
            }
        }

        // Audio recorder device will be initialized in Step 3
        if (audio_format == VIDEO_RECORDER_AUDIO_FORMAT_MP3)
        {
            // MP3 encoder is not available in current SDK.
            LOGE("%s: MP3 recording is not supported (no encoder)\n", __func__);
            msg = CLI_CMD_RSP_ERROR;
            goto exit;
        }

        // Step 1: Open LCD display for preview during recording
        if (video_recorder_lcd_handle == NULL)
        {
        	#if CONFIG_LV_USE_DEMO_WIDGETS
            // Reuse the same display handle as LVGL to avoid taking over the only RGB controller
            video_recorder_lcd_handle = lv_vendor_get_display_handle();
            if (video_recorder_lcd_handle == NULL)
            {
                LOGE("%s: LVGL display handle is NULL\n", __func__);
                goto exit;
            }
            dvp_display_owns_lcd = false;

            if (dvp_preview_frame != NULL)
            {
                frame_buffer_display_free(dvp_preview_frame);
                dvp_preview_frame = NULL;
                dvp_preview_rgb565_buf = NULL;
            }
            dvp_preview_buf_w = DVP_PREVIEW_MAX_W;
            dvp_preview_buf_h = DVP_PREVIEW_MAX_H;
            dvp_preview_frame = frame_buffer_display_malloc(DVP_PREVIEW_MAX_W * DVP_PREVIEW_MAX_H * 2);
			
			if (dvp_preview_frame != NULL)
            {
                dvp_preview_rgb565_buf = dvp_preview_frame->frame;
                os_memset(dvp_preview_rgb565_buf, 0, DVP_PREVIEW_MAX_W * DVP_PREVIEW_MAX_H * 2);
                lv_vendor_disp_lock();
                //lv_dvp_preview_set_buffer(dvp_preview_rgb565_buf, (uint32_t)width, (uint32_t)height);
                lv_dvp_preview_set_buffer(dvp_preview_rgb565_buf, 480, 320);
                lv_vendor_disp_unlock();
                lv_vendor_set_preview_active(1);
            }
            else
            {
                LOGE("%s: dvp_preview_rgb565_buf alloc failed\n", __func__);
            }
            LOGD("%s: Using LVGL display handle for DVP (coexist mode)\n", __func__);

			#else
        	
            // Power on LCD LDO
            ret = bk_pm_module_vote_ctrl_external_ldo(GPIO_CTRL_LDO_MODULE_LCD, LCD_LDO_PIN, GPIO_OUTPUT_STATE_HIGH);
            if (ret != AVDK_ERR_OK)
            {
                LOGE("%s: bk_pm_module_vote_ctrl_external_ldo failed, ret:%d\n", __func__, ret);
                // Clean up resources
                if (video_recorder_frame_queue != NULL)
                {
                    rtos_deinit_queue(&video_recorder_frame_queue);
                    video_recorder_frame_queue = NULL;
                }
                goto exit;
            }

			#if LCD_DEVICE_SWITCH
            // Configure LCD display
            bk_display_rgb_ctlr_config_t lcd_display_config = {0};
            lcd_display_config.lcd_device = lcd_device;
            lcd_display_config.clk_pin = GPIO_0;
            lcd_display_config.cs_pin = GPIO_12;
            lcd_display_config.sda_pin = GPIO_1;
            lcd_display_config.rst_pin = GPIO_6;

            // Create LCD display controller
            ret = bk_display_rgb_new(&video_recorder_lcd_handle, &lcd_display_config);
            if (ret != AVDK_ERR_OK)
            {
                LOGE("%s: bk_display_rgb_new failed, ret:%d\n", __func__, ret);
                bk_pm_module_vote_ctrl_external_ldo(GPIO_CTRL_LDO_MODULE_LCD, LCD_LDO_PIN, GPIO_OUTPUT_STATE_LOW);
                // Clean up resources
                if (video_recorder_frame_queue != NULL)
                {
                    rtos_deinit_queue(&video_recorder_frame_queue);
                    video_recorder_frame_queue = NULL;
                }
                goto exit;
            }
			
			#else
			bk_display_spi_ctlr_config_t spi_ctlr_config = {
			    .lcd_device = &lcd_device_st7735S,
			    .spi_id = 0,
			    .dc_pin = GPIO_17,
			    .reset_pin = GPIO_18,
			    .te_pin = 0,
			};

            // Create LCD display controller
			ret = bk_display_spi_new(&video_recorder_lcd_handle, &spi_ctlr_config);
            if (ret != AVDK_ERR_OK)
            {
                LOGE("%s: bk_display_rgb_new failed, ret:%d\n", __func__, ret);
                bk_pm_module_vote_ctrl_external_ldo(GPIO_CTRL_LDO_MODULE_LCD, LCD_LDO_PIN, GPIO_OUTPUT_STATE_LOW);
                // Clean up resources
                if (video_recorder_frame_queue != NULL)
                {
                    rtos_deinit_queue(&video_recorder_frame_queue);
                    video_recorder_frame_queue = NULL;
                }
                goto exit;
            }
			#endif 
			
            // Open backlight
            ret = lcd_backlight_open(GPIO_7);
            if (ret != AVDK_ERR_OK)
            {
                LOGE("%s: lcd_backlight_open failed, ret:%d\n", __func__, ret);
                bk_display_delete(video_recorder_lcd_handle);
                video_recorder_lcd_handle = NULL;
                bk_pm_module_vote_ctrl_external_ldo(GPIO_CTRL_LDO_MODULE_LCD, LCD_LDO_PIN, GPIO_OUTPUT_STATE_LOW);
                // Clean up resources
                if (video_recorder_frame_queue != NULL)
                {
                    rtos_deinit_queue(&video_recorder_frame_queue);
                    video_recorder_frame_queue = NULL;
                }
                goto exit;
            }

            // Open LCD display
            ret = bk_display_open(video_recorder_lcd_handle);
            if (ret != AVDK_ERR_OK)
            {
                LOGE("%s: bk_display_open failed, ret:%d\n", __func__, ret);
                lcd_backlight_close(GPIO_7);
                bk_display_delete(video_recorder_lcd_handle);
                video_recorder_lcd_handle = NULL;
                bk_pm_module_vote_ctrl_external_ldo(GPIO_CTRL_LDO_MODULE_LCD, LCD_LDO_PIN, GPIO_OUTPUT_STATE_LOW);
                // Clean up resources
                if (video_recorder_frame_queue != NULL)
                {
                    rtos_deinit_queue(&video_recorder_frame_queue);
                    video_recorder_frame_queue = NULL;
                }
                goto exit;
            }
            
            LOGD("%s: LCD display opened for recording preview\n", __func__);
			#endif 
        }

        // Step 2: Open DVP camera with both YUV (for display) and encoded format (for recording)
        if (video_recorder_dvp_handle == NULL   )
        {
        	lcd_display_thread_init();
			
            // Power on DVP camera
            LOGD("%s: Power on DVP camera\n", __func__);
            if (DVP_POWER_GPIO_ID != GPIO_INVALID_ID)
            {
                GPIO_UP(DVP_POWER_GPIO_ID);
            }

            // Configure DVP controller
            // Use combined format: IMAGE_YUV | output_format to output both YUV (for LCD display) and encoded format (for recording)
            LOGD("%s: Configure DVP controller: format=%d|IMAGE_YUV, width=%d, height=%d\n", __func__, output_format, width, height);
            bk_dvp_ctlr_config_t dvp_ctrl_config = {
                .config = BK_DVP_864X480_30FPS_MJPEG_CONFIG(),
                .cbs = &video_recorder_dvp_cbs,
            };

            dvp_ctrl_config.config.img_format = IMAGE_YUV | output_format;  // Combined format for both display and recording
            dvp_ctrl_config.config.width = width;
            dvp_ctrl_config.config.height = height;
			//dvp_ctrl_config.config.fps = 15;

            // Create DVP controller
            LOGD("%s: Creating DVP controller...\n", __func__);
			
            ret = bk_camera_dvp_ctlr_new(&video_recorder_dvp_handle, &dvp_ctrl_config);
            if (ret != AVDK_ERR_OK)
            {
                LOGE("%s: bk_camera_dvp_ctlr_new failed, ret=%d\n", __func__, ret);
                if (DVP_POWER_GPIO_ID != GPIO_INVALID_ID)
                {
                    GPIO_DOWN(DVP_POWER_GPIO_ID);
                }
                // Clean up LCD display
                if (video_recorder_lcd_handle != NULL)
                {
                    bk_display_close(video_recorder_lcd_handle);
                    lcd_backlight_close(GPIO_7);
                    bk_display_delete(video_recorder_lcd_handle);
                    video_recorder_lcd_handle = NULL;
                    bk_pm_module_vote_ctrl_external_ldo(GPIO_CTRL_LDO_MODULE_LCD, LCD_LDO_PIN, GPIO_OUTPUT_STATE_LOW);
                }
                // Clean up queue and mutexes
                if (video_recorder_frame_queue != NULL)
                {
                    rtos_deinit_queue(&video_recorder_frame_queue);
                    video_recorder_frame_queue = NULL;
                }
                goto exit;
            }

            // Open DVP camera
            LOGD("%s: Opening DVP camera...\n", __func__);
            ret = bk_camera_open(video_recorder_dvp_handle);
            if (ret != AVDK_ERR_OK)
            {
                LOGE("%s: bk_camera_open failed, ret=%d\n", __func__, ret);
                bk_camera_delete(video_recorder_dvp_handle);
                video_recorder_dvp_handle = NULL;
                if (DVP_POWER_GPIO_ID != GPIO_INVALID_ID)
                {
                    GPIO_DOWN(DVP_POWER_GPIO_ID);
                }
                // Clean up LCD display
                if (video_recorder_lcd_handle != NULL)
                {
                    bk_display_close(video_recorder_lcd_handle);
                    lcd_backlight_close(GPIO_7);
                    bk_display_delete(video_recorder_lcd_handle);
                    video_recorder_lcd_handle = NULL;
                    bk_pm_module_vote_ctrl_external_ldo(GPIO_CTRL_LDO_MODULE_LCD, LCD_LDO_PIN, GPIO_OUTPUT_STATE_LOW);
                }
                // Clean up queue and mutexes
                if (video_recorder_frame_queue != NULL)
                {
                    rtos_deinit_queue(&video_recorder_frame_queue);
                    video_recorder_frame_queue = NULL;
                }
                goto exit;
            }

            LOGD("%s: DVP camera opened for recording successfully\n", __func__);
        }
		
        // Step 3: Initialize audio recorder device
        if (video_recorder_audio_recorder_handle == NULL)
        {
            // Configure audio recorder device (mic input is always 16-bit PCM).
            audio_recorder_device_cfg_t audio_recorder_cfg = {0};
            audio_recorder_cfg.audio_channels = 1;  // Mono
            /*
             * NOTE about G722 duration:
             * Current g722 encoder element uses wideband (16kHz) mode internally. If we feed 8kHz PCM
             * while encoder is in wideband mode, the encoded stream duration becomes ~half (A/V desync).
             *
             * To keep A/V duration correct without modifying the encoder module, record G722 at 16kHz.
             */
            audio_recorder_cfg.audio_rate = (audio_format == VIDEO_RECORDER_AUDIO_FORMAT_G722) ? 16000 : 8000;
            audio_recorder_cfg.audio_bits = 16;     // 16-bit samples (encoder input)
            audio_recorder_cfg.audio_format = audio_format;

            // Initialize audio recorder device
            LOGD("%s: Initializing audio recorder device...\n", __func__);
            ret = audio_recorder_device_init(&audio_recorder_cfg, &video_recorder_audio_recorder_handle);
            if (ret != AVDK_ERR_OK)
            {
                LOGE("%s: audio_recorder_device_init failed, ret=%d\n", __func__, ret);
                // Close DVP on error
                bk_camera_close(video_recorder_dvp_handle);
                bk_camera_delete(video_recorder_dvp_handle);
                video_recorder_dvp_handle = NULL;
                if (DVP_POWER_GPIO_ID != GPIO_INVALID_ID)
                {
                    GPIO_DOWN(DVP_POWER_GPIO_ID);
                }
                // Clean up LCD display
                if (video_recorder_lcd_handle != NULL)
                {
                    bk_display_close(video_recorder_lcd_handle);
                    lcd_backlight_close(GPIO_7);
                    bk_display_delete(video_recorder_lcd_handle);
                    video_recorder_lcd_handle = NULL;
                    bk_pm_module_vote_ctrl_external_ldo(GPIO_CTRL_LDO_MODULE_LCD, LCD_LDO_PIN, GPIO_OUTPUT_STATE_LOW);
                }
                // Clean up queue
                if (video_recorder_frame_queue != NULL)
                {
                    rtos_deinit_queue(&video_recorder_frame_queue);
                    video_recorder_frame_queue = NULL;
                }
                goto exit;
            }

            // Start audio recorder device
            LOGD("%s: Starting audio recorder device...\n", __func__);
            ret = audio_recorder_device_start(video_recorder_audio_recorder_handle);
            if (ret != AVDK_ERR_OK)
            {
                LOGE("%s: audio_recorder_device_start failed, ret=%d\n", __func__, ret);
                audio_recorder_device_deinit(video_recorder_audio_recorder_handle);
                video_recorder_audio_recorder_handle = NULL;
                // Close DVP on error
                bk_camera_close(video_recorder_dvp_handle);
                bk_camera_delete(video_recorder_dvp_handle);
                video_recorder_dvp_handle = NULL;
                if (DVP_POWER_GPIO_ID != GPIO_INVALID_ID)
                {
                    GPIO_DOWN(DVP_POWER_GPIO_ID);
                }
                // Clean up LCD display
                if (video_recorder_lcd_handle != NULL)
                {
                    bk_display_close(video_recorder_lcd_handle);
                    lcd_backlight_close(GPIO_7);
                    bk_display_delete(video_recorder_lcd_handle);
                    video_recorder_lcd_handle = NULL;
                    bk_pm_module_vote_ctrl_external_ldo(GPIO_CTRL_LDO_MODULE_LCD, LCD_LDO_PIN, GPIO_OUTPUT_STATE_LOW);
                }
                // Clean up queue
                if (video_recorder_frame_queue != NULL)
                {
                    rtos_deinit_queue(&video_recorder_frame_queue);
                    video_recorder_frame_queue = NULL;
                }
                goto exit;
            }
        }

        // Step 4: Create video record instance
        bk_video_recorder_config_t video_recorder_config = {0};
        video_recorder_config.record_type = record_type;
        video_recorder_config.record_format = (output_format == IMAGE_H264) ? VIDEO_RECORDER_FORMAT_H264 : VIDEO_RECORDER_FORMAT_MJPEG;
        video_recorder_config.record_quality = 0;
        video_recorder_config.record_bitrate = 0;
        video_recorder_config.record_framerate = 30;
        video_recorder_config.video_width = width;
        video_recorder_config.video_height = height;
        video_recorder_config.audio_channels = 1;  // Mono
        // Keep AVI header sample rate consistent with recorder input rate.
        video_recorder_config.audio_rate = (audio_format == VIDEO_RECORDER_AUDIO_FORMAT_G722) ? 16000 : 8000;
        // audio_bits is a container hint (encoded bits per sample):
        // - PCM: 16
        // - AAC/MP3/G722: 0 (compressed)
        // - G711 A/U: 8
        if (audio_format == VIDEO_RECORDER_AUDIO_FORMAT_ALAW || audio_format == VIDEO_RECORDER_AUDIO_FORMAT_MULAW)
        {
            video_recorder_config.audio_bits = 8;
        }
        else if (audio_format == VIDEO_RECORDER_AUDIO_FORMAT_PCM)
        {
            video_recorder_config.audio_bits = 16;
        }
        else
        {
            video_recorder_config.audio_bits = 0;
        }
        video_recorder_config.audio_format = audio_format;
        video_recorder_config.get_frame_cb = video_recorder_get_frame_cb;
        video_recorder_config.get_audio_cb = video_recorder_get_audio_cb;
        video_recorder_config.release_frame_cb = video_recorder_release_frame_cb;
        video_recorder_config.release_audio_cb = video_recorder_release_audio_cb;
        video_recorder_config.user_data = NULL;
        LOGD("%s: Video record config prepared: type=%d, format=%d, resolution=%dx%d\n",
             __func__, record_type, video_recorder_config.record_format, width, height);

        LOGD("%s: Creating video record instance...\n", __func__);
        ret = bk_video_recorder_new(&video_recorder_handle, &video_recorder_config);
        if (ret != AVDK_ERR_OK)
        {
            LOGE("%s: bk_video_recorder_new failed, ret=%d\n", __func__, ret);
            // Clean up DVP and MIC
            audio_recorder_device_stop(video_recorder_audio_recorder_handle);
            audio_recorder_device_deinit(video_recorder_audio_recorder_handle);
            video_recorder_audio_recorder_handle = NULL;
            bk_camera_close(video_recorder_dvp_handle);
            bk_camera_delete(video_recorder_dvp_handle);
            video_recorder_dvp_handle = NULL;
            if (DVP_POWER_GPIO_ID != GPIO_INVALID_ID)
            {
                GPIO_DOWN(DVP_POWER_GPIO_ID);
            }
            // Clean up LCD display
            if (video_recorder_lcd_handle != NULL)
            {
                bk_display_close(video_recorder_lcd_handle);
                lcd_backlight_close(GPIO_7);
                bk_display_delete(video_recorder_lcd_handle);
                video_recorder_lcd_handle = NULL;
                bk_pm_module_vote_ctrl_external_ldo(GPIO_CTRL_LDO_MODULE_LCD, LCD_LDO_PIN, GPIO_OUTPUT_STATE_LOW);
            }
            goto exit;
        }

        // Step 5: Open video record
        LOGD("%s: Opening video record...\n", __func__);
        ret = bk_video_recorder_open(video_recorder_handle);
        if (ret != AVDK_ERR_OK)
        {
            LOGE("%s: bk_video_recorder_open failed, ret=%d\n", __func__, ret);
            bk_video_recorder_delete(video_recorder_handle);
            video_recorder_handle = NULL;
            // Clean up DVP and MIC
            audio_recorder_device_stop(video_recorder_audio_recorder_handle);
            audio_recorder_device_deinit(video_recorder_audio_recorder_handle);
            video_recorder_audio_recorder_handle = NULL;
            bk_camera_close(video_recorder_dvp_handle);
            bk_camera_delete(video_recorder_dvp_handle);
            video_recorder_dvp_handle = NULL;
            if (DVP_POWER_GPIO_ID != GPIO_INVALID_ID)
            {
                GPIO_DOWN(DVP_POWER_GPIO_ID);
            }
            // Clean up LCD display
            if (video_recorder_lcd_handle != NULL)
            {
                bk_display_close(video_recorder_lcd_handle);
                lcd_backlight_close(GPIO_7);
                bk_display_delete(video_recorder_lcd_handle);
                video_recorder_lcd_handle = NULL;
                bk_pm_module_vote_ctrl_external_ldo(GPIO_CTRL_LDO_MODULE_LCD, LCD_LDO_PIN, GPIO_OUTPUT_STATE_LOW);
            }
            goto exit;
        }

        // Step 6: Start recording
        LOGD("%s: Starting video recording to file: %s\n", __func__, file_path);
        ret = bk_video_recorder_start(video_recorder_handle, file_path, record_type);
        if (ret != AVDK_ERR_OK)
        {
            LOGE("%s: bk_video_recorder_start failed, ret=%d\n", __func__, ret);
            bk_video_recorder_close(video_recorder_handle);
            bk_video_recorder_delete(video_recorder_handle);
            video_recorder_handle = NULL;
            // Clean up DVP and MIC
            audio_recorder_device_stop(video_recorder_audio_recorder_handle);
            audio_recorder_device_deinit(video_recorder_audio_recorder_handle);
            video_recorder_audio_recorder_handle = NULL;
            bk_camera_close(video_recorder_dvp_handle);
            bk_camera_delete(video_recorder_dvp_handle);
            video_recorder_dvp_handle = NULL;
            if (DVP_POWER_GPIO_ID != GPIO_INVALID_ID)
            {
                GPIO_DOWN(DVP_POWER_GPIO_ID);
            }
            // Clean up LCD display
            if (video_recorder_lcd_handle != NULL)
            {
                bk_display_close(video_recorder_lcd_handle);
                lcd_backlight_close(GPIO_7);
                bk_display_delete(video_recorder_lcd_handle);
                video_recorder_lcd_handle = NULL;
                bk_pm_module_vote_ctrl_external_ldo(GPIO_CTRL_LDO_MODULE_LCD, LCD_LDO_PIN, GPIO_OUTPUT_STATE_LOW);
            }
            // Clean up queue and mutexes
            if (video_recorder_frame_queue != NULL)
            {
                rtos_deinit_queue(&video_recorder_frame_queue);
                video_recorder_frame_queue = NULL;
            }
            goto exit;
        }

        // Start max-duration auto-stop timer (5 minutes).
        // Increment session id so a previous timer callback cannot stop a new session.
        video_recorder_session_id++;
        bk_err_t t_ret = video_recorder_start_auto_stop_timer(video_recorder_session_id);
        if (t_ret != BK_OK)
        {
            LOGE("%s: Failed to start auto stop timer, ret=%d\n", __func__, t_ret);
            // Enforce time limit requirement: stop recording immediately on timer setup failure.
            (void)video_recorder_stop_internal(false);
            goto exit;
        }

        LOGD("%s: Video recording started successfully, file: %s\n", __func__, file_path);
        msg = CLI_CMD_RSP_SUCCEED;
    }
    else if (os_strcmp(argv[1], "stop") == 0)
    {
        // Check if recording is active
        if (video_recorder_handle == NULL)
        {
            LOGE("%s: video recording not started\n", __func__);
            goto exit;
        }

        // Invalidate any pending timer callback for previous sessions.
        video_recorder_session_id++;

        ret = video_recorder_stop_internal(false);
        if (ret != AVDK_ERR_OK)
        {
            goto exit;
        }

        msg = CLI_CMD_RSP_SUCCEED;
    }
    else
    {
        LOGE("%s: unknown command: %s\n", __func__, argv[1]);
        goto exit;
    }

exit:
    if (msg == NULL)
    {
        if (ret == AVDK_ERR_OK)
        {
            msg = CLI_CMD_RSP_SUCCEED;
        }
        else
        {
            msg = CLI_CMD_RSP_ERROR;
        }
    }

    os_memcpy(pcWriteBuffer, msg, os_strlen(msg));
}

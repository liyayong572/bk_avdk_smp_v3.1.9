#include "bk_private/bk_init.h"
#include <os/os.h>
#include <os/str.h>
#include "jpeg_data.h"
#include <media_service.h>
#include "frame_buffer.h"
#include "components/bk_jpeg_decode/bk_jpeg_decode_hw.h"
#include "components/bk_jpeg_decode/bk_jpeg_decode_sw.h"
#include "jpeg_decode_test.h"
#include <components/bk_display.h>
#include <stdio.h>
#include <bk_vfs.h>

#include <bk_posix.h>
#include <bk_partition.h>
#include <sys/stat.h>

#include "driver/hw_scale.h"
#include <driver/hw_scale_types.h>
#include <driver/psram.h>


#define TAG "jdec_common"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)
#define LOGV(...) BK_LOGV(TAG, ##__VA_ARGS__)

#define DECODE_SCALE_PREVIEW_W  	160
#define DECODE_SCALE_PREVIEW_H  	128

bk_err_t jpeg_decode_in_complete(frame_buffer_t *in_frame)
{
    frame_buffer_encode_free(in_frame);
    return BK_OK;
}

frame_buffer_t *jpeg_decode_out_malloc(uint32_t size)
{
    return frame_buffer_display_malloc(size);
}

avdk_err_t display_frame_free_cb(void *frame)
{
    frame_buffer_display_free((frame_buffer_t *)frame);
    return AVDK_ERR_OK;
}


extern bk_display_ctlr_handle_t jpg_display_lcd_handle;

// 2. HW Scale 变量 (用于 1280x720 缩放到 480x320)
static beken_semaphore_t g_scale0_sem = NULL;
frame_buffer_t *small_frame = NULL;
//static int key_capture = 0;

// ==========================================================
// [模块 A]：HW Scale 硬件拉伸引擎
// ==========================================================
static void app_scale0_callback(void *param)
{
	bk_err_t ret = BK_OK;
	
    if (g_scale0_sem != NULL) {
        rtos_set_semaphore(&g_scale0_sem); // 释放信号量，唤醒主线程
    }

	ret = bk_display_flush(jpg_display_lcd_handle, /*out_frame*/small_frame, display_frame_free_cb);
	
    if (ret != BK_OK) {
        frame_buffer_display_free(small_frame); // 队列满则释放
    }
    
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

							  
bk_err_t storage_yuv_to_sdcard(uint8_t *paddr, uint32_t total_len)
{
  int fd = -1;
  int written = 0;
  char file_name[50] = {0};

  //sprintf(file_name, "%d:/%s", DISK_NUMBER_SDIO_SD, filename);
  snprintf(file_name, sizeof(file_name), "/sd0/yuvshot_%lu.yuv", 0);

  fd = open(file_name, (O_RDWR | O_CREAT ));
  
  if (fd < 0)
  {
	  LOGE("can not open file: %s\n", file_name);
	  return BK_FAIL;
  }

  LOGI("open file:%s!\n", file_name);

  written = write(fd, paddr, total_len);
  
  if (written < 0) 
  {
	  LOGE("Write error: %d\n", written);
  } 
  else if (written < total_len) 
  {
	  LOGE("Disk full or partial write: %d/%u\n", written, total_len);
  }
  else 
  {
	  LOGI("Write successful!\n");
  }
  
  close(fd);

  return BK_OK;
}



bk_err_t jpeg_decode_out_complete(uint32_t format_type, uint32_t result, frame_buffer_t *out_frame)
{
    if (result == BK_OK)
    {
        LOGD("%s, %d, jpeg decode success! format_type: %d, out_frame: %p\n", __func__, __LINE__, format_type, out_frame);
    }
    else
    {
        LOGE("%s, %d, jpeg decode failed! format_type: %d, result: %d, out_frame: %p\n", __func__, __LINE__, format_type, result, out_frame);
    }
    frame_buffer_display_free(out_frame);

    return BK_OK;
}

// Helper function: Allocate and fill input frame buffer
static frame_buffer_t *allocate_input_frame(uint32_t jpeg_length, const uint8_t *jpeg_data)
{
    frame_buffer_t *in_frame = frame_buffer_encode_malloc(jpeg_length);
    if (in_frame != NULL) {
        in_frame->length = jpeg_length;
        in_frame->size = jpeg_length;
        os_memcpy(in_frame->frame, jpeg_data, jpeg_length);
    }
    return in_frame;
}

// Helper function: Get image info and log
static bk_err_t get_and_log_image_info(void *jpeg_decode_handle, frame_buffer_t *in_frame, 
                                       bk_jpeg_decode_img_info_t *img_info, jpeg_decode_test_type_t test_type)
{
    bk_err_t ret = BK_OK;

    img_info->frame = in_frame;
    if (test_type == JPEG_DECODE_MODE_HARDWARE) {
        ret = bk_jpeg_decode_hw_get_img_info((bk_jpeg_decode_hw_handle_t)jpeg_decode_handle, img_info);
    } else {
        ret = bk_jpeg_decode_sw_get_img_info((bk_jpeg_decode_sw_handle_t)jpeg_decode_handle, img_info);
    }

    if (ret != BK_OK) {
        LOGE("%s, %d, jpeg decode get img info failed! ret: %d\n", __func__, __LINE__, ret);
    } else {
        LOGD("%s, %d, image info: %dx%d, format: %d\n", __func__, __LINE__,
             img_info->width, img_info->height, img_info->format);
    }

    return ret;
}

// Helper function: Create and open decoder
bk_err_t create_and_open_decoder(void **jpeg_decode_handle, void *jpeg_decode_config, jpeg_decode_test_type_t jpeg_decode_test_type)
{
    bk_err_t ret = BK_OK;

    // Ensure decoder is released
    if (*jpeg_decode_handle != NULL) {
        close_and_delete_decoder(jpeg_decode_handle, jpeg_decode_test_type);
        *jpeg_decode_handle = NULL;
    }

    // Create decoder
    if (jpeg_decode_test_type == JPEG_DECODE_MODE_HARDWARE) {
        ret = bk_hardware_jpeg_decode_new((bk_jpeg_decode_hw_handle_t *)jpeg_decode_handle, (bk_jpeg_decode_hw_config_t *)jpeg_decode_config);
    } else if (jpeg_decode_test_type == JPEG_DECODE_MODE_SOFTWARE) {
        ret = bk_software_jpeg_decode_new((bk_jpeg_decode_sw_handle_t *)jpeg_decode_handle, (bk_jpeg_decode_sw_config_t *)jpeg_decode_config);
    } else if (jpeg_decode_test_type == JPEG_DECODE_MODE_SOFTWARE_DTCM_CP1) {
        ((bk_jpeg_decode_sw_config_t *)jpeg_decode_config)->core_id = JPEG_DECODE_CORE_ID_1;
        ret = bk_software_jpeg_decode_on_multi_core_new((bk_jpeg_decode_sw_handle_t *)jpeg_decode_handle, (bk_jpeg_decode_sw_config_t *)jpeg_decode_config);
    } else if (jpeg_decode_test_type == JPEG_DECODE_MODE_SOFTWARE_DTCM_CP2) {
        ((bk_jpeg_decode_sw_config_t *)jpeg_decode_config)->core_id = JPEG_DECODE_CORE_ID_2;
        ret = bk_software_jpeg_decode_on_multi_core_new((bk_jpeg_decode_sw_handle_t *)jpeg_decode_handle, (bk_jpeg_decode_sw_config_t *)jpeg_decode_config);
    } else if (jpeg_decode_test_type == JPEG_DECODE_MODE_SOFTWARE_DTCM_CP1_CP2) {
        ((bk_jpeg_decode_sw_config_t *)jpeg_decode_config)->core_id = JPEG_DECODE_CORE_ID_1 | JPEG_DECODE_CORE_ID_2;
        ret = bk_software_jpeg_decode_on_multi_core_new((bk_jpeg_decode_sw_handle_t *)jpeg_decode_handle, (bk_jpeg_decode_sw_config_t *)jpeg_decode_config);
    }

    if (ret != BK_OK) {
        LOGE("%s, %d, jpeg decode new failed! ret: %d\n", __func__, __LINE__, ret);
        goto exit;
    }
    LOGD("%s, %d, jpeg decode new success!\n", __func__, __LINE__);

    // Open decoder
    if (jpeg_decode_test_type == JPEG_DECODE_MODE_HARDWARE) {
        ret = bk_jpeg_decode_hw_open((bk_jpeg_decode_hw_handle_t)*jpeg_decode_handle);
    } else {
        ret = bk_jpeg_decode_sw_open((bk_jpeg_decode_sw_handle_t)*jpeg_decode_handle);
    }

    if (ret != BK_OK) {
        LOGE("%s, %d, jpeg decode open failed! ret: %d\n", __func__, __LINE__, ret);
        goto cleanup_jpeg_handle;
    }

    LOGD("%s, %d, jpeg decode open success!\n", __func__, __LINE__);

    return ret;

cleanup_jpeg_handle:
    if (jpeg_decode_test_type == JPEG_DECODE_MODE_HARDWARE) {
        bk_jpeg_decode_hw_delete((bk_jpeg_decode_hw_handle_t)*jpeg_decode_handle);
    } else {
        bk_jpeg_decode_sw_delete((bk_jpeg_decode_sw_handle_t)*jpeg_decode_handle);
    }
    *jpeg_decode_handle = NULL;

exit:
    return ret;
}

// Helper function: Perform software JPEG asynchronous decoding test
bk_err_t perform_jpeg_decode_sw_async_test(void *jpeg_decode_handle, uint32_t jpeg_length, const uint8_t *jpeg_data, const char *test_name, jpeg_decode_test_type_t jpeg_decode_test_type)
{
    bk_err_t ret = BK_OK;
    frame_buffer_t *in_frame = NULL;
    bk_jpeg_decode_img_info_t img_info = {0};

    LOGI("%s, %d, Start %s!\n", __func__, __LINE__, test_name);

    // 1. Allocate and fill input buffer
    in_frame = allocate_input_frame(jpeg_length, jpeg_data);
    if (in_frame == NULL) {
        LOGE("%s, %d, allocate_input_frame failed!\n", __func__, __LINE__);
        ret = BK_FAIL;
        goto exit;
    }

    // 2. Get image dimensions information
    ret = get_and_log_image_info(jpeg_decode_handle, in_frame, &img_info, jpeg_decode_test_type);
    if (ret != BK_OK) {
        goto exit;
    }

    // 3. Configure output format
    bk_jpeg_decode_sw_out_frame_info_t jpeg_decode_sw_config = {0};
    jpeg_decode_sw_config.out_format = JPEG_DECODE_SW_OUT_FORMAT_YUYV;
    jpeg_decode_sw_config.byte_order = JPEG_DECODE_LITTLE_ENDIAN;
    bk_jpeg_decode_sw_set_config((bk_jpeg_decode_sw_handle_t)jpeg_decode_handle, &jpeg_decode_sw_config);

    // 4. Perform asynchronous decoding
    // For async decoding, the output buffer will be allocated and returned in the callback
    ret = bk_jpeg_decode_sw_decode_async((bk_jpeg_decode_sw_handle_t)jpeg_decode_handle, in_frame);
    if (ret != BK_OK) {
        LOGE("%s, %d, jpeg sw async decode start failed! ret: %d\n", __func__, __LINE__, ret);
        goto exit;
    }

    LOGD("%s, %d, jpeg sw async decode started successfully\n", __func__, __LINE__);
    return ret;

exit:
    // Note: For async decoding, the input frame will be released in the callback function
    // This exit path is only for error cases before the decode_async call
    LOGE("%s, %d, jpeg sw async decode failed! ret: %d\n", __func__, __LINE__, ret);

    if (in_frame != NULL) {
        frame_buffer_encode_free(in_frame);
    }

    return ret;
}

// Helper function: Perform multiple software JPEG asynchronous decoding tests (burst mode)
bk_err_t perform_jpeg_decode_sw_async_burst_test(void *jpeg_decode_handle, uint32_t jpeg_length, const uint8_t *jpeg_data, 
                                               const char *test_name, jpeg_decode_test_type_t jpeg_decode_test_type, uint32_t burst_count)
{
    bk_err_t ret = BK_OK;
    uint32_t total_time = 0;
    uint32_t i = 0;

    LOGI("%s, %d, Start %s with %d bursts!\n", __func__, __LINE__, test_name, burst_count);

    // Run multiple asynchronous decoding tests in sequence
    for (i = 0; i < burst_count; i++) {
        uint32_t start_time = 0, end_time = 0;
        LOGD("%s, %d, Burst test %d/%d\n", __func__, __LINE__, i+1, burst_count);

        beken_time_get_time(&start_time);
        ret = perform_jpeg_decode_sw_async_test(jpeg_decode_handle, jpeg_length, jpeg_data, test_name, jpeg_decode_test_type);
        beken_time_get_time(&end_time);
        
        if (ret != BK_OK) {
            LOGE("%s, %d, Burst test %d/%d failed! ret: %d\n", __func__, __LINE__, i+1, burst_count, ret);
            break;
        }
        
        total_time += (end_time - start_time);
        // Add a small delay between bursts to avoid overwhelming the system
        rtos_delay_milliseconds(10);
    }

    if (ret == BK_OK) {
        LOGI("%s, %d, JPEG sw async burst test completed! Total time: %d ms, Average time: %d ms\n", 
             __func__, __LINE__, total_time, total_time / burst_count);
    }

    return ret;
}

// Helper function: Close and delete decoder
bk_err_t close_and_delete_decoder(void **jpeg_decode_handle, jpeg_decode_test_type_t jpeg_decode_test_type)
{
    bk_err_t ret = BK_OK;

    if (*jpeg_decode_handle != NULL) {
        if (jpeg_decode_test_type == JPEG_DECODE_MODE_HARDWARE) {
            ret = bk_jpeg_decode_hw_close((bk_jpeg_decode_hw_handle_t)*jpeg_decode_handle);
        } else {
            ret = bk_jpeg_decode_sw_close((bk_jpeg_decode_sw_handle_t)*jpeg_decode_handle);
        }

        if (ret != BK_OK) {
            LOGE("%s, %d, jpeg decode close failed! ret: %d\n", __func__, __LINE__, ret);
        } else {
            LOGD("%s, %d, jpeg decode close success!\n", __func__, __LINE__);
        }

        if (jpeg_decode_test_type == JPEG_DECODE_MODE_HARDWARE) {
            ret = bk_jpeg_decode_hw_delete((bk_jpeg_decode_hw_handle_t)*jpeg_decode_handle);
        } else {
            ret = bk_jpeg_decode_sw_delete((bk_jpeg_decode_sw_handle_t)*jpeg_decode_handle);
        }

        *jpeg_decode_handle = NULL;
        if (ret != BK_OK) {
            LOGE("%s, %d, jpeg decode delete failed! ret: %d\n", __func__, __LINE__, ret);
        } else {
            LOGD("%s, %d, jpeg decode delete success!\n", __func__, __LINE__);
        }
    }

    return ret;
}

// Helper function: Perform JPEG decoding test
extern bk_display_ctlr_handle_t jpg_display_lcd_handle;

bk_err_t perform_jpeg_decode_test(void *jpeg_decode_handle, uint32_t jpeg_length, const uint8_t *jpeg_data, const char *test_name, jpeg_decode_test_type_t jpeg_decode_test_type)
{
    bk_err_t ret = BK_OK;
    frame_buffer_t *in_frame = NULL;
    frame_buffer_t *out_frame = NULL;
    bk_jpeg_decode_img_info_t img_info = {0};

    LOGI("%s, %d, Start %s!\n", __func__, __LINE__, test_name);

    // 1. Allocate and fill input buffer
    in_frame = allocate_input_frame(jpeg_length, jpeg_data);
    if (in_frame == NULL) {
        LOGE("%s, %d, allocate_input_frame failed!\n", __func__, __LINE__);
        ret = BK_FAIL;
        goto exit;
    }

    // 2. Get image dimensions information
    ret = get_and_log_image_info(jpeg_decode_handle, in_frame, &img_info, jpeg_decode_test_type);
    if (ret != BK_OK) {
        goto exit;
    }

    // 3. Allocate output buffer
    out_frame = frame_buffer_display_malloc(img_info.width * img_info.height * 2);
    if (out_frame == NULL) {
        LOGE("%s, %d, frame_buffer_display_malloc failed!\n", __func__, __LINE__);
        ret = BK_FAIL;
        goto exit;
    }
    out_frame->width = img_info.width;
    out_frame->height = img_info.height;
    out_frame->fmt = PIXEL_FMT_YUYV;

	printf("out_frame->width =%d,out_frame->height  =%d\n",out_frame->width,out_frame->height);
	
    // For software decoder, can set rotation angle, output format and byte order
    if (jpeg_decode_test_type != JPEG_DECODE_MODE_HARDWARE) {

        // Set output format
        bk_jpeg_decode_sw_out_frame_info_t jpeg_decode_sw_config = {0};
        jpeg_decode_sw_config.out_format = JPEG_DECODE_SW_OUT_FORMAT_YUYV;
        jpeg_decode_sw_config.byte_order = JPEG_DECODE_LITTLE_ENDIAN;
        bk_jpeg_decode_sw_set_config((bk_jpeg_decode_sw_handle_t)jpeg_decode_handle, &jpeg_decode_sw_config);
    }

    // 4. Perform decoding and measure time
    uint32_t start_time = 0, end_time = 0;
    beken_time_get_time(&start_time);
    if (jpeg_decode_test_type == JPEG_DECODE_MODE_HARDWARE) {
        ret = bk_jpeg_decode_hw_decode((bk_jpeg_decode_hw_handle_t)jpeg_decode_handle, in_frame, out_frame);
    } else {
        ret = bk_jpeg_decode_sw_decode((bk_jpeg_decode_sw_handle_t)jpeg_decode_handle, in_frame, out_frame);
    }
    beken_time_get_time(&end_time);

	#if 0
	small_frame = frame_buffer_display_malloc(DECODE_SCALE_PREVIEW_W * DECODE_SCALE_PREVIEW_H * 2);
			
	if (small_frame != NULL) {

		small_frame->length = DECODE_SCALE_PREVIEW_W * DECODE_SCALE_PREVIEW_H * 2;
        // 2. 硬件缩放：直接把画面缩放到新申请的 small_frame->frame 里
        video_hw_scale_frame(out_frame->frame, 480, 320, small_frame->frame, DECODE_SCALE_PREVIEW_W, DECODE_SCALE_PREVIEW_H);
        //video_hw_scale_frame(out_frame->frame, 864, 480, small_frame->frame, DECODE_SCALE_PREVIEW_W, DECODE_SCALE_PREVIEW_H);

		frame_buffer_display_free(out_frame);
	
        small_frame->width = DECODE_SCALE_PREVIEW_W;
        small_frame->height = DECODE_SCALE_PREVIEW_H;
        small_frame->fmt = PIXEL_FMT_YUYV;


		storage_yuv_to_sdcard(small_frame->frame,small_frame->length);
		//printf("small_frame....\n");
		/*
		if(key_capture )
		{
			key_capture = 0;
			video_rec_yuv_frame(frame,0);
		}
		*/
		/*
        // 3. 把这个新诞生的小图，推给录像/显示队列
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
	#else 
	
	
	out_frame->fmt = PIXEL_FMT_YUYV;
    ret = bk_display_flush(jpg_display_lcd_handle, out_frame, display_frame_free_cb);
	
    if (ret != BK_OK) {
        LOGE("%s, %d, jpeg decode start failed! ret: %d\n", __func__, __LINE__, ret);
    }
	#endif
    LOGD("%s, %d, jpeg decode success! Decode time: %d ms\n", __func__, __LINE__, end_time - start_time);
    return ret;

    // 5. Release resources
exit:
    if (in_frame != NULL) {
        frame_buffer_encode_free(in_frame);
    }
    if (out_frame != NULL) {
        frame_buffer_display_free(out_frame);
    }

    return ret;
}

// Helper function: Perform JPEG asynchronous decoding test
bk_err_t perform_jpeg_decode_async_test(void *jpeg_decode_handle, uint32_t jpeg_length, const uint8_t *jpeg_data, const char *test_name, jpeg_decode_test_type_t jpeg_decode_test_type)
{
    bk_err_t ret = BK_OK;
    frame_buffer_t *in_frame = NULL;
    bk_jpeg_decode_img_info_t img_info = {0};

    LOGI("%s, %d, Start %s!\n", __func__, __LINE__, test_name);

    // 1. Allocate and fill input buffer
    in_frame = allocate_input_frame(jpeg_length, jpeg_data);
    if (in_frame == NULL) {
        LOGE("%s, %d, allocate_input_frame failed!\n", __func__, __LINE__);
        ret = BK_FAIL;
        goto exit;
    }

    // 2. Get image dimensions information
    ret = get_and_log_image_info(jpeg_decode_handle, in_frame, &img_info, jpeg_decode_test_type);
    if (ret != BK_OK) {
        goto exit;
    }

    // 3. Perform asynchronous decoding
    // For async decoding, the output buffer will be allocated and returned in the callback
    ret = bk_jpeg_decode_hw_decode_async((bk_jpeg_decode_hw_handle_t)jpeg_decode_handle, in_frame);
    if (ret != BK_OK) {
        LOGE("%s, %d, jpeg async decode start failed! ret: %d\n", __func__, __LINE__, ret);
        goto exit;
    }

    LOGD("%s, %d, jpeg async decode started successfully\n", __func__, __LINE__);
    return ret;

exit:
    // Note: For async decoding, the input frame will be released in the callback function
    // This exit path is only for error cases before the decode_async call
    LOGE("%s, %d, jpeg async decode failed! ret: %d\n", __func__, __LINE__, ret);

    if (in_frame != NULL) {
        frame_buffer_encode_free(in_frame);
    }

    return ret;
}

// Helper function: Perform multiple JPEG asynchronous decoding tests (burst mode)
bk_err_t perform_jpeg_decode_async_burst_test(void *jpeg_decode_handle, uint32_t jpeg_length, const uint8_t *jpeg_data, 
                                           const char *test_name, uint32_t burst_count)
{
    bk_err_t ret = BK_OK;
    uint32_t total_time = 0;
    uint32_t i = 0;

    LOGI("%s, %d, Start %s with %d bursts!\n", __func__, __LINE__, test_name, burst_count);

    // Run multiple asynchronous decoding tests in sequence
    for (i = 0; i < burst_count; i++) {
        uint32_t start_time = 0, end_time = 0;
        LOGD("%s, %d, Burst test %d/%d\n", __func__, __LINE__, i+1, burst_count);

        beken_time_get_time(&start_time);
        ret = perform_jpeg_decode_async_test(jpeg_decode_handle, jpeg_length, jpeg_data, test_name, JPEG_DECODE_MODE_HARDWARE);
        beken_time_get_time(&end_time);
        
        if (ret != BK_OK) {
            LOGE("%s, %d, Burst test %d/%d failed! ret: %d\n", __func__, __LINE__, i+1, burst_count, ret);
            break;
        }
        
        total_time += (end_time - start_time);
    }

    if (ret == BK_OK) {
        LOGI("%s, %d, JPEG async burst test completed! Total time: %d ms, Average time: %d ms\n", 
             __func__, __LINE__, total_time, total_time / burst_count);
    }

    return ret;
}

// Helper function: Create and open hardware line decoder
bk_err_t create_and_open_hw_opt_decoder(void **jpeg_decode_handle, void *jpeg_decode_config)
{
    bk_err_t ret = BK_OK;

    // Ensure decoder is released
    if (*jpeg_decode_handle != NULL) {
        close_and_delete_hw_opt_decoder(jpeg_decode_handle);
        *jpeg_decode_handle = NULL;
    }

    // Create decoder
    ret = bk_hardware_jpeg_decode_opt_new((bk_jpeg_decode_hw_handle_t *)jpeg_decode_handle, (bk_jpeg_decode_hw_opt_config_t *)jpeg_decode_config);
    if (ret != BK_OK) {
        LOGE("%s, %d, jpeg hw opt decode new failed! ret: %d\n", __func__, __LINE__, ret);
        goto exit;
    }
    LOGD("%s, %d, jpeg hw opt decode new success!\n", __func__, __LINE__);

    // Open decoder
    ret = bk_jpeg_decode_hw_open((bk_jpeg_decode_hw_handle_t)*jpeg_decode_handle);
    if (ret != BK_OK) {
        LOGE("%s, %d, jpeg hw opt decode open failed! ret: %d\n", __func__, __LINE__, ret);
        goto cleanup_jpeg_handle;
    }

    LOGD("%s, %d, jpeg hw opt decode open success!\n", __func__, __LINE__);
    return ret;

cleanup_jpeg_handle:
    bk_jpeg_decode_hw_delete((bk_jpeg_decode_hw_handle_t)*jpeg_decode_handle);
    *jpeg_decode_handle = NULL;

exit:
    return ret;
}

// Helper function: Close and delete hardware line decoder
bk_err_t close_and_delete_hw_opt_decoder(void **jpeg_decode_handle)
{
    bk_err_t ret = BK_OK;

    if (*jpeg_decode_handle != NULL) {
        ret = bk_jpeg_decode_hw_close((bk_jpeg_decode_hw_handle_t)*jpeg_decode_handle);
        if (ret != BK_OK) {
            LOGE("%s, %d, jpeg hw opt decode close failed! ret: %d\n", __func__, __LINE__, ret);
        } else {
            LOGD("%s, %d, jpeg hw opt decode close success!\n", __func__, __LINE__);
        }

        ret = bk_jpeg_decode_hw_delete((bk_jpeg_decode_hw_handle_t)*jpeg_decode_handle);
        *jpeg_decode_handle = NULL;
        if (ret != BK_OK) {
            LOGE("%s, %d, jpeg hw opt decode delete failed! ret: %d\n", __func__, __LINE__, ret);
        } else {
            LOGD("%s, %d, jpeg hw opt decode delete success!\n", __func__, __LINE__);
        }
    }

    return ret;
}

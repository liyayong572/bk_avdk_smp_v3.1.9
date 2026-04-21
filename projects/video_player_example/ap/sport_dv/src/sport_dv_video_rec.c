#include <common/bk_include.h>
#include <components/bk_display.h>
#include <components/bk_camera_ctlr.h>
#include <components/bk_video_recorder.h>
#include <components/bk_video_recorder_types.h>
#include "bk_video_recorder_ctlr.h"
#include <os/os.h>
#include <os/str.h>
#include <os/mem.h>
#include <driver/pwr_clk.h>
#include <driver/h264_types.h>
#include "frame_buffer.h"

#include "sport_dv_hw.h"
#include "sport_dv_common.h"
#include "sport_dv_display.h"
#include "sport_dv_video_rec.h"

#include "audio_recorder_device.h"

#define TAG "sport_dv_rec"
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

static bk_camera_ctlr_handle_t s_rec_dvp = NULL;
static bk_video_recorder_handle_t s_recorder = NULL;
static audio_recorder_device_handle_t s_rec_audio = NULL;
static beken_queue_t s_rec_frame_q = NULL;
static bool s_running = false;

static uint8_t s_audio_tmp[2048];

static frame_buffer_t *sport_dv_rec_dvp_malloc(image_format_t format, uint32_t size)
{
    frame_buffer_t *frame = NULL;
	
    if (format == IMAGE_YUV) {
        frame = frame_buffer_display_malloc(size);
    } else if (format == IMAGE_H264) {
        frame = frame_buffer_encode_malloc(size);
    } else {
        return NULL;
    }
    if (frame) {
        frame->sequence = 0;
        frame->length = 0;
        frame->timestamp = 0;
        frame->size = size;
    }
    return frame;
}

static void sport_dv_rec_dvp_complete(image_format_t format, frame_buffer_t *frame, int result)
{
    if (frame == NULL) {
        return;
    }

    if (result != AVDK_ERR_OK) {
        if (format == IMAGE_YUV) frame_buffer_display_free(frame);
        else frame_buffer_encode_free(frame);
        return;
    }

    if (format == IMAGE_YUV) {
        frame->fmt = PIXEL_FMT_YUYV;
        (void)sport_dv_display_push(frame);
        return;
    }

    if (format == IMAGE_H264) {
        if (s_rec_frame_q) {
            bk_err_t qret = rtos_push_to_queue(&s_rec_frame_q, &frame, 0);
            if (qret != kNoErr) {
                frame_buffer_encode_free(frame);
            }
        } else {
            frame_buffer_encode_free(frame);
        }
        return;
    }

    frame_buffer_encode_free(frame);
}

static const bk_dvp_callback_t s_dvp_cbs = {
    .malloc = sport_dv_rec_dvp_malloc,
    .complete = sport_dv_rec_dvp_complete,
};

static int sport_dv_rec_get_frame_cb(void *user_data, video_recorder_frame_data_t *frame_data)
{
    (void)user_data;
    if (frame_data == NULL || s_rec_frame_q == NULL) {
        return -1;
    }

    frame_buffer_t *frame = NULL;
    bk_err_t ret = rtos_pop_from_queue(&s_rec_frame_q, &frame, 0);
    if (ret != kNoErr || frame == NULL) {
        return -1;
    }

    if (frame->frame == NULL || frame->length == 0) {
        frame_buffer_encode_free(frame);
        return -1;
    }

    frame_data->data = frame->frame;
    frame_data->length = frame->length;
    frame_data->width = frame->width;
    frame_data->height = frame->height;
    frame_data->frame_buffer = frame;
    frame_data->is_key_frame = (frame->h264_type & (1U << H264_NAL_I_FRAME)) ||
                               (frame->h264_type & (1U << H264_NAL_IDR_SLICE));
    return 0;
}

static int sport_dv_rec_get_audio_cb(void *user_data, video_recorder_audio_data_t *audio_data)
{
    (void)user_data;
    if (audio_data == NULL || s_rec_audio == NULL) {
        return -1;
    }

    uint32_t data_len = 0;
    avdk_err_t ret = audio_recorder_device_read(s_rec_audio, s_audio_tmp, sizeof(s_audio_tmp), &data_len);
    if (ret != AVDK_ERR_OK || data_len == 0) {
        return -1;
    }

    uint8_t *buf = (uint8_t *)os_malloc(data_len);
    if (buf == NULL) {
        return -1;
    }
    os_memcpy(buf, s_audio_tmp, data_len);
    audio_data->data = buf;
    audio_data->length = data_len;
    return 0;
}

static void sport_dv_rec_release_frame_cb(void *user_data, video_recorder_frame_data_t *frame_data)
{
    (void)user_data;
    if (frame_data && frame_data->frame_buffer) {
        frame_buffer_encode_free((frame_buffer_t *)frame_data->frame_buffer);
        frame_data->frame_buffer = NULL;
    }
}

static void sport_dv_rec_release_audio_cb(void *user_data, video_recorder_audio_data_t *audio_data)
{
    (void)user_data;
    if (audio_data && audio_data->data) {
        os_free(audio_data->data);
        audio_data->data = NULL;
        audio_data->length = 0;
    }
}

static int sport_dv_rec_open_dvp(uint32_t width, uint32_t height)
{
    if (s_rec_dvp) {
        return BK_OK;
    }

    if (SPORT_DV_DVP_POWER_GPIO_ID != SPORT_DV_GPIO_INVALID_ID) {
        GPIO_UP(SPORT_DV_DVP_POWER_GPIO_ID);
    }

    bk_dvp_ctlr_config_t dvp_cfg = {
        .config = BK_DVP_864X480_30FPS_MJPEG_CONFIG(),
        .cbs = &s_dvp_cbs,
    };
    dvp_cfg.config.img_format = IMAGE_YUV | IMAGE_H264;
    dvp_cfg.config.width = width;
    dvp_cfg.config.height = height;

    avdk_err_t ret = bk_camera_dvp_ctlr_new(&s_rec_dvp, &dvp_cfg);
    if (ret != AVDK_ERR_OK) {
        s_rec_dvp = NULL;
        if (SPORT_DV_DVP_POWER_GPIO_ID != SPORT_DV_GPIO_INVALID_ID) {
            GPIO_DOWN(SPORT_DV_DVP_POWER_GPIO_ID);
        }
        return ret;
    }

    ret = bk_camera_open(s_rec_dvp);
    if (ret != AVDK_ERR_OK) {
        bk_camera_delete(s_rec_dvp);
        s_rec_dvp = NULL;
        if (SPORT_DV_DVP_POWER_GPIO_ID != SPORT_DV_GPIO_INVALID_ID) {
            GPIO_DOWN(SPORT_DV_DVP_POWER_GPIO_ID);
        }
        return ret;
    }

    return BK_OK;
}

static void sport_dv_rec_close_dvp(void)
{
    if (!s_rec_dvp) {
        return;
    }
    bk_camera_close(s_rec_dvp);
    bk_camera_delete(s_rec_dvp);
    s_rec_dvp = NULL;
    if (SPORT_DV_DVP_POWER_GPIO_ID != SPORT_DV_GPIO_INVALID_ID) {
        GPIO_DOWN(SPORT_DV_DVP_POWER_GPIO_ID);
    }
}

static void sport_dv_rec_clear_frame_q(void)
{
    if (s_rec_frame_q == NULL) {
        return;
    }
    frame_buffer_t *frame = NULL;
    while (rtos_pop_from_queue(&s_rec_frame_q, &frame, 0) == kNoErr && frame != NULL) {
        frame_buffer_encode_free(frame);
    }
}

int sport_dv_video_rec_start(const char *file_path, uint32_t width, uint32_t height)
{
    if (s_running) {
        return BK_OK;
    }
    if (file_path == NULL || file_path[0] == '\0') {
        return BK_FAIL;
    }

    int ret = sport_dv_sd_mount();
    if (ret != BK_OK) {
        return ret;
    }

    if (s_rec_frame_q == NULL) {
        ret = rtos_init_queue(&s_rec_frame_q, "dv_rec_q", sizeof(frame_buffer_t *), 8);
        if (ret != kNoErr) {
            s_rec_frame_q = NULL;
            return BK_FAIL;
        }
    }

    ret = sport_dv_display_start();
    if (ret != AVDK_ERR_OK) {
        return ret;
    }

    ret = sport_dv_rec_open_dvp(width, height);
    if (ret != BK_OK) {
        (void)sport_dv_display_stop();
        return ret;
    }

    audio_recorder_device_cfg_t acfg = {0};
    acfg.audio_channels = 1;
    acfg.audio_rate = 8000;
    acfg.audio_bits = 16;
    acfg.audio_format = VIDEO_RECORDER_AUDIO_FORMAT_PCM;

    if (audio_recorder_device_init(&acfg, &s_rec_audio) != AVDK_ERR_OK) {
        sport_dv_rec_close_dvp();
        (void)sport_dv_display_stop();
        return BK_FAIL;
    }
    if (audio_recorder_device_start(s_rec_audio) != AVDK_ERR_OK) {
        audio_recorder_device_deinit(s_rec_audio);
        s_rec_audio = NULL;
        sport_dv_rec_close_dvp();
        (void)sport_dv_display_stop();
        return BK_FAIL;
    }

    bk_video_recorder_config_t cfg = {0};
    cfg.record_type = VIDEO_RECORDER_TYPE_MP4;
    cfg.record_format = VIDEO_RECORDER_FORMAT_H264;
    cfg.record_quality = 0;
    cfg.record_bitrate = 0;
    cfg.record_framerate = 30;
    cfg.video_width = width;
    cfg.video_height = height;
    cfg.audio_channels = 1;
    cfg.audio_rate = 8000;
    cfg.audio_bits = 16;
    cfg.audio_format = VIDEO_RECORDER_AUDIO_FORMAT_PCM;
    cfg.get_frame_cb = sport_dv_rec_get_frame_cb;
    cfg.get_audio_cb = sport_dv_rec_get_audio_cb;
    cfg.release_frame_cb = sport_dv_rec_release_frame_cb;
    cfg.release_audio_cb = sport_dv_rec_release_audio_cb;
    cfg.user_data = NULL;

    if (bk_video_recorder_new(&s_recorder, &cfg) != AVDK_ERR_OK) {
        audio_recorder_device_stop(s_rec_audio);
        audio_recorder_device_deinit(s_rec_audio);
        s_rec_audio = NULL;
        sport_dv_rec_close_dvp();
        (void)sport_dv_display_stop();
        sport_dv_rec_clear_frame_q();
        return BK_FAIL;
    }
    if (bk_video_recorder_open(s_recorder) != AVDK_ERR_OK) {
        bk_video_recorder_delete(s_recorder);
        s_recorder = NULL;
        audio_recorder_device_stop(s_rec_audio);
        audio_recorder_device_deinit(s_rec_audio);
        s_rec_audio = NULL;
        sport_dv_rec_close_dvp();
        (void)sport_dv_display_stop();
        sport_dv_rec_clear_frame_q();
        return BK_FAIL;
    }

    if (bk_video_recorder_start(s_recorder, (char *)file_path, VIDEO_RECORDER_TYPE_MP4) != AVDK_ERR_OK) {
        bk_video_recorder_close(s_recorder);
        bk_video_recorder_delete(s_recorder);
        s_recorder = NULL;
        audio_recorder_device_stop(s_rec_audio);
        audio_recorder_device_deinit(s_rec_audio);
        s_rec_audio = NULL;
        sport_dv_rec_close_dvp();
        (void)sport_dv_display_stop();
        sport_dv_rec_clear_frame_q();
        return BK_FAIL;
    }

    s_running = true;
    return BK_OK;
}

int sport_dv_video_rec_stop(void)
{
    if (!s_running) {
        return BK_OK;
    }

    if (s_recorder) {
        bk_video_recorder_stop(s_recorder);
        bk_video_recorder_close(s_recorder);
        bk_video_recorder_delete(s_recorder);
        s_recorder = NULL;
    }

    if (s_rec_audio) {
        audio_recorder_device_stop(s_rec_audio);
        audio_recorder_device_deinit(s_rec_audio);
        s_rec_audio = NULL;
    }

    sport_dv_rec_clear_frame_q();

    sport_dv_rec_close_dvp();
    (void)sport_dv_display_stop();

    if (s_rec_frame_q) {
        rtos_deinit_queue(&s_rec_frame_q);
        s_rec_frame_q = NULL;
    }

    s_running = false;
    return BK_OK;
}

bool sport_dv_video_rec_is_running(void)
{
    return s_running;
}

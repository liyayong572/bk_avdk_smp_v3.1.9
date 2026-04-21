#include <common/bk_include.h>
#include <components/bk_camera_ctlr.h>
#include <os/os.h>
#include <os/str.h>
#include <os/mem.h>
#include <bk_vfs.h>
#include <bk_filesystem.h>
#include <fcntl.h>
#include "frame_buffer.h"

#include "sport_dv_cfg.h"
#include "sport_dv_hw.h"
#include "sport_dv_common.h"
#include "sport_dv_display.h"
#include "sport_dv_preview.h"

#define TAG "sport_dv_preview"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)

static bk_camera_ctlr_handle_t s_preview_dvp = NULL;
static bool s_running = false;

static bool s_snapshot_req = false;
static char s_snapshot_path[96];


static frame_buffer_t *sport_dv_dvp_malloc(image_format_t format, uint32_t size)
{
	frame_buffer_t *frame = NULL;

    // For recording, we prefer encoded formats (MJPEG/H264) to reduce file size
    if (format == IMAGE_YUV)
    {
        frame = frame_buffer_display_malloc(size);
    }
	else if (format == IMAGE_MJPEG || format == IMAGE_H264 || format == IMAGE_H265)
	{
		 frame = frame_buffer_encode_malloc(size);
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

static int sport_dv_write_file(const char *path, const uint8_t *data, uint32_t len)
{
    if (path == NULL || path[0] == '\0' || data == NULL || len == 0) {
        return BK_FAIL;
    }
    int fd = bk_vfs_open(path, (O_RDWR | O_CREAT));
    if (fd < 0) {
        return BK_FAIL;
    }
    int written = bk_vfs_write(fd, data, len);
    bk_vfs_close(fd);
    return (written == (int)len) ? BK_OK : BK_FAIL;
}

static void sport_dv_dvp_complete(image_format_t format, frame_buffer_t *frame, int result)
{
    if (frame == NULL) {
        return;
    }

	if (result == AVDK_ERR_OK && frame->frame != NULL && frame->length > 0)
    {
        if (format == IMAGE_YUV)
        {
            frame->fmt = PIXEL_FMT_YUYV;
            (void)sport_dv_display_push(frame);
        }
        else if (format == IMAGE_MJPEG || format == IMAGE_H264 || format == IMAGE_H265)
        {
        	if (format == IMAGE_MJPEG) {
		        bool do_snap = s_snapshot_req;
		        if (do_snap) {
		            s_snapshot_req = false;
		            (void)sport_dv_write_file(s_snapshot_path, frame->frame, frame->length);
		        }
		        frame_buffer_encode_free(frame);
		        return;
		    }
			else
			{
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

static const bk_dvp_callback_t s_dvp_cbs = {
    .malloc = sport_dv_dvp_malloc,
    .complete = sport_dv_dvp_complete,
};

static int sport_dv_open_dvp(uint32_t width, uint32_t height)
{
    if (s_preview_dvp) {
        return BK_OK;
    }

    if (SPORT_DV_DVP_POWER_GPIO_ID != SPORT_DV_GPIO_INVALID_ID) {
        GPIO_UP(SPORT_DV_DVP_POWER_GPIO_ID);
    }

    bk_dvp_ctlr_config_t dvp_cfg = {
        .config = BK_DVP_864X480_30FPS_MJPEG_CONFIG(),
        .cbs = &s_dvp_cbs,
    };
    dvp_cfg.config.img_format = IMAGE_YUV | IMAGE_MJPEG;
    dvp_cfg.config.width = width;
    dvp_cfg.config.height = height;

    avdk_err_t ret = bk_camera_dvp_ctlr_new(&s_preview_dvp, &dvp_cfg);
	
    if (ret != AVDK_ERR_OK) {
        s_preview_dvp = NULL;
        if (SPORT_DV_DVP_POWER_GPIO_ID != SPORT_DV_GPIO_INVALID_ID) {
            GPIO_DOWN(SPORT_DV_DVP_POWER_GPIO_ID);
        }
        return ret;
    }

    ret = bk_camera_open(s_preview_dvp);
    if (ret != AVDK_ERR_OK) {
        bk_camera_delete(s_preview_dvp);
        s_preview_dvp = NULL;
        if (SPORT_DV_DVP_POWER_GPIO_ID != SPORT_DV_GPIO_INVALID_ID) {
            GPIO_DOWN(SPORT_DV_DVP_POWER_GPIO_ID);
        }
        return ret;
    }

    return BK_OK;
}

static void sport_dv_close_dvp(void)
{
    if (!s_preview_dvp) {
        return;
    }
    bk_camera_close(s_preview_dvp);
    bk_camera_delete(s_preview_dvp);
    s_preview_dvp = NULL;
    if (SPORT_DV_DVP_POWER_GPIO_ID != SPORT_DV_GPIO_INVALID_ID) {
        GPIO_DOWN(SPORT_DV_DVP_POWER_GPIO_ID);
    }
}

int sport_dv_preview_start(uint32_t width, uint32_t height)
{
    if (s_running) {
        return BK_OK;
    }
    int ret = sport_dv_display_start();
    if (ret != AVDK_ERR_OK) {
        return ret;
    }
    ret = sport_dv_open_dvp(width, height);
    if (ret != BK_OK) {
        (void)sport_dv_display_stop();
        return ret;
    }
    s_running = true;
    return BK_OK;
}

int sport_dv_preview_stop(void)
{
    if (!s_running) {
        return BK_OK;
    }
    s_snapshot_req = false;
    sport_dv_close_dvp();
    (void)sport_dv_display_stop();
    s_running = false;
    return BK_OK;
}

int sport_dv_preview_request_snapshot(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        return BK_FAIL;
    }
    os_memset(s_snapshot_path, 0, sizeof(s_snapshot_path));
    os_strncpy(s_snapshot_path, path, sizeof(s_snapshot_path) - 1);
    s_snapshot_req = true;
    return BK_OK;
}

bool sport_dv_preview_is_running(void)
{
    return s_running;
}

#include <common/bk_include.h>
#include <os/os.h>
#include <os/str.h>
#include <os/mem.h>

#include "sport_dv.h"
#include "sport_dv_cfg.h"
#include "sport_dv_common.h"
#include "sport_dv_keys.h"
#include "sport_dv_preview.h"
#include "sport_dv_video_rec.h"

#define TAG "sport_dv"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)

typedef enum {
    SPORT_DV_MODE_PHOTO = 0,
    SPORT_DV_MODE_VIDEO,
    SPORT_DV_MODE_AUDIO,
    SPORT_DV_MODE_PLAYBACK,
    SPORT_DV_MODE_WIFI,
    SPORT_DV_MODE_MAX,
} sport_dv_mode_t;

static bool s_started = false;
static sport_dv_mode_t s_mode = SPORT_DV_MODE_PHOTO;

static void sport_dv_set_mode(sport_dv_mode_t mode)
{
    if (mode >= SPORT_DV_MODE_MAX) {
        return;
    }

    if (sport_dv_video_rec_is_running()) {
        (void)sport_dv_video_rec_stop();
    }

    s_mode = mode;

    if (mode == SPORT_DV_MODE_PHOTO) {
        (void)sport_dv_preview_start(SPORT_DV_PREVIEW_WIDTH, SPORT_DV_PREVIEW_HEIGHT);
    } else if (mode == SPORT_DV_MODE_VIDEO) {
        (void)sport_dv_preview_start(SPORT_DV_PREVIEW_WIDTH, SPORT_DV_PREVIEW_HEIGHT);
    } else {
        (void)sport_dv_preview_stop();
    }
}

static void sport_dv_mode_next(void)
{
    sport_dv_mode_t next = (sport_dv_mode_t)((s_mode + 1) % SPORT_DV_MODE_MAX);
    sport_dv_set_mode(next);
}

static void sport_dv_take_photo(void)
{
    if (sport_dv_sd_mount() != BK_OK) {
        return;
    }

    (void)sport_dv_preview_start(SPORT_DV_PREVIEW_WIDTH, SPORT_DV_PREVIEW_HEIGHT);

    uint32_t t = (uint32_t)rtos_get_time();
    char path[64];
    os_memset(path, 0, sizeof(path));
    os_snprintf(path, sizeof(path), "/sd0/photo_%lu.jpg", (unsigned long)t);
    (void)sport_dv_preview_request_snapshot(path);
}

static void sport_dv_toggle_video_record(void)
{
    if (sport_dv_video_rec_is_running()) {
        (void)sport_dv_video_rec_stop();
        (void)sport_dv_preview_start(SPORT_DV_PREVIEW_WIDTH, SPORT_DV_PREVIEW_HEIGHT);
        return;
    }

    if (sport_dv_sd_mount() != BK_OK) {
        return;
    }

    (void)sport_dv_preview_stop();

    uint32_t t = (uint32_t)rtos_get_time();
    char path[64];
    os_memset(path, 0, sizeof(path));
    os_snprintf(path, sizeof(path), "/sd0/video_%lu.mp4", (unsigned long)t);
    (void)sport_dv_video_rec_start(path, SPORT_DV_PREVIEW_WIDTH, SPORT_DV_PREVIEW_HEIGHT);
}

static void sport_dv_on_key(uint8_t event)
{
    if (!s_started) {
        return;
    }

    if (event == SPORT_DV_KEY_EVENT_MODE) {
        sport_dv_mode_next();
        return;
    }

    if (event == SPORT_DV_KEY_EVENT_OK) {
        if (s_mode == SPORT_DV_MODE_PHOTO) {
            sport_dv_take_photo();
        } else if (s_mode == SPORT_DV_MODE_VIDEO) {
            sport_dv_toggle_video_record();
        }
        return;
    }
}

int sport_dv_start(void)
{
    if (s_started) {
        return BK_OK;
    }
    s_mode = SPORT_DV_MODE_PHOTO;
    (void)sport_dv_keys_init(sport_dv_on_key);
    (void)sport_dv_preview_start(SPORT_DV_PREVIEW_WIDTH, SPORT_DV_PREVIEW_HEIGHT);
    s_started = true;
    LOGI("started\n");
    return BK_OK;
}

int sport_dv_stop(void)
{
    if (!s_started) {
        return BK_OK;
    }
    (void)sport_dv_video_rec_stop();
    (void)sport_dv_preview_stop();
    (void)sport_dv_keys_deinit();
    (void)sport_dv_sd_unmount();
    s_started = false;
    return BK_OK;
}

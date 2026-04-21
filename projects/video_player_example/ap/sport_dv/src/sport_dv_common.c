#include <common/bk_include.h>
#include <components/avdk_utils/avdk_error.h>
#include <os/os.h>
#include <driver/gpio.h>
#include <bk_partition.h>
#include <bk_vfs.h>
#include <bk_filesystem.h>
#include "gpio_driver.h"
#include "frame_buffer.h"
#include "sport_dv_common.h"

#define TAG "sport_dv_common"
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

avdk_err_t sport_dv_lcd_backlight_open(uint8_t bl_gpio)
{
    gpio_dev_unmap(bl_gpio);
    avdk_err_t ret = bk_gpio_enable_output(bl_gpio);
    if (ret != AVDK_ERR_OK) {
        return ret;
    }
    ret = bk_gpio_pull_up(bl_gpio);
    if (ret != AVDK_ERR_OK) {
        return ret;
    }
    bk_gpio_set_output_high(bl_gpio);
    return AVDK_ERR_OK;
}

avdk_err_t sport_dv_lcd_backlight_close(uint8_t bl_gpio)
{
    avdk_err_t ret = bk_gpio_pull_down(bl_gpio);
    if (ret != AVDK_ERR_OK) {
        return ret;
    }
    bk_gpio_set_output_low(bl_gpio);
    return AVDK_ERR_OK;
}

avdk_err_t sport_dv_display_frame_free_cb(void *frame)
{
    frame_buffer_display_free((frame_buffer_t *)frame);
    return AVDK_ERR_OK;
}

static bool s_sd_mounted = false;

int sport_dv_sd_mount(void)
{
    if (s_sd_mounted) {
        return BK_OK;
    }

    struct bk_fatfs_partition partition;
    partition.part_type = FATFS_DEVICE;
    partition.part_dev.device_name = FATFS_DEV_SDCARD;
    partition.mount_path = VFS_SD_0_PATITION_0;

    int ret = bk_vfs_mount("SOURCE_NONE", partition.mount_path, "fatfs", 0, &partition);
    if (ret == BK_OK) {
        s_sd_mounted = true;
    }
    return ret;
}

int sport_dv_sd_unmount(void)
{
    if (!s_sd_mounted) {
        return BK_OK;
    }
    int ret = bk_vfs_umount(VFS_SD_0_PATITION_0);
    if (ret == BK_OK) {
        s_sd_mounted = false;
    }
    return ret;
}

bool sport_dv_sd_is_mounted(void)
{
    return s_sd_mounted;
}

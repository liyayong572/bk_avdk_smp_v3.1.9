#pragma once

#include <stdint.h>
#include <components/avdk_utils/avdk_error.h>

avdk_err_t sport_dv_lcd_backlight_open(uint8_t bl_gpio);
avdk_err_t sport_dv_lcd_backlight_close(uint8_t bl_gpio);
avdk_err_t sport_dv_display_frame_free_cb(void *frame);

int sport_dv_sd_mount(void);
int sport_dv_sd_unmount(void);
bool sport_dv_sd_is_mounted(void);

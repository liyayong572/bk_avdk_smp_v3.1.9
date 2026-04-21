#pragma once

#include <stdint.h>
#include <stdbool.h>

int sport_dv_video_rec_start(const char *file_path, uint32_t width, uint32_t height);
int sport_dv_video_rec_stop(void);
bool sport_dv_video_rec_is_running(void);

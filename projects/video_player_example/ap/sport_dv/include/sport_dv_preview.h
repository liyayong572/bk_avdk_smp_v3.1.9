#pragma once

#include <stdint.h>
#include <stdbool.h>

int sport_dv_preview_start(uint32_t width, uint32_t height);
int sport_dv_preview_stop(void);
int sport_dv_preview_request_snapshot(const char *path);
bool sport_dv_preview_is_running(void);

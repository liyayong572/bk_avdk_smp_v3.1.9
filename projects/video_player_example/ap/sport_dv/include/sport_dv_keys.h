#pragma once

#include <stdint.h>

typedef enum {
    SPORT_DV_KEY_EVENT_MODE = 100,
    SPORT_DV_KEY_EVENT_OK,
    SPORT_DV_KEY_EVENT_UP,
    SPORT_DV_KEY_EVENT_DOWN,
} sport_dv_key_event_t;

typedef void (*sport_dv_key_handler_t)(uint8_t event);

int sport_dv_keys_init(sport_dv_key_handler_t handler);
int sport_dv_keys_deinit(void);

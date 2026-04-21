#include <common/bk_include.h>
#include <os/os.h>
#include <os/mem.h>
#include "key_adapter.h"
#include "sport_dv_cfg.h"
#include "sport_dv_keys.h"

static sport_dv_key_handler_t s_handler = NULL;
static KeyConfig_t s_keys[4];

static void sport_dv_key_dispatch(uint8_t event)
{
    if (s_handler) {
        s_handler(event);
    }
}

int sport_dv_keys_init(sport_dv_key_handler_t handler)
{
    s_handler = handler;

    os_memset(s_keys, 0, sizeof(s_keys));

    s_keys[0].gpio_id = (uint8_t)SPORT_DV_KEY_GPIO_MODE;
    s_keys[0].active_level = SPORT_DV_KEY_ACTIVE_LEVEL;
    s_keys[0].short_event = SPORT_DV_KEY_EVENT_MODE;
    s_keys[0].double_event = EVENT_NONE;
    s_keys[0].long_event = EVENT_NONE;
    s_keys[0].long_press_up_event = EVENT_NONE;

    s_keys[1].gpio_id = (uint8_t)SPORT_DV_KEY_GPIO_OK;
    s_keys[1].active_level = SPORT_DV_KEY_ACTIVE_LEVEL;
    s_keys[1].short_event = SPORT_DV_KEY_EVENT_OK;
    s_keys[1].double_event = EVENT_NONE;
    s_keys[1].long_event = EVENT_NONE;
    s_keys[1].long_press_up_event = EVENT_NONE;

    s_keys[2].gpio_id = (uint8_t)SPORT_DV_KEY_GPIO_UP;
    s_keys[2].active_level = SPORT_DV_KEY_ACTIVE_LEVEL;
    s_keys[2].short_event = SPORT_DV_KEY_EVENT_UP;
    s_keys[2].double_event = EVENT_NONE;
    s_keys[2].long_event = EVENT_NONE;
    s_keys[2].long_press_up_event = EVENT_NONE;

    s_keys[3].gpio_id = (uint8_t)SPORT_DV_KEY_GPIO_DOWN;
    s_keys[3].active_level = SPORT_DV_KEY_ACTIVE_LEVEL;
    s_keys[3].short_event = SPORT_DV_KEY_EVENT_DOWN;
    s_keys[3].double_event = EVENT_NONE;
    s_keys[3].long_event = EVENT_NONE;
    s_keys[3].long_press_up_event = EVENT_NONE;

    bk_key_register_event_handler(sport_dv_key_dispatch);
    bk_key_driver_init(s_keys, 4);
    return BK_OK;
}

int sport_dv_keys_deinit(void)
{
    bk_key_driver_deinit(s_keys, 4);
    s_handler = NULL;
    return BK_OK;
}

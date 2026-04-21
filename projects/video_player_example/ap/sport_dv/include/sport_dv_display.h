#pragma once

#include <stdint.h>
#include <components/avdk_utils/avdk_error.h>
#include "frame_buffer.h"

avdk_err_t sport_dv_display_start(void);
avdk_err_t sport_dv_display_stop(void);
avdk_err_t sport_dv_display_push(frame_buffer_t *frame);

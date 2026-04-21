#pragma once

#include <stdint.h>
#include <stdbool.h>

#include <common/bk_include.h>
#include <components/avdk_utils/avdk_error.h>

#include "components/bk_dma2d.h"
#include "components/bk_draw_osd_types.h"

avdk_err_t bk_dma2d_yuv_blend_icon_to_rgb565(bk_dma2d_ctlr_handle_t handle,
                                            const frame_buffer_t *yuv_frame,
                                            frame_buffer_t *dst_rgb565_frame,
                                            const bk_blend_t *icon,
                                            data_reverse_t out_byte_by_byte_reverse,
                                            bool is_sync);

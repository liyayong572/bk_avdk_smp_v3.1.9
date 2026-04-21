#include <os/os.h>
#include <common/bk_include.h>
#include <components/avdk_utils/avdk_error.h>

#include "components/bk_dma2d.h"
#include "components/media_types.h"
#include "frame_buffer.h"
#include "dma2d_yuv_blend.h"

#define TAG "dma2d_yuv_blend"
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

static bool dma2d_map_yuv_format(pixel_format_t fmt, input_color_mode_t *out_mode)
{
    if (out_mode == NULL) {
        return false;
    }

    switch (fmt) {
        case PIXEL_FMT_YUYV:
            *out_mode = DMA2D_INPUT_YUYV;
            return true;
        case PIXEL_FMT_UYVY:
            *out_mode = DMA2D_INPUT_UYVY;
            return true;
        case PIXEL_FMT_YYUV:
            *out_mode = DMA2D_INPUT_YYUV;
            return true;
        case PIXEL_FMT_UVYY:
            *out_mode = DMA2D_INPUT_UVYY;
            return true;
        case PIXEL_FMT_VUYY:
            *out_mode = DMA2D_INPUT_VUYY;
            return true;
        default:
            return false;
    }
}

avdk_err_t bk_dma2d_yuv_blend_icon_to_rgb565(bk_dma2d_ctlr_handle_t handle,
                                            const frame_buffer_t *yuv_frame,
                                            frame_buffer_t *dst_rgb565_frame,
                                            const bk_blend_t *icon,
                                            data_reverse_t out_byte_by_byte_reverse,
                                            bool is_sync)
{
    if (handle == NULL || yuv_frame == NULL || dst_rgb565_frame == NULL || icon == NULL) {
        return AVDK_ERR_INVAL;
    }
    if (yuv_frame->frame == NULL || dst_rgb565_frame->frame == NULL || icon->image.data == NULL) {
        return AVDK_ERR_INVAL;
    }
    if (dst_rgb565_frame->fmt != PIXEL_FMT_RGB565) {
        return AVDK_ERR_INVAL;
    }
    if (dst_rgb565_frame->width != yuv_frame->width || dst_rgb565_frame->height != yuv_frame->height) {
        return AVDK_ERR_INVAL;
    }
    if ((icon->xpos + icon->width > yuv_frame->width) || (icon->ypos + icon->height > yuv_frame->height)) {
        return AVDK_ERR_INVAL;
    }

    input_color_mode_t bg_mode = 0;
    if (!dma2d_map_yuv_format(yuv_frame->fmt, &bg_mode)) {
        return AVDK_ERR_UNSUPPORTED;
    }

    dma2d_blend_config_t cfg = {0};
    cfg.blend.pfg_addr = (char *)icon->image.data;
    cfg.blend.pbg_addr = (char *)yuv_frame->frame;
    cfg.blend.pdst_addr = (char *)dst_rgb565_frame->frame;

    cfg.blend.fg_color_mode = DMA2D_INPUT_ARGB8888;
    cfg.blend.bg_color_mode = bg_mode;
    cfg.blend.dst_color_mode = DMA2D_OUTPUT_RGB565;

    cfg.blend.fg_red_blue_swap = DMA2D_RB_SWAP;
    cfg.blend.bg_red_blue_swap = DMA2D_RB_REGULAR;
    cfg.blend.dst_red_blue_swap = DMA2D_RB_REGULAR;

    cfg.blend.fg_frame_width = icon->width;
    cfg.blend.fg_frame_height = icon->height;
    cfg.blend.bg_frame_width = yuv_frame->width;
    cfg.blend.bg_frame_height = yuv_frame->height;
    cfg.blend.dst_frame_width = dst_rgb565_frame->width;
    cfg.blend.dst_frame_height = dst_rgb565_frame->height;

    cfg.blend.fg_frame_xpos = 0;
    cfg.blend.fg_frame_ypos = 0;
    cfg.blend.bg_frame_xpos = icon->xpos;
    cfg.blend.bg_frame_ypos = icon->ypos;
    cfg.blend.dst_frame_xpos = icon->xpos;
    cfg.blend.dst_frame_ypos = icon->ypos;

    cfg.blend.fg_pixel_byte = FOUR_BYTES;
    cfg.blend.bg_pixel_byte = TWO_BYTES;
    cfg.blend.dst_pixel_byte = TWO_BYTES;

    cfg.blend.dma2d_width = icon->width;
    cfg.blend.dma2d_height = icon->height;

    cfg.blend.fg_alpha_mode = DMA2D_NO_MODIF_ALPHA;
    cfg.blend.bg_alpha_mode = DMA2D_NO_MODIF_ALPHA;

    cfg.blend.out_byte_by_byte_reverse = out_byte_by_byte_reverse;
    cfg.blend.input_data_reverse = NO_REVERSE;

    cfg.is_sync = is_sync;

    avdk_err_t ret = bk_dma2d_blend(handle, &cfg);
    if (ret != AVDK_ERR_OK) {
        LOGE("bk_dma2d_blend failed, ret=%d\n", ret);
        return ret;
    }

    return AVDK_ERR_OK;
}

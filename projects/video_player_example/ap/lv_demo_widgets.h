/**
 * @file lv_demo_widgets.h
 *
 */

#ifndef LV_DEMO_WIDGETS_H
#define LV_DEMO_WIDGETS_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#include "lvgl.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 * GLOBAL PROTOTYPES
 **********************/
void lv_demo_widgets(void);
void lv_dvp_preview_set_buffer(void *buf, uint32_t w, uint32_t h);
void lv_dvp_preview_invalidate(void);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /*LV_DEMO_WIDGETS_H*/

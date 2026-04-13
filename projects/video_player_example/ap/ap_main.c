#include <os/os.h>
#include <os/mem.h>
#include "bk_private/bk_init.h"
#include <components/system.h>
#include <components/shell_task.h>
#include <media_service.h>
#include "cli.h"
#include "video_player_cli.h"

#include <modules/pm.h>
#include <driver/pwr_clk.h>
#include "components/media_types.h"
#include "driver/drv_tp.h"
#if CONFIG_LVGL
#include "lvgl.h"
#include "lv_vendor.h"
#include "lv_demo_widgets.h"
#endif
#if (defined(CONFIG_INTEGRATION_DOORBELL) || defined(CONFIG_INTEGRATION_DOORVIEWER))
#include "bk_smart_config.h"
#include <doorbell_comm.h>
#include <stdio.h>
#endif

#if CONFIG_LV_USE_DEMO_WIDGETS
#include "components/bk_display.h"
#include "driver/gpio.h"
#include "gpio_driver.h"
#include "driver/pwr_clk.h"
#define TAG "widgets"

#include "components/bk_dma2d.h"
#include "components/bk_dma2d_types.h"
#include "driver/dma2d.h"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)
#define LOGV(...) BK_LOGV(TAG, ##__VA_ARGS__)

#define LCD_LDO_PIN         (GPIO_13)
extern void user_app_main(void);
extern void rtos_set_user_app_entry(beken_thread_function_t entry);
extern int bk_cli_init(void);
extern void bk_set_jtag_mode(uint32_t cpu_id, uint32_t group_id);
extern const lcd_device_t lcd_device_st7701s;

bk_display_rgb_ctlr_config_t rgb_ctlr_config = {
    .lcd_device = &lcd_device_st7701s,
    .clk_pin = GPIO_0,
    .cs_pin = GPIO_12,
    .sda_pin = GPIO_1,
    .rst_pin = GPIO_6,
};

static avdk_err_t lcd_backlight_open(uint8_t bl_io)
{
    gpio_dev_unmap(bl_io);
    BK_LOG_ON_ERR(bk_gpio_enable_output(bl_io));
    BK_LOG_ON_ERR(bk_gpio_pull_up(bl_io));
    bk_gpio_set_output_high(bl_io);
    return AVDK_ERR_OK;
}

static avdk_err_t lcd_backlight_close(uint8_t bl_io)
{
    BK_LOG_ON_ERR(bk_gpio_pull_down(bl_io));
    bk_gpio_set_output_low(bl_io);
    return AVDK_ERR_OK;
}

bk_err_t lvgl_app_widgets_init(void)
{
    lv_vnd_config_t lv_vnd_config = {0};

    lv_vnd_config.width = rgb_ctlr_config.lcd_device->width;
    lv_vnd_config.height = rgb_ctlr_config.lcd_device->height;
    lv_vnd_config.render_mode = RENDER_PARTIAL_MODE;
    lv_vnd_config.rotation = ROTATE_NONE;
    for (int i = 0; i < CONFIG_LVGL_FRAME_BUFFER_NUM; i++) {
        lv_vnd_config.frame_buffer[i] = frame_buffer_display_malloc(lv_vnd_config.width * lv_vnd_config.height * sizeof(bk_color_t));
        if (lv_vnd_config.frame_buffer[i] == NULL) {
            LOGE("lv_frame_buffer[%d] malloc failed\r\n", i);
            return BK_FAIL;
        }
    }
    bk_display_rgb_new(&lv_vnd_config.handle, &rgb_ctlr_config);
    lv_vendor_init(&lv_vnd_config);
    bk_pm_module_vote_ctrl_external_ldo(GPIO_CTRL_LDO_MODULE_LCD, LCD_LDO_PIN, GPIO_OUTPUT_STATE_HIGH);
    bk_display_open(lv_vnd_config.handle);
    lcd_backlight_open(GPIO_7);

	#if (CONFIG_TP)
    drv_tp_open(lv_vnd_config.width, lv_vnd_config.height, TP_MIRROR_NONE);
	#endif

    lv_vendor_disp_lock();
    lv_demo_widgets();
    lv_vendor_disp_unlock();

    lv_vendor_start();

    LOGI("LVGL widgets initialized with display sync optimization\r\n");
    return BK_OK;
}


beken_semaphore_t g_dvp_dma2d_sem = NULL;

// DMA2D 转换完成的硬件中断服务函数 (ISR)
static void dvp_dma2d_transfer_complete_isr(void *arg)
{
    // 硬件干完活了，释放信号量（按下叫号器）
    if (g_dvp_dma2d_sem != NULL) {
        rtos_set_semaphore(&g_dvp_dma2d_sem);
    }
}

static void dvp_dma2d_config_error(void *arg)
{
    LOGD("%s \n", __func__);
}
// 可选：错误处理中断
static void dvp_dma2d_transfer_error_isr(void *arg)
{
    LOGE("DMA2D Transfer Error!\n");
    if (g_dvp_dma2d_sem != NULL) {
        rtos_set_semaphore(&g_dvp_dma2d_sem); // 出错也要释放，防止任务永久死锁
    }
}

static bk_err_t dvp_dma2d_int_init(void)
{
    bk_err_t ret = BK_OK;

    // 1. 创建信号量（初始值为 0，最大值为 1）
    if (g_dvp_dma2d_sem == NULL) {
        ret = rtos_init_semaphore_ex(&g_dvp_dma2d_sem, 1, 0);
        if (BK_OK != ret) {
            LOGE("%s dma2d_sem init failed\n", __func__);
            return ret;
        }
    }

    // 2. 初始化 DMA2D 驱动 (如果系统前面没初始化过的话)
    bk_dma2d_driver_init();

    // 3. 注册中断回调函数
    bk_dma2d_register_int_callback_isr(DMA2D_CFG_ERROR_ISR, dvp_dma2d_config_error, NULL);
    bk_dma2d_register_int_callback_isr(DMA2D_TRANS_ERROR_ISR, dvp_dma2d_transfer_error_isr, NULL);
    bk_dma2d_register_int_callback_isr(DMA2D_TRANS_COMPLETE_ISR, dvp_dma2d_transfer_complete_isr, NULL);
    
    // 4. 在硬件层级使能这些中断
    bk_dma2d_int_enable(DMA2D_CFG_ERROR_ISR | DMA2D_TRANS_ERROR | DMA2D_TRANS_COMPLETE, 1);

    return ret;
}

// 对应的反初始化，在 stop 时调用
static void dvp_dma2d_int_deinit(void)
{
    if (g_dvp_dma2d_sem != NULL) {
        rtos_deinit_semaphore(&g_dvp_dma2d_sem);
        g_dvp_dma2d_sem = NULL;
    }
    // 关闭中断
    bk_dma2d_int_enable(DMA2D_CFG_ERROR_ISR | DMA2D_TRANS_ERROR | DMA2D_TRANS_COMPLETE, 0);
}

#endif



int main(void)
{
    bk_init();
    media_service_init();
    cli_video_player_init();

	bk_pm_module_vote_cpu_freq(PM_POWER_SUB_MODULE_NAME_VIDP_LCD, PM_CPU_FRQ_480M);
	
    #if (defined(CONFIG_LV_USE_DEMO_WIDGETS))
	bk_pm_module_vote_psram_ctrl(PM_POWER_PSRAM_MODULE_NAME_LVGL_CODE_RUN, PM_POWER_MODULE_STATE_ON);
    lvgl_app_widgets_init();
	dvp_dma2d_int_init();
    #endif 

	#if (defined(CONFIG_INTEGRATION_DOORBELL) || defined(CONFIG_INTEGRATION_DOORVIEWER))
	printf("CONFIG_INTEGRATION_DOORBELL ....\n");
    bk_smart_config_init();
	printf("doorbell_core_init ....\n");
    doorbell_core_init();
	#endif
	
    return 0;
}

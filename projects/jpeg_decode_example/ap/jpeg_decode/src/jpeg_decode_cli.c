#include "bk_private/bk_init.h"
#include <os/os.h>
#include <os/str.h>
#include "jpeg_data.h"
#include <media_service.h>
#include "frame_buffer.h"
#include "components/bk_jpeg_decode/bk_jpeg_decode_hw.h"
#include "components/bk_jpeg_decode/bk_jpeg_decode_sw.h"
#include "jpeg_decode_test.h"

#include "lcd_panel_devices.h"
#include <components/bk_camera_ctlr.h>
#include <common/bk_include.h>
#include <components/avdk_utils/avdk_error.h>
#include <components/bk_display.h>
#include <os/mem.h>
#include <driver/gpio.h>
#include <driver/pwr_clk.h>
#include <bk_vfs.h>
#include <bk_posix.h>
#include <bk_partition.h>
#include <sys/stat.h>
#include "cli.h"
#include "gpio_driver.h"


#define TAG "jdec_cli"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)
#define LOGV(...) BK_LOGV(TAG, ##__VA_ARGS__)

bk_display_ctlr_handle_t jpg_display_lcd_handle = NULL;

#define JPG_DECODE_LCD_DEVICE_SWITCH 1
#if JPG_DECODE_LCD_DEVICE_SWITCH
static const lcd_device_t *lcd_device = &lcd_device_st7701s;// lcd_device_st7282
#endif

#define LCD_LDO_PIN          (GPIO_13)
#define GPIO_INVALID_ID      (0xFF)

#ifdef CONFIG_DVP_CTRL_POWER_GPIO_ID
#define DVP_POWER_GPIO_ID CONFIG_DVP_CTRL_POWER_GPIO_ID
#else
#define DVP_POWER_GPIO_ID GPIO_INVALID_ID
#endif

avdk_err_t lcd_backlight_open(uint8_t bl_io)
{
    avdk_err_t ret = AVDK_ERR_OK;
    gpio_dev_unmap(bl_io);
    ret = bk_gpio_enable_output(bl_io);
    if (ret != AVDK_ERR_OK)
    {
        LOGE("%s: bk_gpio_enable_output failed, ret:%d\n", __func__, ret);
        return ret;
    }
    ret = bk_gpio_pull_up(bl_io);
    if (ret != AVDK_ERR_OK)
    {
        LOGE("%s: bk_gpio_pull_up failed, ret:%d\n", __func__, ret);
        return ret;
    }
    bk_gpio_set_output_high(bl_io);
    return AVDK_ERR_OK;
}

avdk_err_t lcd_backlight_close(uint8_t bl_io)
{
    avdk_err_t ret = AVDK_ERR_OK;
    ret = bk_gpio_pull_down(bl_io);
    if (ret != AVDK_ERR_OK)
    {
        LOGE("%s: bk_gpio_pull_down failed, ret:%d\n", __func__, ret);
        return ret;
    }
    bk_gpio_set_output_low(bl_io);
    return AVDK_ERR_OK;
}

typedef struct {
    uint32_t size;
    uint8_t *data;
} image_info_t;

static image_info_t *dec_jpeg_data = NULL;

image_info_t* load_image_to_psram(const char *filepath)
{
	printf("open = \n");
	
	int fd = -1;
    uint32_t file_size = 0;
    int ret = 0;
	
	//img->size = file_size;
    // 1. 打开文件
    fd = open("/sd0/snapshot_0.jpg", 0x2000);
	printf("open = %d\n",fd);
	
    if (fd < 0) {
        LOGE("VFS: Open %s failed\n", filepath);
        return NULL;
    }
	printf("2 open = %d\n",fd);
	
    // 2. 获取文件长度
    file_size = lseek(fd, 0, 2);
    //lseek(fd, 0, SEEK_SET);
    lseek(fd, 0, 0);

    if (file_size == 0) {
        LOGE("VFS: File is empty\n");
        close(fd);
        return NULL;
    }

    //img->size = file_size;
	
	//printf("img->size = %d\n",img->size);

	uint32_t total_size = sizeof(image_info_t) + file_size;
	
    //img->data = os_malloc(file_size);
    image_info_t *dst = os_malloc(total_size);
	
	printf("3 open = %d\n",fd);
	
    if (!dst) {
        LOGE("PSRAM: Malloc failed, size: %d bytes\n", total_size);
		os_free(dst);
		close(fd);
        return NULL;
    }
	printf("4 open = %d\n",fd);
	dst->size = file_size;
	
    // 4. 读取数据到 PSRAM
    // VFS 的 read 函数会自动处理从 SD 卡到 PSRAM 地址的数据搬运
    ret = read(fd, (uint8_t*)dst + sizeof(image_info_t), file_size);
    if (ret != file_size) {
        LOGE("Read error: expected %d, got %d\n", file_size, ret);
        os_free(dst); // 失败时释放 PSRAM
        dst = NULL;
        close(fd);
        return NULL;
    }
	printf("5 open = %d\n",fd);

	dst->data = (uint8_t*)dst + sizeof(image_info_t);
	
    close(fd);
    LOGI("Load success! Data at PSRAM: %p, size: %d\n", dst, file_size);
	
    return dst;
}

void free_image_data(image_info_t *img) {
    if (img->data) {
        os_free(img->data);
        img->data = NULL;
    }
}

static bool sds_card_mounted = false;

// Mount SD card to /sd0
int dec_sd_card_mount(void)
{
    int ret = BK_OK;

    LOGD("%s: Starting SD card mount, current status: %s\n", __func__, sds_card_mounted ? "mounted" : "not mounted");

    if (!sds_card_mounted)
    {
        struct bk_fatfs_partition partition;
        char *fs_name = NULL;
        fs_name = "fatfs";
        partition.part_type = FATFS_DEVICE;
        partition.part_dev.device_name = FATFS_DEV_SDCARD;
        partition.mount_path = VFS_SD_0_PATITION_0;

        LOGD("%s: Calling mount() with path=%s, fs_name=%s\n", __func__, partition.mount_path, fs_name);
        ret = mount("SOURCE_NONE", partition.mount_path, fs_name, 0, &partition);
        LOGD("%s: mount() returned: %d\n", __func__, ret);

        if (ret == BK_OK)
        {
            sds_card_mounted = true;
            LOGI("%s: SD card mounted to /sd0 successfully\n", __func__);
        }
        else
        {
            LOGE("%s: Failed to mount SD card, ret=%d\n", __func__, ret);
        }
    }
    else
    {
        LOGD("%s: SD card already mounted\n", __func__);
    }

    return ret;
}

// Unmount SD card from /sd0
int dec_sd_card_unmount(void)
{
    int ret = BK_OK;

    if (sds_card_mounted)
    {
        ret = umount(VFS_SD_0_PATITION_0);
        if (ret == BK_OK)
        {
            sds_card_mounted = false;
            LOGI("%s: SD card unmounted from /sd0 successfully\n", __func__);
        }
        else
        {
            LOGE("%s: Failed to unmount SD card, ret=%d\n", __func__, ret);
        }
    }
    else
    {
        LOGD("%s: SD card not mounted\n", __func__);
    }

    return ret;
}



static bk_jpeg_decode_sw_handle_t jpeg_decode_sw_handle = NULL;
static bk_jpeg_decode_sw_config_t jpeg_decode_sw_config = {
    .decode_cbs = {
        .in_complete = jpeg_decode_in_complete,
        .out_malloc = jpeg_decode_out_malloc,
        .out_complete = jpeg_decode_out_complete,
    }
};

static bk_jpeg_decode_hw_handle_t jpeg_decode_hw_handle = NULL;
static bk_jpeg_decode_hw_config_t jpeg_decode_hw_config = {
    .decode_cbs = {
        .in_complete = jpeg_decode_in_complete,
        .out_malloc = jpeg_decode_out_malloc,
        .out_complete = jpeg_decode_out_complete,
    }
};

static bk_jpeg_decode_hw_opt_config_t jpeg_decode_hw_opt_config = {
    .decode_cbs = {
        .in_complete = jpeg_decode_in_complete,
        .out_malloc = jpeg_decode_out_malloc,
        .out_complete = jpeg_decode_out_complete,
    },
    .sram_buffer = NULL,
    .image_max_width = 864,
    .is_pingpong = 0,
    .lines_per_block = 16,  // Using plain number instead of enum
    .copy_method = 0,       // Using plain number instead of enum
};

static jpeg_decode_test_type_t jpeg_decode_mode = JPEG_DECODE_MODE_HARDWARE;

void cli_jpeg_decode_cmd(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
#if 1
    bk_err_t ret = BK_OK;

    if (os_strcmp(argv[1], "init_hw") == 0) {
        ret = bk_hardware_jpeg_decode_new(&jpeg_decode_hw_handle, &jpeg_decode_hw_config);
        if (ret != BK_OK) {
            LOGE("%s, %d, bk_hardware_jpeg_decode_new failed!\n", __func__, __LINE__);
        } else {
            LOGD("%s, %d, bk_hardware_jpeg_decode_new success!\n", __func__, __LINE__);
        }
        jpeg_decode_mode = JPEG_DECODE_MODE_HARDWARE;
    }
    else if (os_strcmp(argv[1], "init_sw") == 0) {
        ret = bk_software_jpeg_decode_new(&jpeg_decode_sw_handle, &jpeg_decode_sw_config);
        if (ret != BK_OK) {
            LOGE("%s, %d, bk_software_jpeg_decode_new failed!\n", __func__, __LINE__);
        } else {
            LOGD("%s, %d, bk_software_jpeg_decode_new success!\n", __func__, __LINE__);
        }
        jpeg_decode_mode = JPEG_DECODE_MODE_SOFTWARE;
    }
    else if (os_strcmp(argv[1], "init_sw_on_dtcm") == 0) {
        if (argc > 2)
        {
            if (os_strcmp(argv[2], "1") == 0)
            {
                jpeg_decode_sw_config.core_id = JPEG_DECODE_CORE_ID_1;
            }
            else if (os_strcmp(argv[2], "2") == 0)
            {
                jpeg_decode_sw_config.core_id = JPEG_DECODE_CORE_ID_2;
            }
            else
            {
                LOGE("%s, %d, param error!\n", __func__, __LINE__);
                return;
            }
        }
        ret = bk_software_jpeg_decode_on_multi_core_new(&jpeg_decode_sw_handle, &jpeg_decode_sw_config);
        if (ret != BK_OK) {
            LOGE("%s, %d, bk_software_jpeg_decode_on_multi_core_new failed!\n", __func__, __LINE__);
        } else {
            LOGD("%s, %d, bk_software_jpeg_decode_on_multi_core_new success!\n", __func__, __LINE__);
        }
        jpeg_decode_mode = JPEG_DECODE_MODE_SOFTWARE;
    }
    else if (os_strcmp(argv[1], "init_hw_line") == 0) {
        ret = bk_hardware_jpeg_decode_opt_new(&jpeg_decode_hw_handle, &jpeg_decode_hw_opt_config);
        if (ret != BK_OK) {
            LOGE("%s, %d, bk_hardware_jpeg_decode_opt_new failed!\n", __func__, __LINE__);
        } else {
            LOGD("%s, %d, bk_hardware_jpeg_decode_opt_new success!\n", __func__, __LINE__);
        }
        jpeg_decode_mode = JPEG_DECODE_MODE_HARDWARE;
    }
    else if (os_strcmp(argv[1], "delete") == 0) {
        if(jpeg_decode_mode == JPEG_DECODE_MODE_HARDWARE)
        {
            ret = bk_jpeg_decode_hw_delete(jpeg_decode_hw_handle);
        }
        else
        {
            ret = bk_jpeg_decode_sw_delete(jpeg_decode_sw_handle);
        }
        if (ret != BK_OK) {
            LOGE("%s, %d, jpeg decode delete failed! ret: %d\n", __func__, __LINE__, ret);
        } else {
            LOGD("%s, %d, jpeg decode delete success!\n", __func__, __LINE__);
            if(jpeg_decode_mode == JPEG_DECODE_MODE_HARDWARE)
            {
                jpeg_decode_hw_handle = NULL;
            }
            else
            {
                jpeg_decode_sw_handle = NULL;
            }
        }
    }
    else if (os_strcmp(argv[1], "open") == 0) {
        if(jpeg_decode_mode == JPEG_DECODE_MODE_HARDWARE)
        {
            ret = bk_jpeg_decode_hw_open(jpeg_decode_hw_handle);
        }
        else
        {
            ret = bk_jpeg_decode_sw_open(jpeg_decode_sw_handle);
        }
        if (ret != BK_OK) {
            LOGE("%s, %d, jpeg decode open failed!\n", __func__, __LINE__);
        } else {
            LOGD("%s, %d, jpeg decode open success!\n", __func__, __LINE__);
        }
    }
    else if (os_strcmp(argv[1], "close") == 0) {
        if (jpeg_decode_mode == JPEG_DECODE_MODE_HARDWARE)
        {
            if(jpeg_decode_hw_handle != NULL)
            {
                ret = bk_jpeg_decode_hw_close(jpeg_decode_hw_handle);
            }
        }
        else
        {
            if(jpeg_decode_sw_handle != NULL)
            {
                ret = bk_jpeg_decode_sw_close(jpeg_decode_sw_handle);
            }
        }
        if (ret != BK_OK) {
            LOGE("%s, %d, jpeg decode close failed!\n", __func__, __LINE__);
        } else {
            LOGD("%s, %d, jpeg decode close success!\n", __func__, __LINE__);
        }
    }
    else if (os_strcmp(argv[1], "dec") == 0) {
        if (argc < 3) {
            LOGE("%s, %d, param error!\n", __func__, __LINE__);
            return;
        }

        uint32_t jpeg_length = 0;
        const uint8_t *jpeg_data = NULL;

        if (os_strcmp(argv[2], "422_864_480") == 0) {
            jpeg_length = jpeg_length_422_864_480;
            jpeg_data = jpeg_data_422_864_480;
        }
        else if (os_strcmp(argv[2], "420_864_480") == 0) {
            jpeg_length = jpeg_length_420_864_480;
            jpeg_data = jpeg_data_420_864_480;
        }

        else if (os_strcmp(argv[2], "422_865_480") == 0) {
            jpeg_length = jpeg_length_422_865_480;
            jpeg_data = jpeg_data_422_865_480;
        }
        else if (os_strcmp(argv[2], "422_864_479") == 0) {
            jpeg_length = jpeg_length_422_864_479;
            jpeg_data = jpeg_data_422_864_479;
        }
        else if (os_strcmp(argv[2], "420_865_480") == 0) {
            jpeg_length = jpeg_length_420_865_480;
            jpeg_data = jpeg_data_420_865_480;
        }
        else if (os_strcmp(argv[2], "420_864_479") == 0) {
            jpeg_length = jpeg_length_420_864_479;
            jpeg_data = jpeg_data_420_864_479;
        }

		#if 1
		uint32_t mount_ret = dec_sd_card_mount();
	    if (mount_ret != BK_OK)
	    {
	        LOGE("%s: Failed to mount SD card, ret=%d. Please mount SD card first\n", __func__, mount_ret);
	    }
	    LOGD("%s: SD card mounted successfully\n", __func__);

		dec_jpeg_data = load_image_to_psram("/sd0/snapshot_0.jpg");
		printf("dec_jpeg_data->size = %d\n",dec_jpeg_data->size);
		#endif 
		
		if (jpg_display_lcd_handle != NULL)
        {
            LOGE("%s: DVP display already started\n", __func__);
        }
        // Parse DVP parameters (width, height)
    
        // Step 1: Open LCD display (or reuse LVGL's display when CONFIG_LVGL)
        if (jpg_display_lcd_handle == NULL)
        {
			// Power on LCD LDO
            ret = bk_pm_module_vote_ctrl_external_ldo(GPIO_CTRL_LDO_MODULE_LCD, LCD_LDO_PIN, GPIO_OUTPUT_STATE_HIGH);
            if (ret != AVDK_ERR_OK)
            {
                LOGE("%s: bk_pm_module_vote_ctrl_external_ldo failed, ret:%d\n", __func__, ret);
               
            }

			#if JPG_DECODE_LCD_DEVICE_SWITCH
            // Configure LCD display
            bk_display_rgb_ctlr_config_t lcd_display_config = {0};
            lcd_display_config.lcd_device = lcd_device;
            lcd_display_config.clk_pin = GPIO_0;
            lcd_display_config.cs_pin = GPIO_12;
            lcd_display_config.sda_pin = GPIO_1;
            lcd_display_config.rst_pin = GPIO_6;
            
            // Create LCD display controller
            ret = bk_display_rgb_new(&jpg_display_lcd_handle, &lcd_display_config);
            if (ret != AVDK_ERR_OK)
            {
                LOGE("%s: bk_display_rgb_new failed, ret:%d\n", __func__, ret);
                bk_pm_module_vote_ctrl_external_ldo(GPIO_CTRL_LDO_MODULE_LCD, LCD_LDO_PIN, GPIO_OUTPUT_STATE_LOW);
                
            }
            LOGI("%s: LCD display created successfully\n", __func__);
			#else
			bk_display_spi_ctlr_config_t spi_ctlr_config = {
			    .lcd_device = &lcd_device_st7735S,
			    .spi_id = 0,
			    .dc_pin = GPIO_17,
			    .reset_pin = GPIO_18,
			    .te_pin = 0,
			};

            // Create LCD display controller
			ret = bk_display_spi_new(&jpg_display_lcd_handle, &spi_ctlr_config);
            if (ret != AVDK_ERR_OK)
            {
                LOGE("%s: bk_display_rgb_new failed, ret:%d\n", __func__, ret);
                bk_pm_module_vote_ctrl_external_ldo(GPIO_CTRL_LDO_MODULE_LCD, LCD_LDO_PIN, GPIO_OUTPUT_STATE_LOW);
                
            }
            LOGI("%s: LCD display created successfully\n", __func__);
            #endif 
			
            // Open backlight
            ret = lcd_backlight_open(GPIO_7);
            if (ret != AVDK_ERR_OK)
            {
                LOGE("%s: lcd_backlight_open failed, ret:%d\n", __func__, ret);
                bk_display_delete(jpg_display_lcd_handle);
                jpg_display_lcd_handle = NULL;
                bk_pm_module_vote_ctrl_external_ldo(GPIO_CTRL_LDO_MODULE_LCD, LCD_LDO_PIN, GPIO_OUTPUT_STATE_LOW);
                
            }
            
            // Open LCD display
            ret = bk_display_open(jpg_display_lcd_handle);
            if (ret != AVDK_ERR_OK)
            {
                LOGE("%s: bk_display_open failed, ret:%d\n", __func__, ret);
                lcd_backlight_close(GPIO_7);
                bk_display_delete(jpg_display_lcd_handle);
                jpg_display_lcd_handle = NULL;
                bk_pm_module_vote_ctrl_external_ldo(GPIO_CTRL_LDO_MODULE_LCD, LCD_LDO_PIN, GPIO_OUTPUT_STATE_LOW);
                
            }
            LOGI("%s: LCD display opened successfully\n", __func__);
			
        }
		
        if(jpeg_decode_mode == JPEG_DECODE_MODE_HARDWARE)
        {
        	//ret = perform_jpeg_decode_test(jpeg_decode_hw_handle, jpeg_length, jpeg_data, "manual", jpeg_decode_mode);
            ret = perform_jpeg_decode_test(jpeg_decode_hw_handle, dec_jpeg_data->size, dec_jpeg_data->data, "manual", jpeg_decode_mode);
        }
        else
        {
            ret = perform_jpeg_decode_test(jpeg_decode_sw_handle, jpeg_length, jpeg_data, "manual", jpeg_decode_mode);
        }
    }
    else {
        LOGE("%s, %d, not found this cmd!\n", __func__, __LINE__);
    }

    char *msg = NULL;
    if (ret != BK_OK) {
        msg = CLI_CMD_RSP_ERROR;
    } else {
        msg = CLI_CMD_RSP_SUCCEED;
    }

    LOGI("%s ---complete\n", __func__);
    os_memcpy(pcWriteBuffer, msg, os_strlen(msg));
#endif
}
#include <os/os.h>
#include <os/mem.h>
#include <os/str.h>
#include <media_service.h>
#include <modules/wifi.h>
#include <components/log.h>
#include <components/netif.h>
#include <components/event.h>
#include <bk_private/bk_init.h>
#include <bk_private/bk_wifi.h>
#include <lwip/inet.h>
#include "gatts.h"




#define TAG "main_ap"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)
#define LOGV(...) BK_LOGV(TAG, ##__VA_ARGS__)

#define TEST_PASS()     BK_LOGI(TAG,"%s:%d:%s:%s\r\n", __FILE__, __LINE__, __FUNCTION__, "PASS")
#define TEST_FAIL()     BK_LOGE(TAG,"%s:%d:%s:%s\r\n", __FILE__, __LINE__, __FUNCTION__, "FAIL")



int main(void)
{
    bk_init();
    ble_gatts_init();



    return 0;
}

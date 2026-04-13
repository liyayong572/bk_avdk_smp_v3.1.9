// Copyright 2020-2021 Beken
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <common/sys_config.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <components/log.h>
#include <os/mem.h>
#include <os/str.h>
#include <os/os.h>

#include "components/bluetooth/bk_dm_bluetooth_types.h"
#include "components/bluetooth/bk_dm_gap_ble_types.h"
#include "components/bluetooth/bk_dm_gap_ble.h"
#include "components/bluetooth/bk_dm_ble_types.h"
#include "dm_gattc.h"
#include "dm_gatt_connection.h"


static dm_ble_gattc_app_cb s_gattc_cb_list[2];
//static beken2_timer_t s_gap_bond_timeout_tmr;
static bk_bd_addr_t host_bda;
static uint8_t ble_bond_status=0;
static uint8_t device_manufacture=0;

static int32_t dm_ble_gattc_private_cb(bk_gattc_cb_event_t event, bk_gatt_if_t gattc_if, bk_ble_gattc_cb_param_t *param)
{


    for (int i = 0; i < sizeof(s_gattc_cb_list) / sizeof(s_gattc_cb_list[0]); ++i)
    {
        if (s_gattc_cb_list[i])
        {
            s_gattc_cb_list[i](event, gattc_if, param);
        }
    }
    return 0;
}
static int dm_gattc_add_gattc_callback(void *param)
{
    dm_ble_gattc_app_cb cb = (typeof(cb))param;

    if (!cb)
    {
        return -1;
    }

    for (int i = 0; i < sizeof(s_gattc_cb_list) / sizeof(s_gattc_cb_list[0]); ++i)
    {
        if (!s_gattc_cb_list[i])
        {
            s_gattc_cb_list[i] = cb;
            return 0;
        }
    }

    gatt_loge("full !!!");
    return -1;
}

#if 0
static void ble_gap_create_bond_timer_hdl(void *param)
{
    gatt_logi("~~ble_bond_status=%x",ble_bond_status);
    if(ble_bond_status==0)
    {
        bk_ble_gap_create_bond(host_bda);
    }
}
#endif
static int32_t bk_gattc_cb (bk_gattc_cb_event_t event, bk_gatt_if_t gattc_if, bk_ble_gattc_cb_param_t *comm_param)
{
    ble_err_t ret = 0;

    gatt_logi("~~~~ event= %d", event);
    switch (event)
    {

    case BK_GATTC_READ_BY_TYPE_EVT:
    {
        struct gattc_read_by_type_evt_param *param = (typeof(param))comm_param;

        gatt_logi("BK_GATTC_READ_BY_TYPE_EVT %d %d %d", param->status, param->conn_id, param->elem_count);

        if(param->elem!=NULL && param->elem->len>0)
        {
            gatt_logi("BK_GATTC_READ_BY_TYPE_EVT %s %d ", param->elem->value, param->elem->len);
            device_manufacture=0;
            if(0==memcmp("Apple Inc",param->elem->value,param->elem->len-1))
            {
                gatt_logi("the host is IOS device");
                device_manufacture=1;
                //rtos_init_oneshot_timer(&s_gap_bond_timeout_tmr, 2000, (timer_2handler_t)ble_gap_create_bond_timer_hdl, NULL,NULL);
                //rtos_start_oneshot_timer(&s_gap_bond_timeout_tmr);
            }
        }
    }
    break;

    case BK_GATTC_CONNECT_EVT:
    {
        struct gattc_connect_evt_param *param = (typeof(param))comm_param;
        uint16_t hci_handle = 0;
        ble_err_t ret = bk_ble_get_hci_handle_from_gatt_conn_id(param->conn_id, &hci_handle);

        gatt_logi("BK_GATTC_CONNECT_EVT role %d %02X:%02X:%02X:%02X:%02X:%02X conn_id %d hci_handle 0x%x",
                  param->link_role,
                  param->remote_bda[5],
                  param->remote_bda[4],
                  param->remote_bda[3],
                  param->remote_bda[2],
                  param->remote_bda[1],
                  param->remote_bda[0],
                  param->conn_id,
                  (!ret ? hci_handle : 0xffff)
                 );

        memcpy(host_bda,param->remote_bda,6);
        bk_bt_uuid_t manu_uuid;
        manu_uuid.len=2;
        manu_uuid.uuid.uuid16=GATT_MANUFACTURER_NAME_CHARACTERISTIC;

        bk_ble_gattc_read_by_type(0,param->conn_id,1,0xffff,&manu_uuid,0);
        ble_bond_status_set(0);
    }
    
    break;

    case BK_GATTC_DISCONNECT_EVT:
    {
        struct gattc_disconnect_evt_param *param = (typeof(param))comm_param;

        gatt_logi("BK_GATTC_DISCONNECT_EVT %02X:%02X:%02X:%02X:%02X:%02X conn_id %d",
                  param->remote_bda[5],
                  param->remote_bda[4],
                  param->remote_bda[3],
                  param->remote_bda[2],
                  param->remote_bda[1],
                  param->remote_bda[0],
                  param->conn_id
                 );
    }
    break;

    default:
        break;
    }

    return ret;
}
int ble_bond_status_set(uint8_t bond_status)
{
    ble_bond_status=bond_status;
    return 0;
}
int ble_get_host_device_manufacture(void)
{
    return device_manufacture;
}

int dm_gattc_init(void)
{
    bk_ble_gattc_register_callback(dm_ble_gattc_private_cb);
    dm_gattc_add_gattc_callback(bk_gattc_cb);

    return 0;
}




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

#include "dm_gatts.h"
#include "dm_gatt_connection.h"
#include "dm_gap_utils.h"
#include "f618/f618_server.h"
#include "fa00/fa00_server.h"
#include "hogp/hogpd.h"
#include "battery/battery_server.h"
#include "dis/dis.h"




#define SET_ADVTYPE_TO_IDENTITY_WHEN_NORPA 0
#define INVALID_ATTR_HANDLE 0
#define ADV_HANDLE 0
#define BEKEN_COMPANY_ID           (0x05F0)
#define dm_gatts_app_env_t dm_gatt_demo_app_env_t

enum
{
    DB_REG_STATUS_IDLE,
    DB_REG_STATUS_WAIT_COMPLETED,
    DB_REG_STATUS_COMPLETED
};

typedef struct
{
    uint8_t status; //DB_REG_STATUS_IDLE
    bk_gatts_attr_db_t *db;
    uint16_t *attr_list;
    uint32_t count;
    dm_ble_gatts_db_cb cb;
} dm_gatts_db_reg_t;

typedef struct
{
    dm_gatts_db_reg_t reg[GATT_MAX_PROFILE_COUNT];
    uint32_t count;
} dm_gatts_db_ctx_t;


static int32_t dm_gatts_set_adv_param(uint8_t local_addr_is_public);
static dm_gatts_db_reg_t *find_db_ctx_by_attr_handle(uint16_t attr_handle);

static beken_semaphore_t s_ble_sema = NULL;
static bk_gatt_if_t s_gatts_if;
static dm_gatts_db_ctx_t s_dm_gatts_db_ctx;
static dm_ble_gatts_app_cb s_gatts_cb_list[8];



static int32_t dm_ble_gatts_private_cb(bk_gatts_cb_event_t event, bk_gatt_if_t gatts_if, bk_ble_gatts_cb_param_t *param)
{
    for (int i = 0; i < sizeof(s_gatts_cb_list) / sizeof(s_gatts_cb_list[0]); ++i)
    {
        if (s_gatts_cb_list[i])
        {
            s_gatts_cb_list[i](event, gatts_if, param);
        }
    }

    return 0;
}

static int32_t dm_ble_gap_cb(bk_ble_gap_cb_event_t event, bk_ble_gap_cb_param_t *param)
{
    gatt_logi("~~~event %d", event);

    switch (event)
    {
    //    case BK_BLE_GAP_SET_STATIC_RAND_ADDR_EVT:
    //    {
    //        struct ble_set_rand_cmpl_evt_param *pm = (typeof(pm))param;
    //
    //        if (pm->status)
    //        {
    //            gatt_loge("set rand addr err %d", pm->status);
    //        }
    //
    //        if (s_ble_sema != NULL)
    //        {
    //            rtos_set_semaphore( &s_ble_sema );
    //        }
    //    }
    //    break;

    case BK_BLE_GAP_EXT_ADV_SET_RAND_ADDR_COMPLETE_EVT:
    {
        struct ble_adv_set_rand_addr_cmpl_evt_param *pm = (typeof(pm))param;

        if (pm->status)
        {
            gatt_loge("set adv rand addr err %d", pm->status);
        }

        if (s_ble_sema != NULL)
        {
            rtos_set_semaphore( &s_ble_sema );
        }
    }
    break;

    case BK_BLE_GAP_EXT_ADV_PARAMS_SET_COMPLETE_EVT:
    {
        struct ble_adv_params_set_cmpl_evt_param *pm = (typeof(pm))param;

        if (pm->status)
        {
            gatt_loge("set adv param err 0x%x", pm->status);
        }

        if (s_ble_sema != NULL)
        {
            rtos_set_semaphore( &s_ble_sema );
        }
    }
    break;

    case BK_BLE_GAP_EXT_ADV_DATA_SET_COMPLETE_EVT:
    {
        struct ble_adv_data_set_cmpl_evt_param *pm = (typeof(pm))param;

        if (pm->status)
        {
            gatt_loge("set adv data err %d", pm->status);
        }

        if (s_ble_sema != NULL)
        {
            rtos_set_semaphore( &s_ble_sema );
        }
    }
    break;

    case BK_BLE_GAP_EXT_SCAN_RSP_DATA_SET_COMPLETE_EVT:
    {
        struct ble_adv_scan_rsp_set_cmpl_evt_param *pm = (typeof(pm))param;

        if (pm->status)
        {
            gatt_loge("set adv data scan rsp err %d", pm->status);
        }

        if (s_ble_sema != NULL)
        {
            rtos_set_semaphore( &s_ble_sema );
        }
    }
    break;

    case BK_BLE_GAP_EXT_ADV_DATA_RAW_SET_COMPLETE_EVT:
    {
        struct ble_adv_data_raw_set_cmpl_evt_param *pm = (typeof(pm))param;

        gatt_logi("BK_BLE_GAP_EXT_ADV_DATA_RAW_SET_COMPLETE_EVT 0x%x", pm->status);

        if (s_ble_sema)
        {
            rtos_set_semaphore(&s_ble_sema);
        }
    }
    break;

    case BK_BLE_GAP_EXT_SCAN_RSP_DATA_RAW_SET_COMPLETE_EVT:
    {
        struct ble_scan_rsp_data_raw_set_cmpl_evt_param *pm = (typeof(pm))param;

        gatt_logi("BK_BLE_GAP_EXT_SCAN_RSP_DATA_RAW_SET_COMPLETE_EVT 0x%x", pm->status);

        if (s_ble_sema)
        {
            rtos_set_semaphore(&s_ble_sema);
        }
    }
    break;

    case BK_BLE_GAP_EXT_ADV_START_COMPLETE_EVT:
    {
        struct ble_adv_start_cmpl_evt_param *pm = (typeof(pm))param;

        if (pm->status)
        {
            gatt_loge("set adv enable err %d", pm->status);
        }

        gatt_logw("pls disable adv before remove pair !!!");

        if (s_ble_sema != NULL)
        {
            rtos_set_semaphore( &s_ble_sema );
        }
    }
    break;

    default:
        return DM_BLE_GAP_APP_CB_RET_NO_INTERESTING;
        break;

    }

    return DM_BLE_GAP_APP_CB_RET_PROCESSED;
}


static int32_t bk_gatts_cb (bk_gatts_cb_event_t event, bk_gatt_if_t gatts_if, bk_ble_gatts_cb_param_t *comm_param)
{
    ble_err_t ret = 0;
    dm_gatt_app_env_t *common_env_tmp = NULL;
    dm_gatts_app_env_t *app_env_tmp = NULL;
    gatt_logi("~~~event=%d\n",event);
    switch (event)
    {
    case BK_GATTS_REG_EVT:
    {
        struct gatts_reg_evt_param *param = (typeof(param))comm_param;

        gatt_logi("BK_GATTS_REG_EVT %d %d", param->status, param->gatt_if);
        s_gatts_if = param->gatt_if;

        if (s_ble_sema != NULL)
        {
            rtos_set_semaphore( &s_ble_sema );
        }
    }
    break;

    case BK_GATTS_UNREG_EVT:
    {
        gatt_logi("BK_GATTS_UNREG_EVT");

        if (s_ble_sema != NULL)
        {
            rtos_set_semaphore( &s_ble_sema );
        }
    }
    break;
    case BK_GATTS_EXEC_WRITE_EVT:
    {
        struct gatts_exec_write_evt_param *param = (typeof(param))comm_param;

        gatt_logi("BK_GATTS_EXEC_WRITE_EVT %d %d %d %d", param->conn_id, param->trans_id, param->exec_write_flag, param->need_rsp);

        for (uint32_t i = 0; i < sizeof(s_dm_gatts_db_ctx.reg) / sizeof(s_dm_gatts_db_ctx.reg[0]); ++i)
        {
            if (s_dm_gatts_db_ctx.reg[i].status == DB_REG_STATUS_COMPLETED && s_dm_gatts_db_ctx.reg[i].cb)
            {
                s_dm_gatts_db_ctx.reg[i].cb(event, gatts_if, comm_param);
            }
        }

        if (param->need_rsp)
        {
            bk_gatt_rsp_t rsp;

            memset(&rsp, 0, sizeof(rsp));

            rsp.attr_value.auth_req = BK_GATT_AUTH_REQ_NONE;
            rsp.attr_value.handle = 0;
            rsp.attr_value.offset = 0;
            rsp.attr_value.value = NULL;
            rsp.attr_value.len = 0;

            ret = bk_ble_gatts_send_response(gatts_if, param->conn_id, param->trans_id, BK_GATT_OK, &rsp);
        }
    }
    break;

    case BK_GATTS_CONF_EVT:
    {
        struct gatts_conf_evt_param *param = (typeof(param))comm_param;

        gatt_logi("BK_GATTS_CONF_EVT %d %d %d", param->status, param->conn_id, param->handle);


    }
    break;

    case BK_GATTS_RESPONSE_EVT:
    {
        struct gatts_rsp_evt_param *param = (typeof(param))comm_param;

        gatt_logi("BK_GATTS_RESPONSE_EVT 0x%x %d conn_id %d", param->status, param->handle);

    }
    break;

    case BK_GATTS_SEND_SERVICE_CHANGE_EVT:
    {
        struct gatts_send_service_change_evt_param *param = (typeof(param))comm_param;

        gatt_logi("BK_GATTS_SEND_SERVICE_CHANGE_EVT %02x:%02x:%02x:%02x:%02x:%02x %d %d",
                  param->remote_bda[5],
                  param->remote_bda[4],
                  param->remote_bda[3],
                  param->remote_bda[2],
                  param->remote_bda[1],
                  param->remote_bda[0],
                  param->status, param->conn_id);
    }
    break;

    case BK_GATTS_CONNECT_EVT:
    {
        struct gatts_connect_evt_param *param = (typeof(param))comm_param;
        uint16_t hci_handle = 0;
        ble_err_t ret = bk_ble_get_hci_handle_from_gatt_conn_id(param->conn_id, &hci_handle);

        gatt_logi("BK_GATTS_CONNECT_EVT role %d %02X:%02X:%02X:%02X:%02X:%02X conn_id %d hci_handle 0x%x",
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
        {
            uint16_t conn_id = 0;
            ret = bk_ble_get_gatt_conn_id_from_hci_handle(hci_handle, &conn_id);
            gatt_logi("conn_id %d from hci handle", !ret ? conn_id : -1);
        }
    }
    
    break;

    case BK_GATTS_DISCONNECT_EVT:
    {
        struct gatts_disconnect_evt_param *param = (typeof(param))comm_param;
        

        gatt_logi("BK_GATTS_DISCONNECT_EVT %02X:%02X:%02X:%02X:%02X:%02X conn_id %d",
                  param->remote_bda[5],
                  param->remote_bda[4],
                  param->remote_bda[3],
                  param->remote_bda[2],
                  param->remote_bda[1],
                  param->remote_bda[0],
                  param->conn_id
                 );

        const bk_ble_gap_ext_adv_t ext_adv =
        {
            .instance = 0,
            .duration = 0,
            .max_events = 0,
        };

        ret = bk_ble_gap_adv_start(1, &ext_adv);

        if (ret)
        {
            gatt_loge("bk_ble_gap_adv_start err %d", ret);
        }
        rtos_delay_milliseconds(100);

    }
    break;

    case BK_GATTS_MTU_EVT:
    {
        struct gatts_mtu_evt_param *param = (typeof(param))comm_param;

        gatt_logi("BK_GATTS_MTU_EVT %d %d", param->conn_id, param->mtu);

        common_env_tmp = dm_ble_find_app_env_by_conn_id(param->conn_id);

        if (!common_env_tmp)
        {
            gatt_loge("not found conn_id %d !!!!", param->conn_id);
        }
        else
        {
            app_env_tmp = (typeof(app_env_tmp))common_env_tmp->data;

            if (!app_env_tmp)
            {
                gatt_loge("can't find app_env_tmp");
            }
            else
            {
                app_env_tmp->server_mtu = param->mtu;
            }
        }
    }
    break;

    default:
        break;
    }

    return ret;
}

int32_t dm_gatts_disconnect(uint8_t *addr)
{
    dm_gatt_app_env_t *common_env_tmp = NULL;
    int32_t err = 0;

    gatt_logi("0x%02x:%02x:%02x:%02x:%02x:%02x",
              addr[5],
              addr[4],
              addr[3],
              addr[2],
              addr[1],
              addr[0]);

    if (!s_gatts_if)
    {
        gatt_loge("gatts not init");

        return -1;
    }

    common_env_tmp = dm_ble_find_app_env_by_addr(addr);

    if (!common_env_tmp || !common_env_tmp->data)
    {
        gatt_loge("conn not found !!!!", common_env_tmp, common_env_tmp ? common_env_tmp->data : NULL);
        return -1;
    }

    if (common_env_tmp->status != GAP_CONNECT_STATUS_CONNECTED)
    {
        gatt_loge("connect status is not connected %d", common_env_tmp->status);
        return -1;
    }

    bk_bd_addr_t peer_addr;
    os_memcpy(peer_addr, addr, sizeof(peer_addr));

    err = bk_ble_gap_disconnect(peer_addr);

    if (err)
    {
        gatt_loge("disconnect fail %d", err);
    }
    else
    {
        common_env_tmp->status = GAP_CONNECT_STATUS_DISCONNECTING;
    }

    return err;
}

static int32_t dm_gatts_set_adv_param(uint8_t local_addr_is_public)
{
    int32_t ret = 0;
    //bk_bd_addr_t nominal_addr = {0};
    //uint8_t nominal_addr_type = 0;
    //bk_bd_addr_t identity_addr = {0};
    //uint8_t identity_addr_type = 0;
    bk_ble_gap_ext_adv_params_t adv_param = {0};

    adv_param = (bk_ble_gap_ext_adv_params_t)
    {
        .type = BK_BLE_GAP_SET_EXT_ADV_PROP_LEGACY_IND,
        .interval_min = 120 * 1,
        .interval_max = 160 * 1,
        .channel_map = BK_ADV_CHNL_ALL,
        .filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
        .primary_phy = BK_BLE_GAP_PRI_PHY_1M,
        .secondary_phy = BK_BLE_GAP_PHY_1M,
        .sid = 0,
        .scan_req_notif = 0,
    };

    adv_param.own_addr_type = (local_addr_is_public ? BLE_ADDR_TYPE_PUBLIC : BLE_ADDR_TYPE_RANDOM);
    gatt_logw("set adv param other, addr type 0x%x", adv_param.own_addr_type);

    ret = bk_ble_gap_set_adv_params(ADV_HANDLE, &adv_param);

    if (ret)
    {
        gatt_loge("bk_ble_gap_set_adv_params err %d", ret);
        goto error;
    }

    return 0;

error:;

    gatt_loge("err");
    return -1;
}



int32_t dm_gatts_send_notify(uint16_t gatt_conn_id, uint16_t attr_handle, uint8_t *data, uint32_t len, uint8_t is_notify)
{
    int32_t ret = 0;

    dm_gatts_app_env_t *app_env_tmp = NULL;
    dm_gatt_app_env_t *common_env_tmp = NULL;

    if (!s_gatts_if)
    {
        gatt_loge("gatts not init");
        return -1;
    }

    common_env_tmp = dm_ble_find_app_env_by_conn_id(gatt_conn_id);

    if (!common_env_tmp || !common_env_tmp->data)
    {
        gatt_loge("conn_id %d not found %d %p", gatt_conn_id, common_env_tmp->data);
        ret = -1;
        goto end;
    }

    app_env_tmp = (typeof(app_env_tmp))common_env_tmp->data;

    if (!common_env_tmp->server_sem)
    {
        ret = rtos_init_semaphore(&common_env_tmp->server_sem, 1);

        if (ret)
        {
            gatt_loge("init sem err %d", ret);
            ret = -1;
            goto end;
        }
    }

    ret = bk_ble_gatts_send_indicate(s_gatts_if, (uint16_t)gatt_conn_id, attr_handle, len, data, is_notify ? 0 : 1);

    if (ret)
    {
        gatt_loge("bk_ble_gatts_send_indicate err %d", ret);
        ret = -1;
        goto end;
    }

    ret = rtos_get_semaphore(&common_env_tmp->server_sem, SYNC_CMD_TIMEOUT_MS);

    if (ret)
    {
        gatt_loge("wait send indicate err %d", ret);
        ret = -1;
        goto end;
    }

end:

    ret = (app_env_tmp->send_notify_status ? -1 : 0);

    app_env_tmp->send_notify_status = 0;

    if (common_env_tmp->server_sem)
    {
        if (rtos_deinit_semaphore(&common_env_tmp->server_sem))
        {
            gatt_loge("rtos_deinit_semaphore s_ble_data_sem err %d", ret);
        }

        common_env_tmp->server_sem = NULL;
    }

    return ret;
}

static dm_gatts_db_reg_t *find_db_ctx_by_attr_handle(uint16_t attr_handle)
{
    for (uint32_t i = 0; i < sizeof(s_dm_gatts_db_ctx.reg) / sizeof(s_dm_gatts_db_ctx.reg[0]); ++i)
    {
        if (s_dm_gatts_db_ctx.reg[i].status == DB_REG_STATUS_COMPLETED &&
                s_dm_gatts_db_ctx.reg[i].attr_list &&
                s_dm_gatts_db_ctx.reg[i].attr_list[0] <= attr_handle &&
                s_dm_gatts_db_ctx.reg[i].attr_list[s_dm_gatts_db_ctx.reg[i].count - 1] >= attr_handle
           )
        {
            return &s_dm_gatts_db_ctx.reg[i];
        }
    }

    return NULL;
}


int dm_gatts_add_gatts_callback(void *param)
{
    dm_ble_gatts_app_cb cb = (typeof(cb))param;

    if (!cb)
    {
        return -1;
    }

    for (int i = 0; i < sizeof(s_gatts_cb_list) / sizeof(s_gatts_cb_list[0]); ++i)
    {
        if (!s_gatts_cb_list[i])
        {
            s_gatts_cb_list[i] = cb;
            return 0;
        }
    }

    gatt_loge("full !!!");
    return -1;
}


int32_t dm_gatts_set_advertising_data(char *adv_name)
{
    ble_err_t ret = 0;

    uint8_t adv_data[31] = {0}; //legacy adv max len
    uint32_t adv_index = 0, len_index = 0;

    /* flags */
    len_index = adv_index;
    adv_data[adv_index++] = 0x00;
    adv_data[adv_index++] = BK_BLE_AD_TYPE_FLAG;
    adv_data[adv_index++] = 0x06;
    adv_data[len_index] = 2;

    len_index = adv_index;
    adv_data[adv_index++] = 0x00;
    adv_data[adv_index++] = BK_BLE_AD_TYPE_16SRV_CMPL;
    adv_data[adv_index++] = 0x12;
    adv_data[adv_index++] = 0x18;
    adv_data[len_index] = 3;

    len_index = adv_index;
    adv_data[adv_index++] = 0x00;
    adv_data[adv_index++] = BK_BLE_AD_TYPE_APPEARANCE;
    adv_data[adv_index++] = 0xC1;
    adv_data[adv_index++] = 0x03;
    adv_data[len_index] = 3;

    // name
    len_index = adv_index;
    adv_data[adv_index++] = 0x00;
    adv_data[adv_index++] = BK_BLE_AD_TYPE_NAME_CMPL;

    ret = sprintf((char *)&adv_data[adv_index], "%s", adv_name);

    adv_index += ret;
    adv_data[len_index] = ret + 1;
    gatt_loge("adv_name len= %d", ret);
#if 0
    /* manufacturer */
    len_index = adv_index;
    adv_data[adv_index++] = 0x00;
    adv_data[adv_index++] = BK_BLE_AD_TYPE_MANU;
    adv_data[adv_index++] = BEKEN_COMPANY_ID & 0xFF;
    adv_data[adv_index++] = BEKEN_COMPANY_ID >> 8;
    adv_data[len_index] = 3;
#endif
    ret = bk_ble_gap_set_adv_data_raw(0, adv_index, (const uint8_t *)adv_data);

    if (ret)
    {
        gatt_loge("bk_ble_gap_set_adv_data_raw err %d", ret);
        goto error;
    }

    ret = rtos_get_semaphore(&s_ble_sema, SYNC_CMD_TIMEOUT_MS);

    if (ret != kNoErr)
    {
        gatt_loge("wait set adv data err %d", ret);
        goto error;
    }

    ret = bk_ble_gap_set_scan_rsp_data_raw(0, adv_index, (const uint8_t *)adv_data);

    if (ret)
    {
        gatt_loge("bk_ble_gap_set_scan_rsp_data_raw err %d", ret);
        goto error;
    }

    ret = rtos_get_semaphore(&s_ble_sema, SYNC_CMD_TIMEOUT_MS);

    if (ret != kNoErr)
    {
        gatt_loge("wait set scan rsp adv data err %d", ret);
        goto error;
    }
    return 0;
error:
    gatt_loge("err");
    return -1;

}
int32_t dm_gatts_enable_adv(uint8_t enable)
{
    int32_t ret = 0;

    if (!s_gatts_if)
    {
        gatt_loge("gatts not init");
        return -1;
    }

    if (enable)
    {
        const bk_ble_gap_ext_adv_t ext_adv =
        {
            .instance = 0,
            .duration = 0,
            .max_events = 0,
        };
        ret = bk_ble_gap_adv_start(1, &ext_adv);
    }
    else
    {
        const uint8_t ext_adv_inst[] = {0};
        ret = bk_ble_gap_adv_stop(sizeof(ext_adv_inst) / sizeof(ext_adv_inst[0]), ext_adv_inst);
    }

    if (ret)
    {
        gatt_loge("bk_ble_gap_adv_start err %d", ret);
        return -1;;
    }

    ret = rtos_get_semaphore(&s_ble_sema, SYNC_CMD_TIMEOUT_MS);

    if (ret != kNoErr)
    {
        gatt_loge("wait set adv enable err %d", ret);
        return -1;;
    }

    return 0;
}


int dm_gatts_init(void)
{
    static uint8_t s_dm_gatts_is_init=0;
    ble_err_t ret = 0;

    if (s_dm_gatts_is_init)
    {
        gatt_loge("already init");
        return -1;
    }

    ret = rtos_init_semaphore(&s_ble_sema, 1);

    if (ret != 0)
    {
        gatt_loge("rtos_init_semaphore err %d", ret);
        return -1;
    }

    bk_ble_gatts_register_callback(dm_ble_gatts_private_cb);
    dm_gatts_add_gatts_callback(bk_gatts_cb);
    dm_gatt_add_gap_callback(dm_ble_gap_cb);
    
    ret = bk_ble_gatts_app_register(0);

    if (ret)
    {
        gatt_loge("reg err %d", ret);
        return -1;
    }


    //device_info_server_init();
    //battery_server_init();
    //hogpd_server_init();
    f618_server_init();
    fa00_server_init();


    bk_bd_addr_t identity_addr = {0};
    char adv_name[64] = {0};

    dm_ble_gap_get_identity_addr(identity_addr);
    snprintf((char *)(adv_name), sizeof(adv_name) - 1, "%s_%02x%02x%02x", LOCAL_NAME, identity_addr[2], identity_addr[1], identity_addr[0]);

    gatt_logi("adv name %s", adv_name);
    ret = bk_ble_gap_set_device_name(adv_name);

    if (ret)
    {
        gatt_loge("bk_ble_gap_set_device_name err %d", ret);
        return -1;
    }

    ret = dm_gatts_set_adv_param(1);

    if (ret != kNoErr)
    {
        gatt_loge("set adv param err %d", ret);
        return -1;
    }

    ret = rtos_get_semaphore(&s_ble_sema, SYNC_CMD_TIMEOUT_MS);

    if (ret != kNoErr)
    {
        gatt_loge("wait set adv param err %d", ret);
        return -1;
    }

    ret =dm_gatts_set_advertising_data(adv_name);
    if (ret != kNoErr)
    {
        gatt_loge("set_advertising_data err %d", ret);
        return -1;
    }

    ret =dm_gatts_enable_adv(1);
    if (ret != kNoErr)
    {
        gatt_loge("enable_adv err %d", ret);
        return -1;
    }
    
    s_dm_gatts_is_init = 1;
    return 0;
}







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
#include <stdbool.h>
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
#include "bk_cli.h"

#define CLI_TEST             1
#define DYNAMIC_ADD_ATTR 0
#define SPECIAL_HANDLE 15 
#define INVALID_ATTR_HANDLE 0
#define MIN_VALUE(x, y) (((x) < (y)) ? (x): (y))
#define AT_SYNC_CMD_TIMEOUT_MS          4000
#define AUTO_GATTC_TEST     1
#define AUTO_DISCOVER 1
#define AUTO_MTU_REQ 1
#define AUTO_SCAN_INTERVAL   0x80
#define AUTO_SCAN_WINDOW     0x30


enum
{
    GATTC_STATUS_IDLE,
    GATTC_STATUS_DISCOVERY_SELF,
    GATTC_STATUS_READ_BY_TYPE,
    GATTC_STATUS_READ_CHAR,
    GATTC_STATUS_READ_CHAR_DESC,
    GATTC_STATUS_READ_MULTI,
    GATTC_STATUS_WRITE_DESC_NEED_RSP,
    GATTC_STATUS_WRITE_DESC_NO_RSP,
    GATTC_STATUS_PREP_WRITE_STEP_1,
    GATTC_STATUS_PREP_WRITE_STEP_2,
    GATTC_STATUS_WRITE_EXEC,
    GATTC_STATUS_WRITE_READ_SAMETIME,
};


#define dm_gattc_app_env_t dm_gatt_demo_app_env_t

static ble_device_info_t g_peer_dev;
static bk_gatt_if_t s_gattc_if;
static beken_semaphore_t s_ble_sema = NULL;
static beken_semaphore_t s_ble_connect_sem = NULL;
static uint8_t s_dm_gattc_is_init;
static uint8_t s_dm_gattc_local_addr_is_public = 1;
static uint8_t s_is_connect_pending;
static dm_ble_gattc_app_cb s_gattc_cb_list[2];
static uint8_t ble_get_device_name_from_adv(uint8_t *p_buf, uint8_t data_len, uint8_t *dev_name, uint8_t *in_out_len);
static uint8_t ble_check_device_name(uint8_t *p_buf, uint8_t data_len, ble_device_name_t *dev_name);

#if CLI_TEST
static int dm_gattc_cli_init(void);
#endif

static int32_t dm_ble_gattc_private_cb(bk_gattc_cb_event_t event, bk_gatt_if_t gattc_if, bk_ble_gattc_cb_param_t *param)
{
    int32_t ret=0;

    for (int i = 0; i < sizeof(s_gattc_cb_list) / sizeof(s_gattc_cb_list[0]); ++i)
    {
        if (s_gattc_cb_list[i])
        {
            ret = s_gattc_cb_list[i](event, gattc_if, param);
        }
    }
    return ret;
}

static int32_t dm_ble_gap_cb(bk_ble_gap_cb_event_t event, bk_ble_gap_cb_param_t *param)
{
    gatt_logi("event %d", event);

    switch (event)
    {
    case BK_BLE_GAP_SET_STATIC_RAND_ADDR_EVT:
    {
        struct ble_set_rand_cmpl_evt_param *pm = (typeof(pm))param;

        if (pm->status)
        {
            gatt_loge("set rand addr err %d", pm->status);
        }

        if (s_ble_sema != NULL)
        {
            rtos_set_semaphore( &s_ble_sema );
        }
    }
    break;

    case BK_BLE_GAP_CONNECT_COMPLETE_EVT:
    {
        struct ble_connect_complete_param *evt = (typeof(evt))param;
        gatt_logi("BK_BLE_GAP_CONNECT_COMPLETE_EVT %02x:%02x:%02x:%02x:%02x:%02x status 0x%x role %d hci_handle 0x%x",
                  evt->remote_bda[5],
                  evt->remote_bda[4],
                  evt->remote_bda[3],
                  evt->remote_bda[2],
                  evt->remote_bda[1],
                  evt->remote_bda[0],
                  evt->status,
                  evt->link_role,
                  evt->hci_handle
                 );
        s_is_connect_pending = 0;
    }
    break;
    case BK_BLE_GAP_DISCONNECT_COMPLETE_EVT:
    {
        struct ble_connect_complete_param *evt = (typeof(evt))param;
        bk_ble_gap_start_scan(0,200); /* 0 = 一直扫描 */
    }
    break;

    case BK_BLE_GAP_CONNECT_CANCEL_EVT:
    {
        struct ble_connect_cancel_param *pm = (typeof(pm))param;

        if (pm->status)
        {
            gatt_loge("cancel connect err %d", pm->status);
        }

        s_is_connect_pending = 0;

        if (s_ble_sema != NULL)
        {
            rtos_set_semaphore( &s_ble_sema );
        }
    }
    break;
    case BK_BLE_GAP_EXT_SCAN_PARAMS_SET_COMPLETE_EVT:
    {
        struct ble_scan_params_set_cmpl_param *pm = (typeof(pm))param;

        if (pm->status)
        {
            gatt_logw("set scan param err 0x%x", pm->status);
        }

        if (s_ble_sema != NULL)
        {
            rtos_set_semaphore(&s_ble_sema);
        }
    }
    break;
    case BK_BLE_GAP_EXT_SCAN_START_COMPLETE_EVT:
    {
        struct ble_scan_start_cmpl_param *pm = (typeof(pm))param;

        if (pm->status)
        {
            gatt_logw("set scan start err 0x%x", pm->status);
        }

        if (s_ble_sema != NULL)
        {
            rtos_set_semaphore(&s_ble_sema);
        }
    }
    break;
    case BK_BLE_GAP_EXT_ADV_REPORT_EVT:
    {
        struct ble_ext_adv_report_param *pm = (typeof(pm))param;
        char printf_buff[128] = {0};
        uint32_t index = 0;
        char dname[64] = { 0 };
        uint8_t dname_len = sizeof(dname);
        uint8_t found_name = 0;

        found_name = ble_get_device_name_from_adv(pm->params.adv_data, pm->params.adv_data_len, (uint8_t *) dname, &dname_len);
        dname[sizeof(dname) - 1] = 0;

        switch(pm->params.event_type)
        {
        case BK_BLE_ADV_REPORT_EXT_ADV_IND:
            index += os_snprintf(printf_buff + index, sizeof(printf_buff) - index, "EXT_ADV");
            break;

        case BK_BLE_ADV_REPORT_EXT_SCAN_IND:
            index += os_snprintf(printf_buff + index, sizeof(printf_buff) - index, "EXT_SCAN_IND");
            break;

        case BK_BLE_ADV_REPORT_EXT_DIRECT_ADV:
            index += os_snprintf(printf_buff + index, sizeof(printf_buff) - index, "EXT_DIR");
            break;

        case BK_BLE_ADV_REPORT_EXT_SCAN_RSP:
            index += os_snprintf(printf_buff + index, sizeof(printf_buff) - index, "EXT_SCAN_RSP");
            break;

        case BK_BLE_LEGACY_ADV_TYPE_IND:
            index += os_snprintf(printf_buff + index, sizeof(printf_buff) - index, "ADV_IND");
            break;

        case BK_BLE_LEGACY_ADV_TYPE_DIRECT_IND:
            index += os_snprintf(printf_buff + index, sizeof(printf_buff) - index, "ADV_DIRECT_IND");
            break;

        case BK_BLE_LEGACY_ADV_TYPE_SCAN_IND:
            index += os_snprintf(printf_buff + index, sizeof(printf_buff) - index, "ADV_SCAN_IND");
            break;

        case BK_BLE_LEGACY_ADV_TYPE_NONCON_IND:
            index += os_snprintf(printf_buff + index, sizeof(printf_buff) - index, "ADV_NONCONN_IND");
            break;

        case BK_BLE_LEGACY_ADV_TYPE_SCAN_RSP_TO_ADV_IND:
        case BK_BLE_LEGACY_ADV_TYPE_SCAN_RSP_TO_ADV_SCAN_IND:
            index += os_snprintf(printf_buff + index, sizeof(printf_buff) - index, "ADV_SCAN_RSP");
            break;

        default:
            index += os_snprintf(printf_buff + index, sizeof(printf_buff) - index, "UNKNOW_ADV");
            break;
        }

        index += os_snprintf(printf_buff + index, sizeof(printf_buff) - index,
                             " addr_type:%d, adv_addr:%02x:%02x:%02x:%02x:%02x:%02x",
                             pm->params.addr_type,
                             pm->params.addr[0],
                             pm->params.addr[1],
                             pm->params.addr[2],
                             pm->params.addr[3],
                             pm->params.addr[4],
                             pm->params.addr[5]);

        if(found_name)
        {
            index += os_snprintf(printf_buff + index, sizeof(printf_buff) - index, " name:%s", dname);
        }

        printf_buff[sizeof(printf_buff) - 1] = 0;
        gatt_logi("%s\n", printf_buff);

        if ( (ble_check_device_name(pm->params.adv_data, pm->params.adv_data_len, &(g_peer_dev.dev))))
        {
            gatt_logi("dev : %s is discovered\r\n", g_peer_dev.dev.name);

            os_memcpy(g_peer_dev.bdaddr.addr, pm->params.addr, BK_BLE_GAP_BD_ADDR_LEN);
            g_peer_dev.addr_type = pm->params.addr_type;
            //g_peer_dev.state = STATE_DISCOVERED;

            bk_ble_gap_stop_scan();
        }
    }
    break;
    case BK_BLE_GAP_EXT_SCAN_STOP_COMPLETE_EVT:
    {
        dm_gattc_connect(g_peer_dev.bdaddr.addr,g_peer_dev.addr_type,200);
    }
    break;

    default:
        return DM_BLE_GAP_APP_CB_RET_NO_INTERESTING;
        break;
    }

    return DM_BLE_GAP_APP_CB_RET_PROCESSED;
}

static int32_t bk_gattc_cb (bk_gattc_cb_event_t event, bk_gatt_if_t gattc_if, bk_ble_gattc_cb_param_t *comm_param)
{
    ble_err_t ret = 0;
    bk_gatt_auth_req_t auth_req = BK_GATT_AUTH_REQ_NONE;
    dm_gattc_app_env_t *app_env_tmp = NULL;
    dm_gatt_app_env_t *common_env_tmp = NULL;

    uint16_t client_config_noti_indic_enable =0; 
    const uint16_t client_config_all_disable = 0;
    #if AUTO_GATTC_TEST
    const uint16_t client_config_noti_enable = 1;
    #endif

    gatt_logi("~~event %d", event);
    switch (event)
    {
    case BK_GATTC_REG_EVT:
    {
        struct gattc_reg_evt_param *param = (typeof(param))comm_param;

        s_gattc_if = param->gatt_if;
        gatt_logi("reg ret gatt_if %d", s_gattc_if);

        if (s_ble_sema != NULL)
        {
            rtos_set_semaphore( &s_ble_sema );
        }
    }
    break;

    case BK_GATTC_UNREG_EVT:
    {
        gatt_logi("BK_GATTC_UNREG_EVT");

        if (s_ble_sema != NULL)
        {
            rtos_set_semaphore( &s_ble_sema );
        }
    }
    break;

    case BK_GATTC_DIS_SRVC_CMPL_EVT:
    {
        struct gattc_dis_srvc_cmpl_evt_param *param = (typeof(param))comm_param;

        gatt_logi("BK_GATTC_DIS_SRVC_CMPL_EVT %d %d", param->status, param->conn_id);

        common_env_tmp = dm_ble_find_app_env_by_conn_id(param->conn_id);

        if (!common_env_tmp || !common_env_tmp->data)
        {
            gatt_loge("conn_id %d not found %d %p", param->conn_id, common_env_tmp->data);
            break;
        }

        app_env_tmp = (typeof(app_env_tmp))common_env_tmp->data;

        gatt_logi("job_status %d", app_env_tmp->job_status);
#if AUTO_GATTC_TEST
        bk_bt_uuid_t uuid = {BK_UUID_LEN_16, {BK_GATT_UUID_CHAR_DECLARE}};
        if (app_env_tmp->job_status == GATTC_STATUS_IDLE)
        {
            if (0 != bk_ble_gattc_discover(s_gattc_if, param->conn_id, auth_req))
            {
                gatt_loge("bk_ble_gattc_discover err");
            }
            else
            {
                app_env_tmp->job_status = GATTC_STATUS_DISCOVERY_SELF;
            }
        }
        else if (app_env_tmp->job_status == GATTC_STATUS_DISCOVERY_SELF)
        {
            if (0 != bk_ble_gattc_read_by_type(s_gattc_if, param->conn_id, 1, 98, &uuid, auth_req))
            {
                gatt_loge("bk_ble_gattc_read_by_type err");
            }
            else
            {
                app_env_tmp->job_status = GATTC_STATUS_READ_BY_TYPE;
            }
        }
#endif
    }
    break;

    case BK_GATTC_DIS_RES_SERVICE_EVT:
    {
        struct gattc_dis_res_service_evt_param *param = (typeof(param))comm_param;
        uint32_t short_uuid = 0;

        gatt_logi("BK_GATTC_DIS_RES_SERVICE_EVT count %d", param->count);

        for (int i = 0; i < param->count; ++i)
        {
            switch (param->array[i].srvc_id.uuid.len)
            {
            case BK_UUID_LEN_16:
            {
                short_uuid = param->array[i].srvc_id.uuid.uuid.uuid16;
            }
            break;

            case BK_UUID_LEN_128:
            {
                memcpy(&short_uuid, &param->array[i].srvc_id.uuid.uuid.uuid128[BK_UUID_LEN_128 - 4], sizeof(short_uuid));
            }
            break;
            }

            gatt_logi("0x%04x uuid len %d %d~%d", short_uuid, param->array[i].srvc_id.uuid.len, param->array[i].start_handle, param->array[i].end_handle);

            common_env_tmp = dm_ble_find_app_env_by_conn_id(param->conn_id);

            if (!common_env_tmp || !common_env_tmp->data)
            {
                gatt_loge("conn_id %d not found %d %p", param->conn_id, common_env_tmp->data);
                break;
            }

            app_env_tmp = (typeof(app_env_tmp))common_env_tmp->data;

            if (INTERESTING_SERIVCE_UUID == short_uuid)
            {
                app_env_tmp->peer_interest_service_start_handle = param->array[i].start_handle;
                app_env_tmp->peer_interest_service_end_handle = param->array[i].end_handle;

                gatt_logi("interesting service 0x%04x %d~%d", short_uuid, param->array[i].start_handle, param->array[i].end_handle);
            }

            if (BK_GATT_UUID_GAP_SVC == short_uuid)
            {
                app_env_tmp->peer_gap_service_start_handle = param->array[i].start_handle;
                app_env_tmp->peer_gap_service_end_handle = param->array[i].end_handle;

                gatt_logi("gap service %d~%d", param->array[i].start_handle, param->array[i].end_handle);
            }

            if (BK_GATT_UUID_GATT_SVC == short_uuid)
            {
                gatt_logi("gatt service %d~%d", param->array[i].start_handle, param->array[i].end_handle);
            }
        }
    }
    break;

    case BK_GATTC_DIS_RES_CHAR_EVT:
    {
        struct gattc_dis_res_char_evt_param *param = (typeof(param))comm_param;
        uint32_t short_uuid = 0;

        gatt_logi("BK_GATTC_DIS_RES_CHAR_EVT count %d", param->count);

        for (int i = 0; i < param->count; ++i)
        {
            switch (param->array[i].uuid.uuid.len)
            {
            case BK_UUID_LEN_16:
            {
                short_uuid = param->array[i].uuid.uuid.uuid.uuid16;
            }
            break;

            case BK_UUID_LEN_128:
            {
                memcpy(&short_uuid, &param->array[i].uuid.uuid.uuid.uuid128[BK_UUID_LEN_128 - 4], sizeof(short_uuid));
            }
            break;
            }

            gatt_logi("0x%04x uuid len %d %d~%d char_value_handle %d,property=%x", short_uuid, param->array[i].uuid.uuid.len, param->array[i].start_handle,
                     param->array[i].end_handle, param->array[i].char_value_handle,param->array[i].prop);

            common_env_tmp = dm_ble_find_app_env_by_conn_id(param->conn_id);

            if (!common_env_tmp || !common_env_tmp->data)
            {
                gatt_loge("conn_id %d not found %d %p", param->conn_id, common_env_tmp->data);
                break;
            }

            app_env_tmp = (typeof(app_env_tmp))common_env_tmp->data;
            if(param->array[i].prop&(PROPERTIES_NOTIFY|PROPERTIES_INDICATE))
            {   
                if (app_env_tmp->notify_indication_valid_cnt < GATT_MAX_PROFILE_COUNT)
                {
                    app_env_tmp->peer_cproperty_notify_indication[app_env_tmp->notify_indication_valid_cnt++]=param->array[i].prop;
                    gatt_logi("notify cproperty=%x\n",param->array[i].prop);
                }
            }
            else if(param->array[i].prop&(PROPERTIES_WRITE_NO_RESPONSE|PROPERTIES_WRITE))
            {   
                if (app_env_tmp->write_valid_cnt < GATT_MAX_PROFILE_COUNT&&short_uuid!=BK_GATT_UUID_GATT_CLI_SUPPORT_FEAT)
                {
                    app_env_tmp->peer_cproperty_write[app_env_tmp->write_valid_cnt]=param->array[i].prop;
                    app_env_tmp->peer_write_handle[app_env_tmp->write_valid_cnt] = param->array[i].char_value_handle;
                    app_env_tmp->write_valid_cnt++;
                    gatt_logi("write_handle=%x\n",param->array[i].char_value_handle);
                }
            }

            if ( INTERESTING_CHAR_UUID == short_uuid &&
                    app_env_tmp->peer_interest_service_start_handle <= param->array[i].char_value_handle &&
                    app_env_tmp->peer_interest_service_end_handle >= param->array[i].char_value_handle
               )
            {
                app_env_tmp->peer_interest_char_handle = param->array[i].char_value_handle;

                gatt_logi("interesting char 0x%04x %d", short_uuid, param->array[i].char_value_handle);
            }

            if ( app_env_tmp->peer_gap_service_start_handle <= param->array[i].char_value_handle &&
                    app_env_tmp->peer_gap_service_end_handle >= param->array[i].char_value_handle
               )
            {
                switch (short_uuid)
                {
                case BK_GATT_UUID_GAP_DEVICE_NAME:
                    gatt_logi("device name value attr handle 0x%04x", param->array[i].char_value_handle);
                    break;

                case BK_GATT_UUID_GAP_CENTRAL_ADDR_RESOL:
                    gatt_logi("central addr resolvable attr handle 0x%04x", param->array[i].char_value_handle);
                    break;

                case BK_GATT_UUID_GAP_RESOLV_RPVIATE_ADDR_ONLY:
                    gatt_logi("peer resolve RPA only !!!");
                    break;

                default:
                    break;
                }
            }
        }
    }
    break;

    case BK_GATTC_DIS_RES_CHAR_DESC_EVT:
    {
        struct gattc_dis_res_char_desc_evt_param *param = (typeof(param))comm_param;
        uint32_t short_uuid = 0;

        gatt_logi("BK_GATTC_DIS_RES_CHAR_DESC_EVT count %d", param->count);

        for (int i = 0; i < param->count; ++i)
        {
            switch (param->array[i].uuid.uuid.len)
            {
            case BK_UUID_LEN_16:
            {
                short_uuid = param->array[i].uuid.uuid.uuid.uuid16;
            }
            break;

            case BK_UUID_LEN_128:
            {
                memcpy(&short_uuid, &param->array[i].uuid.uuid.uuid.uuid128[BK_UUID_LEN_128 - 4], sizeof(short_uuid));
            }
            break;

            default:
                gatt_loge("unknow uuid len %d", param->array[i].uuid.uuid.len);
                break;
            }

            gatt_logi("0x%04x uuid len %d char_handle %d desc_handle %d", short_uuid, param->array[i].uuid.uuid.len, param->array[i].char_handle, param->array[i].desc_handle);

            common_env_tmp = dm_ble_find_app_env_by_conn_id(param->conn_id);

            if (!common_env_tmp || !common_env_tmp->data)
            {
                gatt_loge("conn_id %d not found %d %p", param->conn_id, common_env_tmp->data);
                break;
            }

            app_env_tmp = (typeof(app_env_tmp))common_env_tmp->data;

            if (BK_GATT_UUID_CHAR_CLIENT_CONFIG == short_uuid)
            {
                gatt_logi("interesting char desc 0x%04x %d,%d", short_uuid, param->array[i].desc_handle,i);

                if(i<GATT_MAX_PROFILE_COUNT)
                    app_env_tmp->peer_interest_char_desc_handle[i] = param->array[i].desc_handle;
            }
        }
    }
    break;

    case BK_GATTC_READ_CHAR_EVT:
    {
        struct gattc_read_char_evt_param *param = (typeof(param))comm_param;

        gatt_logi("BK_GATTC_READ_CHAR_EVT 0x%x %d %d", param->status, param->handle, param->value_len);

        common_env_tmp = dm_ble_find_app_env_by_conn_id(param->conn_id);

        if (!common_env_tmp || !common_env_tmp->data)
        {
            gatt_loge("conn_id %d not found %d %p", param->conn_id, common_env_tmp->data);
            break;
        }

        app_env_tmp = (typeof(app_env_tmp))common_env_tmp->data;
#if AUTO_GATTC_TEST

        if (!param->status && app_env_tmp->job_status == GATTC_STATUS_READ_CHAR)
        {
            if (0 != bk_ble_gattc_read_char_descr(s_gattc_if, param->conn_id, SPECIAL_HANDLE, auth_req))
            {
                gatt_loge("bk_ble_gattc_read_char_descr err");
            }
            else
            {
                app_env_tmp->job_status = GATTC_STATUS_READ_CHAR_DESC;
            }
        }

#endif

        if (common_env_tmp->client_sem)
        {
            app_env_tmp->write_read_status = param->status;

            if (!param->status)
            {
                app_env_tmp->read_buff = os_malloc(param->value_len);

                if (!app_env_tmp->read_buff)
                {
                    gatt_loge("alloc read buffer err");
                    param->status = BK_GATT_INSUF_RESOURCE;
                }
                else
                {
                    app_env_tmp->read_buff_len = param->value_len;
                    os_memcpy(app_env_tmp->read_buff, param->value, param->value_len);
                }
            }

            rtos_set_semaphore(&common_env_tmp->client_sem);
        }
    }
    break;

    case BK_GATTC_READ_DESCR_EVT:
    {
        struct gattc_read_char_evt_param *param = (typeof(param))comm_param;

        gatt_logi("BK_GATTC_READ_DESCR_EVT %x %d %d", param->status, param->handle, param->value_len);

        common_env_tmp = dm_ble_find_app_env_by_conn_id(param->conn_id);

        if (!common_env_tmp || !common_env_tmp->data)
        {
            gatt_loge("conn_id %d not found %d %p", param->conn_id, common_env_tmp->data);
            break;
        }

        app_env_tmp = (typeof(app_env_tmp))common_env_tmp->data;
#if AUTO_GATTC_TEST

        bk_gattc_multi_t multi;

        memset(&multi, 0, sizeof(multi));

        multi.num_attr = 5;
        multi.handles[0] = 3;
        multi.handles[1] = 5;
        multi.handles[2] = 9;
        multi.handles[3] = 13;
        multi.handles[4] = 16;

        if (app_env_tmp->job_status == GATTC_STATUS_READ_CHAR_DESC)
        {
            if (0 != bk_ble_gattc_read_multiple(s_gattc_if, param->conn_id, &multi, auth_req))
            {
                gatt_loge("bk_ble_gattc_read_multiple err");
            }
            else
            {
                app_env_tmp->job_status = GATTC_STATUS_READ_MULTI;
            }
        }

#endif

        if (common_env_tmp->client_sem)
        {
            app_env_tmp->write_read_status = param->status;

            if (!param->status)
            {
                app_env_tmp->read_buff = os_malloc(param->value_len);

                if (!app_env_tmp->read_buff)
                {
                    gatt_loge("alloc read buffer err");
                    param->status = BK_GATT_INSUF_RESOURCE;
                }
                else
                {
                    os_memcpy(app_env_tmp->read_buff, param->value, param->value_len);
                }
            }

            rtos_set_semaphore(&common_env_tmp->client_sem);
        }
    }
    break;

    case BK_GATTC_READ_BY_TYPE_EVT:
    {
        struct gattc_read_by_type_evt_param *param = (typeof(param))comm_param;

        gatt_logi("BK_GATTC_READ_BY_TYPE_EVT %d %d %d", param->status, param->conn_id, param->elem_count);

        common_env_tmp = dm_ble_find_app_env_by_conn_id(param->conn_id);

        if (!common_env_tmp || !common_env_tmp->data)
        {
            gatt_loge("conn_id %d not found %d %p", param->conn_id, common_env_tmp->data);
            break;
        }

        app_env_tmp = (typeof(app_env_tmp))common_env_tmp->data;
#if AUTO_GATTC_TEST

        if (app_env_tmp->job_status == GATTC_STATUS_READ_BY_TYPE)
        {
            if (0 != bk_ble_gattc_read_char(s_gattc_if, param->conn_id, 3, auth_req))
            {
                gatt_loge("bk_ble_gattc_read_char err");
            }
            else
            {
                app_env_tmp->job_status = GATTC_STATUS_READ_CHAR;
            }
        }

#endif
    }
    break;

    case BK_GATTC_READ_MULTIPLE_EVT:
    {
        struct gattc_read_char_evt_param *param = (typeof(param))comm_param;

        gatt_logi("BK_GATTC_READ_MULTIPLE_EVT %x %d %d", param->status, param->handle, param->value_len);

        common_env_tmp = dm_ble_find_app_env_by_conn_id(param->conn_id);

        if (!common_env_tmp || !common_env_tmp->data)
        {
            gatt_loge("conn_id %d not found %d %p", param->conn_id, common_env_tmp->data);
            break;
        }

        app_env_tmp = (typeof(app_env_tmp))common_env_tmp->data;
#if AUTO_GATTC_TEST

        if (app_env_tmp->job_status == GATTC_STATUS_READ_MULTI)
        {
            gatt_logi("peer_interest_char_desc_handle %x \n", app_env_tmp->peer_interest_char_desc_handle[app_env_tmp->peer_desc_cnt]);
            
            if(app_env_tmp->peer_cproperty_notify_indication[app_env_tmp->peer_desc_cnt]&PROPERTIES_NOTIFY)
                client_config_noti_indic_enable=0x01;
            else if(app_env_tmp->peer_cproperty_notify_indication[app_env_tmp->peer_desc_cnt]&PROPERTIES_INDICATE)
                client_config_noti_indic_enable=0x02;
            else
                client_config_noti_indic_enable=0x00;
            
            if (0 != bk_ble_gattc_write_char_descr(s_gattc_if, param->conn_id, app_env_tmp->peer_interest_char_desc_handle[app_env_tmp->peer_desc_cnt], 
                sizeof(client_config_noti_indic_enable), (uint8_t *)&client_config_noti_indic_enable, BK_GATT_WRITE_TYPE_RSP, auth_req))
            {
                gatt_loge("bk_ble_gattc_write_char_descr err");
            }
            else
            {
                app_env_tmp->job_status = GATTC_STATUS_WRITE_DESC_NEED_RSP;
            }
        }

#endif
    }
    break;

    case BK_GATTC_WRITE_CHAR_EVT:
    {
        struct gattc_write_evt_param *param = (typeof(param))comm_param;

        gatt_logi("BK_GATTC_WRITE_CHAR_EVT %d %d %d %d", param->status, param->conn_id, param->handle, param->offset);

        if (param->status == BK_GATT_INSUF_AUTHENTICATION)
        {
            //we need create bond
            gatt_logw("status insufficient authentication, need bond !!!");
        }

        common_env_tmp = dm_ble_find_app_env_by_conn_id(param->conn_id);

        if (!common_env_tmp || !common_env_tmp->data)
        {
            gatt_loge("conn_id %d not found %d %p", param->conn_id, common_env_tmp->data);
            break;
        }

        app_env_tmp = (typeof(app_env_tmp))common_env_tmp->data;

        if (common_env_tmp->client_sem)
        {
            app_env_tmp->write_read_status = param->status;
            rtos_set_semaphore(&common_env_tmp->client_sem);
        }
    }
    break;

    case BK_GATTC_WRITE_DESCR_EVT:
    {
        struct gattc_write_evt_param *param = (typeof(param))comm_param;

        gatt_logi("BK_GATTC_WRITE_DESCR_EVT %d %d %d %d", param->status, param->conn_id, param->handle, param->offset);

        if (param->status == BK_GATT_INSUF_AUTHENTICATION)
        {
            //we need create bond
            gatt_logw("status insufficient authentication, need bond !!!");
        }

        common_env_tmp = dm_ble_find_app_env_by_conn_id(param->conn_id);

        if (!common_env_tmp || !common_env_tmp->data)
        {
            gatt_loge("conn_id %d not found %d %p", param->conn_id, common_env_tmp->data);
            break;
        }

        app_env_tmp = (typeof(app_env_tmp))common_env_tmp->data;

        app_env_tmp->peer_desc_cnt++;
        if (app_env_tmp->job_status == GATTC_STATUS_WRITE_DESC_NEED_RSP && app_env_tmp->peer_desc_cnt<app_env_tmp->notify_indication_valid_cnt)
        {
            if(app_env_tmp->peer_cproperty_notify_indication[app_env_tmp->peer_desc_cnt]&PROPERTIES_NOTIFY)
                client_config_noti_indic_enable=0x01;
            else if(app_env_tmp->peer_cproperty_notify_indication[app_env_tmp->peer_desc_cnt]&PROPERTIES_INDICATE)
                client_config_noti_indic_enable=0x02;
            else
                client_config_noti_indic_enable=0;

            if (0 != bk_ble_gattc_write_char_descr(s_gattc_if, param->conn_id, app_env_tmp->peer_interest_char_desc_handle[app_env_tmp->peer_desc_cnt], 
                sizeof(client_config_all_disable), (uint8_t *)&client_config_noti_indic_enable, BK_GATT_WRITE_TYPE_RSP, auth_req))
            {
                gatt_loge("bk_ble_gattc_write_char_descr err");
            }
            else
            {
                app_env_tmp->job_status = GATTC_STATUS_WRITE_DESC_NEED_RSP;
            }
        }
#if AUTO_GATTC_TEST
        else if (app_env_tmp->job_status == GATTC_STATUS_WRITE_DESC_NO_RSP)
        {
            uint8_t buff = 1;

            if (0 != bk_ble_gattc_prepare_write(s_gattc_if, param->conn_id, SPECIAL_HANDLE, 0, sizeof(buff), (uint8_t *)&buff, auth_req))
            {
                gatt_loge("bk_ble_gattc_prepare_write err");
            }
            else
            {
                app_env_tmp->job_status = GATTC_STATUS_PREP_WRITE_STEP_1;
            }
        }
#endif

        if (common_env_tmp->client_sem)
        {
            app_env_tmp->write_read_status = param->status;
            rtos_set_semaphore(&common_env_tmp->client_sem);
        }
    }
    break;

    case BK_GATTC_PREP_WRITE_EVT:
    {
        struct gattc_write_evt_param *param = (typeof(param))comm_param;

        gatt_logi("BK_GATTC_PREP_WRITE_EVT %d %d %d %d", param->status, param->conn_id, param->handle, param->offset);

        common_env_tmp = dm_ble_find_app_env_by_conn_id(param->conn_id);

        if (!common_env_tmp || !common_env_tmp->data)
        {
            gatt_loge("conn_id %d not found %d %p", param->conn_id, common_env_tmp->data);
            break;
        }

        app_env_tmp = (typeof(app_env_tmp))common_env_tmp->data;
#if AUTO_GATTC_TEST

        if (app_env_tmp->job_status == GATTC_STATUS_PREP_WRITE_STEP_1)
        {
            uint8_t buff = 0;

            if (0 != bk_ble_gattc_prepare_write(s_gattc_if, param->conn_id, SPECIAL_HANDLE, 1, /*400,*/sizeof(buff), (uint8_t *)&buff, auth_req))
            {
                gatt_loge("bk_ble_gattc_prepare_write err");
            }
            else
            {
                app_env_tmp->job_status = GATTC_STATUS_PREP_WRITE_STEP_2;
            }
        }
        else if (app_env_tmp->job_status == GATTC_STATUS_PREP_WRITE_STEP_2)
        {
            if (0 != bk_ble_gattc_execute_write(s_gattc_if, param->conn_id, 1))
            {
                gatt_loge("bk_ble_gattc_execute_write err");
            }
            else
            {
                app_env_tmp->job_status = GATTC_STATUS_WRITE_EXEC;
            }
        }

#endif

        if (common_env_tmp->client_sem)
        {
            rtos_set_semaphore(&common_env_tmp->client_sem);
        }
    }
    break;

    case BK_GATTC_EXEC_EVT:
    {
        struct gattc_exec_cmpl_evt_param *param = (typeof(param))comm_param;

        gatt_logi("BK_GATTC_EXEC_EVT %d %d", param->status, param->conn_id);

        common_env_tmp = dm_ble_find_app_env_by_conn_id(param->conn_id);

        if (!common_env_tmp || !common_env_tmp->data)
        {
            gatt_loge("conn_id %d not found %d %p", param->conn_id, common_env_tmp->data);
            break;
        }

        app_env_tmp = (typeof(app_env_tmp))common_env_tmp->data;
#if AUTO_GATTC_TEST

        if (app_env_tmp->job_status == GATTC_STATUS_WRITE_EXEC)
        {
            //consecutive write read test
            if (0 != bk_ble_gattc_write_char_descr(s_gattc_if, param->conn_id, SPECIAL_HANDLE, sizeof(client_config_noti_enable), (uint8_t *)&client_config_noti_enable, BK_GATT_WRITE_TYPE_RSP, auth_req))
            {
                gatt_loge("bk_ble_gattc_write_char_descr 1 err");
            }
            else if (0 != bk_ble_gattc_read_char_descr(s_gattc_if, param->conn_id, SPECIAL_HANDLE, auth_req))
            {
                gatt_loge("bk_ble_gattc_read_char_descr 2 err");
            }
            else if (0 != bk_ble_gattc_read_char_descr(s_gattc_if, param->conn_id, SPECIAL_HANDLE, auth_req))
            {
                gatt_loge("bk_ble_gattc_read_char_descr 3 err");
            }
            else if (0 != bk_ble_gattc_write_char_descr(s_gattc_if, param->conn_id, SPECIAL_HANDLE, sizeof(client_config_all_disable), (uint8_t *)&client_config_all_disable, BK_GATT_WRITE_TYPE_RSP, auth_req))
            {
                gatt_loge("bk_ble_gattc_write_char_descr 4 err");
            }
            else if (0 != bk_ble_gattc_write_char_descr(s_gattc_if, param->conn_id, SPECIAL_HANDLE, sizeof(client_config_noti_enable), (uint8_t *)&client_config_noti_enable, BK_GATT_WRITE_TYPE_NO_RSP, auth_req))
            {
                gatt_loge("bk_ble_gattc_write_char_descr 5 err");
            }
            else
            {
                app_env_tmp->job_status = GATTC_STATUS_WRITE_READ_SAMETIME;
            }
        }
#endif
    }
    break;

    case BK_GATTC_NOTIFY_EVT:
    {
        struct gattc_notify_evt_param *param = (typeof(param))comm_param;

        gatt_logi("BK_GATTC_NOTIFY_EVT %d %d handle %d vallen %d %02X:%02X:%02X:%02X:%02X:%02X", param->conn_id,
                  param->is_notify,
                  param->handle,
                  param->value_len,
                  param->remote_bda[5],
                  param->remote_bda[4],
                  param->remote_bda[3],
                  param->remote_bda[2],
                  param->remote_bda[1],
                  param->remote_bda[0]);
        for(uint8_t i=0;i<param->value_len;i++)
            os_printf("%x\n",param->value[i]);

        common_env_tmp = dm_ble_find_app_env_by_conn_id(param->conn_id);

        if (!common_env_tmp || !common_env_tmp->data)
        {
            gatt_loge("conn_id %d not found %d %p", param->conn_id, common_env_tmp->data);
            break;
        }
        app_env_tmp = (typeof(app_env_tmp))common_env_tmp->data;
    }
    break;

    case BK_GATTC_CFG_MTU_EVT:
    {
        struct gattc_cfg_mtu_evt_param *param = (typeof(param))comm_param;

        gatt_logi("BK_GATTC_CFG_MTU_EVT status 0x%x %d %d", param->status, param->conn_id, param->mtu);

        common_env_tmp = dm_ble_find_app_env_by_conn_id(param->conn_id);

        if (!common_env_tmp || !common_env_tmp->data)
        {
            gatt_loge("conn_id %d not found %d %p", param->conn_id, common_env_tmp->data);
        }
        else
        {
            app_env_tmp = (typeof(app_env_tmp))common_env_tmp->data;

            if (!app_env_tmp)
            {
                gatt_loge("app_env_tmp not fond");
            }
            else
            {
                app_env_tmp->client_mtu = param->mtu;
            }

            if (common_env_tmp->client_sem)
            {
                app_env_tmp->write_read_status = param->status;
                rtos_set_semaphore(&common_env_tmp->client_sem);
            }
        }

#if AUTO_DISCOVER

        if (0 != bk_ble_gattc_discover(s_gattc_if, param->conn_id, auth_req))
        {
            gatt_loge("bk_ble_gattc_discover err");
        }

#endif
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

        common_env_tmp = dm_ble_find_app_env_by_addr(param->remote_bda);

        if (!common_env_tmp)
        {
            gatt_logw("not found addr, alloc it !!!!");
            common_env_tmp = dm_ble_alloc_app_env_by_addr(param->remote_bda, sizeof(dm_gattc_app_env_t));

            if (!common_env_tmp || !common_env_tmp->data)
            {
                gatt_loge("conn max %p %p !!!!", common_env_tmp, common_env_tmp ? common_env_tmp->data : NULL);
                break;
            }

            if (common_env_tmp->status != GAP_CONNECT_STATUS_IDLE)
            {
                gatt_loge("connect status is not idle %d", common_env_tmp->status);
                break;
            }
        }

        if (!common_env_tmp || !common_env_tmp->data)
        {
            gatt_loge("not found addr or data %p %p !!!!", common_env_tmp, common_env_tmp ? common_env_tmp->data : NULL);
            break;
        }

        common_env_tmp->status = GAP_CONNECT_STATUS_CONNECTED;
        common_env_tmp->conn_id = param->conn_id;
        common_env_tmp->local_is_master = (param->link_role == 0 ? 1 : 0);
        app_env_tmp = (typeof(app_env_tmp))common_env_tmp->data;
        os_memset(app_env_tmp,0,sizeof(dm_gatt_demo_app_env_t));

        gatt_logi("local is master %d", common_env_tmp->local_is_master);

        if (common_env_tmp->local_is_master)
        {
#if AUTO_MTU_REQ

            // only do mtu req when local is master
            if (0 != bk_ble_gattc_send_mtu_req(s_gattc_if, common_env_tmp->conn_id))
            {
                gatt_loge("bk_ble_gattc_send_mtu_req err");
            }

#endif
        }
        else
        {
#if 0//AUTO_DISCOVER

            if (0 != bk_ble_gattc_discover(s_gattc_if, param->conn_id, auth_req))
            {
                gatt_loge("bk_ble_gattc_discover err");
            }

#endif
        }

        if (s_ble_connect_sem)
        {
            rtos_set_semaphore(&s_ble_connect_sem);
        }
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

        common_env_tmp = dm_ble_find_app_env_by_addr(param->remote_bda);

        if (!common_env_tmp || !common_env_tmp->data)
        {
            gatt_loge("not found addr or data %p %p !!!!", common_env_tmp, common_env_tmp ? common_env_tmp->data : NULL);
            break;
        }

        app_env_tmp = (typeof(app_env_tmp))common_env_tmp->data;

        if (app_env_tmp->read_buff)
        {
            os_free(app_env_tmp->read_buff);
            app_env_tmp->read_buff = NULL;
        }

        dm_ble_del_app_env_by_addr(param->remote_bda);

        if (s_ble_connect_sem)
        {
            rtos_set_semaphore(&s_ble_connect_sem);
        }
    }
    break;

    default:
        break;
    }

    return ret;
}

int32_t dm_gattc_connect(uint8_t *addr, uint32_t addr_type, uint32_t s_timeout)
{
    dm_gatt_app_env_t *common_env_tmp = NULL;
    int32_t err = 0;

    gatt_logi("0x%02x:%02x:%02x:%02x:%02x:%02x %d",
              addr[5],
              addr[4],
              addr[3],
              addr[2],
              addr[1],
              addr[0],
              addr_type);

    if (!s_gattc_if)
    {
        gatt_loge("gattc not init");

        return -1;
    }

    if (s_is_connect_pending)
    {
        gatt_loge("connect pending !!!");
        return -1;
    }

    common_env_tmp = dm_ble_alloc_app_env_by_addr(addr, sizeof(dm_gattc_app_env_t));

    if (!common_env_tmp || !common_env_tmp->data)
    {
        gatt_loge("conn max %p %p !!!!", common_env_tmp, common_env_tmp ? common_env_tmp->data : NULL);
        return -1;
    }

    if (common_env_tmp->status != GAP_CONNECT_STATUS_IDLE)
    {
        gatt_loge("connect status is not idle %d", common_env_tmp->status);
        return -1;
    }

    bk_gap_create_conn_params_t param = {0};
    bk_bd_addr_t peer_id_addr = {0};
    bk_ble_addr_type_t peer_id_addr_type = BLE_ADDR_TYPE_PUBLIC;

    param.scan_interval = 800;
    param.scan_window = param.scan_interval / 2;
    param.initiator_filter_policy = 0;

    /* attention: some device could only send rpa adv after pair, some device is opposite.
     * so we need to decide if rpa should be used in connection.
     */

    if (g_dm_gap_use_rpa && 0 == dm_gatt_find_id_info_by_nominal_info(addr, addr_type, peer_id_addr, &peer_id_addr_type))
    {
        gatt_logi("local use rpa");
        param.local_addr_type = (s_dm_gattc_local_addr_is_public ? BLE_ADDR_TYPE_RPA_PUBLIC : BLE_ADDR_TYPE_RPA_RANDOM);
        os_memcpy(param.peer_addr, addr, sizeof(param.peer_addr));
        param.peer_addr_type = addr_type;
    }
    else if (!dm_gatt_find_id_info_by_nominal_info(addr, addr_type, peer_id_addr, &peer_id_addr_type))
    {
        gatt_logi("peer use rpa, so we need use rpa to connect");
        param.local_addr_type = (s_dm_gattc_local_addr_is_public ? BLE_ADDR_TYPE_RPA_PUBLIC : BLE_ADDR_TYPE_RPA_RANDOM);
        os_memcpy(param.peer_addr, addr, sizeof(param.peer_addr));
        param.peer_addr_type = addr_type;
    }
    else
    {
        gatt_logi("don't use rpa");
        param.local_addr_type = (s_dm_gattc_local_addr_is_public ? BLE_ADDR_TYPE_PUBLIC : BLE_ADDR_TYPE_RANDOM);
        os_memcpy(param.peer_addr, addr, sizeof(param.peer_addr));
        param.peer_addr_type = addr_type;
    }

    param.conn_interval_min = 0x20;
    param.conn_interval_max = 0x40;
    param.conn_latency = 0;
    param.supervision_timeout = s_timeout;
    param.min_ce = 0;
    param.max_ce = 0;

    err = bk_ble_gap_connect(&param);

    if (err)
    {
        gatt_loge("connect fail %d", err);
    }
    else
    {
        os_memcpy(common_env_tmp->addr, addr, sizeof(common_env_tmp->addr));
        common_env_tmp->status = GAP_CONNECT_STATUS_CONNECTING;
        s_is_connect_pending = 1;
    }

    return err;
}

int32_t dm_gattc_disconnect(uint8_t *addr)
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

    if (!s_gattc_if)
    {
        gatt_loge("gattc not init");

        return -1;
    }

    common_env_tmp = dm_ble_find_app_env_by_addr(addr);

    if (!common_env_tmp || !common_env_tmp->data)
    {
        gatt_loge("conn not found !!!!");
        return -1;
    }

    if (common_env_tmp->status != GAP_CONNECT_STATUS_CONNECTED)
    {
        gatt_loge("connect status is not connected %d", common_env_tmp->status);
        return -1;
    }

    if (!s_ble_connect_sem)
    {
        err = rtos_init_semaphore(&s_ble_connect_sem, 1);

        if (err)
        {
            gatt_loge("init sem err %d", err);
            err = -1;
            goto end;
        }
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

    err = rtos_get_semaphore(&s_ble_connect_sem, SYNC_CMD_TIMEOUT_MS);

    if (err)
    {
        gatt_loge("rtos_get_semaphore disconnect err %d", err);
        err = -1;
        goto end;
    }

end:;

    if (s_ble_connect_sem)
    {
        if (rtos_deinit_semaphore(&s_ble_connect_sem))
        {
            gatt_loge("rtos_deinit_semaphore s_ble_data_sem err %d", err);
        }

        s_ble_connect_sem = NULL;
    }

    return err;
}

int32_t dm_gattc_connect_cancel(void)
{
    int32_t err = 0;

    if (!s_gattc_if)
    {
        gatt_loge("gattc not init");

        return -1;
    }

    if (!s_is_connect_pending)
    {
        gatt_loge("no need cancel");
        return 0;
    }

    if (!s_ble_connect_sem)
    {
        err = rtos_init_semaphore(&s_ble_connect_sem, 1);

        if (err)
        {
            gatt_loge("init sem err %d", err);
            err = -1;
            goto end;
        }
    }

    err = bk_ble_gap_cancel_connect();

    if (err)
    {
        gatt_loge("cancel fail %d", err);
    }

    err = rtos_get_semaphore(&s_ble_connect_sem, SYNC_CMD_TIMEOUT_MS);

    if (err )
    {
        gatt_loge("rtos_get_semaphore cancel connect err %d", err);
        return -1;
    }

end:;

    if (s_ble_connect_sem)
    {
        if (rtos_deinit_semaphore(&s_ble_connect_sem))
        {
            gatt_loge("rtos_deinit_semaphore s_ble_data_sem err %d", err);
        }

        s_ble_connect_sem = NULL;
    }

    return err;
}

int32_t dm_gattc_discover(uint16_t conn_id)
{
    if (!s_gattc_if)
    {
        gatt_loge("gattc not init");

        return -1;
    }

    if (bk_ble_gattc_discover(s_gattc_if, conn_id, BK_GATT_AUTH_REQ_NONE))
    {
        gatt_loge("err");
    }

    return 0;
}

int32_t dm_gattc_write(uint16_t conn_id, uint16_t attr_handle, uint8_t *data, uint32_t len,uint8_t write_type)
{
    int32_t ret = 0;
    if (!s_gattc_if)
    {
        gatt_loge("gattc not init");

        return -1;
    }
    ret=bk_ble_gattc_write_char_descr(s_gattc_if, conn_id, attr_handle, len, data, write_type, BK_GATT_AUTH_REQ_NONE);

    if (0 != ret)
    {
        gatt_loge("err");
    }

    return 0;
}

int32_t dm_gattc_write_ext(uint16_t gatt_conn_id, uint16_t attr_handle, uint8_t *data, uint32_t len, uint8_t write_req)
{
    int32_t ret = 0;
    dm_gattc_app_env_t *app_env_tmp = NULL;
    dm_gatt_app_env_t *common_env_tmp = NULL;

    if (!s_gattc_if)
    {
        gatt_loge("gattc not init");

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

    if (!common_env_tmp->client_sem)
    {
        ret = rtos_init_semaphore(&common_env_tmp->client_sem, 1);

        if (ret)
        {
            gatt_loge("init sem err %d", ret);
            ret = -1;
            goto end;
        }
    }

    ret = bk_ble_gattc_write_char(s_gattc_if, gatt_conn_id, attr_handle, len, data, write_req ? BK_GATT_WRITE_TYPE_RSP : BK_GATT_WRITE_TYPE_NO_RSP, BK_GATT_AUTH_REQ_NONE);

    if (ret)
    {
        gatt_loge("write err %d", ret);
        ret = -1;
        goto end;
    }

    ret = rtos_get_semaphore(&common_env_tmp->client_sem, SYNC_CMD_TIMEOUT_MS);

    if (ret)
    {
        gatt_loge("wait write err %d", ret);
        ret = -1;
        goto end;
    }

end:;

    if (app_env_tmp)
    {
        ret = app_env_tmp->write_read_status;
        app_env_tmp->write_read_status = 0;
    }

    if (common_env_tmp->client_sem)
    {
        if (rtos_deinit_semaphore(&common_env_tmp->client_sem))
        {
            gatt_loge("rtos_deinit_semaphore s_ble_data_sem err %d", ret);
        }

        common_env_tmp->client_sem = NULL;
    }

    return ret;
}

int32_t dm_gattc_read(uint16_t gatt_conn_id, uint16_t attr_handle, uint8_t *data, uint32_t len)
{
    int32_t ret = 0;
    dm_gattc_app_env_t *app_env_tmp = NULL;
    dm_gatt_app_env_t *common_env_tmp = NULL;

    if (!s_gattc_if)
    {
        gatt_loge("gattc not init");

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

    if (!common_env_tmp->client_sem)
    {
        ret = rtos_init_semaphore(&common_env_tmp->client_sem, 1);

        if (ret)
        {
            gatt_loge("init sem err %d", ret);
            ret = -1;
            goto end;
        }
    }

    ret = bk_ble_gattc_read_char(s_gattc_if, gatt_conn_id, attr_handle, BK_GATT_AUTH_REQ_NONE);

    if (ret)
    {
        gatt_loge("read err %d", ret);
        ret = -1;
        goto end;
    }

    ret = rtos_get_semaphore(&common_env_tmp->client_sem, SYNC_CMD_TIMEOUT_MS);

    if (ret)
    {
        gatt_loge("wait read err %d", ret);
        ret = -1;
        goto end;
    }

end:;

    if(app_env_tmp)
    {
        if (!app_env_tmp->write_read_status)
        {
            os_memcpy(data, app_env_tmp->read_buff, MIN_VALUE(len, app_env_tmp->read_buff_len));

            if (MIN_VALUE(len, app_env_tmp->read_buff_len) != app_env_tmp->read_buff_len)
            {
                gatt_logw("read len %d api len %d", app_env_tmp->read_buff_len, len);
            }
        }
        else
        {
            ret = app_env_tmp->write_read_status;
        }

        app_env_tmp->write_read_status = 0;

        if (app_env_tmp->read_buff)
        {
            os_free(app_env_tmp->read_buff);
            app_env_tmp->read_buff_len = 0;
            app_env_tmp->read_buff = NULL;
            app_env_tmp->write_read_status = 0;
        }
    }

    if (common_env_tmp->client_sem)
    {
        if (rtos_deinit_semaphore(&common_env_tmp->client_sem))
        {
            gatt_loge("rtos_deinit_semaphore s_ble_data_sem err %d", ret);
        }

        common_env_tmp->client_sem = NULL;
    }

    return ret;
}

int32_t dm_gattc_send_mtu_req(uint8_t *mac, uint8_t gatt_conn_id)
{
    int32_t ret = 0;
    dm_gattc_app_env_t *app_env_tmp = NULL;
    dm_gatt_app_env_t *common_env_tmp = NULL;

    if (!s_gattc_if)
    {
        gatt_loge("gattc not init");

        return -1;
    }

    if (mac)
    {
        common_env_tmp = dm_ble_find_app_env_by_addr(mac);
    }
    else
    {
        common_env_tmp = dm_ble_find_app_env_by_conn_id(gatt_conn_id);
    }

    if (!common_env_tmp || !common_env_tmp->data)
    {
        gatt_loge("conn_id %d not found %d %p", gatt_conn_id, common_env_tmp->data);
        ret = -1;
        goto end;
    }

    gatt_conn_id = common_env_tmp->conn_id;

    app_env_tmp = (typeof(app_env_tmp))common_env_tmp->data;

    if (!common_env_tmp->client_sem)
    {
        ret = rtos_init_semaphore(&common_env_tmp->client_sem, 1);

        if (ret)
        {
            gatt_loge("init sem err %d", ret);
            ret = -1;
            goto end;
        }
    }

    ret = bk_ble_gattc_send_mtu_req(s_gattc_if, gatt_conn_id);

    if (ret)
    {
        gatt_loge("send mtu req err %d", ret);
        ret = -1;
        goto end;
    }

    ret = rtos_get_semaphore(&common_env_tmp->client_sem, SYNC_CMD_TIMEOUT_MS);

    if (ret)
    {
        gatt_loge("wait send mtu req err %d", ret);
        ret = -1;
        goto end;
    }

end:;

    if (common_env_tmp->client_sem)
    {
        if (rtos_deinit_semaphore(&common_env_tmp->client_sem))
        {
            gatt_loge("rtos_deinit_semaphore s_ble_data_sem err %d", ret);
        }

        common_env_tmp->client_sem = NULL;
    }

    return ret;
}

int dm_gattc_add_gattc_callback(void *param)
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

static uint8_t ble_get_device_name_from_adv(uint8_t *p_buf, uint8_t data_len, uint8_t *dev_name, uint8_t *in_out_len)
{
    uint8_t *p_data = p_buf;
    uint8_t *p_data_end = p_buf + data_len;

    uint8_t is_found = 0;

    while (p_data < p_data_end)
    {
        if (*(p_data + 1) == 0x09)//GAP_AD_TYPE_COMPLETE_NAME
        {
            is_found = 1;

            os_memcpy(dev_name, p_data + 2, ((p_data[0] - 1 > *in_out_len) ? *in_out_len : p_data[0] - 1));
            break;
        }
        // Go to next advertising info
        p_data += (*p_data + 1);
    }
    return (is_found);
}
static uint8_t ble_check_device_name(uint8_t *p_buf, uint8_t data_len, ble_device_name_t *dev_name)
{
    uint8_t *p_data = p_buf;
    uint8_t *p_data_end = p_buf + data_len;
    uint8_t name_len = 0;

    uint8_t is_found = 0;

    while (p_data < p_data_end)
    {
        if (*(p_data + 1) == 0x09)//GAP_AD_TYPE_COMPLETE_NAME
        {
            name_len = *p_data - 1;
            if ((name_len == dev_name->len) && (!os_memcmp(dev_name->name, p_data + 2, name_len)))
            {
                is_found = 1;
            }
            break;
        }

        // Go to next advertising info
        p_data += (*p_data + 1);
    }

    return (is_found);
}
static void dm_ble_set_scan_param(void)
{
    gatt_logi("%s\n",__func__);
    int err = kNoErr;
    bk_ble_ext_scan_params_t param = {0};
    param.own_addr_type = BLE_ADDR_TYPE_PUBLIC;
    param.cfg_mask = BK_BLE_GAP_EXT_SCAN_CFG_UNCODE_MASK;
    param.uncoded_cfg.scan_interval = 0xc8;
    param.uncoded_cfg.scan_window = 0x10;
    param.uncoded_cfg.scan_type = BLE_SCAN_TYPE_PASSIVE;
    param.coded_cfg.scan_interval = 0;//0x64;
    param.coded_cfg.scan_window = 0;//0x10;
    param.coded_cfg.scan_type = BLE_SCAN_TYPE_PASSIVE;
    param.filter_policy = 0;
    param.scan_duplicate = 1;

    err=bk_ble_gap_set_scan_params(&param);
    if (err != kNoErr)
    {
        gatt_loge("set_scan_params err=%d\n",err);
        return;
    }
    err = rtos_get_semaphore(&s_ble_sema, AT_SYNC_CMD_TIMEOUT_MS);
    if (err != kNoErr)
    {
        gatt_loge("get_semaphore err=%d\n",err);
        return;
    }
}
static void dm_ble_start_scan(void)
{
    int err = kNoErr;
    gatt_logi("%s\n",__func__);
    //s_auto.state = AUTO_SCANNING;
    err=bk_ble_gap_start_scan(0,200); /* 0 = 一直扫描 */
    if (err != kNoErr)
    {
        gatt_loge("set_scan_params err=%d\n",err);
        return;
    }
    err = rtos_get_semaphore(&s_ble_sema, AT_SYNC_CMD_TIMEOUT_MS);
    if (err != kNoErr)
    {
        gatt_loge("get_semaphore err=%d\n",err);
        return;
    }
}

int dm_gattc_init(void)
{
    const uint8_t con_dev_str_name[] ="BK7258_BLE";
    ble_err_t ret = 0;

    ret = rtos_init_semaphore(&s_ble_sema, 1);

    if (ret != 0)
    {
        gatt_loge("rtos_init_semaphore err %d", ret);
        return -1;
    }

    dm_gatt_add_gap_callback(dm_ble_gap_cb);

    bk_ble_gattc_register_callback(dm_ble_gattc_private_cb);
    dm_gattc_add_gattc_callback(bk_gattc_cb);

    ret = bk_ble_gattc_app_register(0);

    if (ret)
    {
        gatt_loge("reg err %d", ret);
        return -1;
    }
    ret = rtos_get_semaphore(&s_ble_sema, SYNC_CMD_TIMEOUT_MS);

    if (ret != kNoErr)
    {
        gatt_loge("rtos_get_semaphore reg err %d", ret);
        return -1;
    }

    dm_ble_set_scan_param();
    dm_ble_start_scan();
    os_memcpy(g_peer_dev.dev.name,con_dev_str_name,sizeof(con_dev_str_name));
    g_peer_dev.dev.len=sizeof(con_dev_str_name)-1;
    #if CLI_TEST
    dm_gattc_cli_init();
    #endif
    s_dm_gattc_is_init = 1;
    return 0;
}

int dm_gattc_deinit(void)
{
    int32_t ret = 0;

    if (!s_dm_gattc_is_init)
    {
        gatt_loge("already deinit");
        return -1;
    }

    ret = bk_ble_gattc_app_unregister(s_gattc_if);
    if (ret)
    {
        gatt_loge("unreg err %d", ret);
    }

    ret = rtos_get_semaphore(&s_ble_sema, SYNC_CMD_TIMEOUT_MS);
    if (ret != kNoErr)
    {
        gatt_loge("rtos_get_semaphore unreg err %d", ret);
    }

    if (s_ble_sema)
    {
        rtos_deinit_semaphore(&s_ble_sema);
        s_ble_sema = NULL;
    }

    if (s_ble_connect_sem)
    {
        rtos_deinit_semaphore(&s_ble_connect_sem);
        s_ble_connect_sem = NULL;
    }

    bk_ble_gattc_register_callback(NULL);

    s_gattc_if = 0;
    s_dm_gattc_local_addr_is_public = 0;
    s_is_connect_pending = 0;
    os_memset(s_gattc_cb_list, 0, sizeof(s_gattc_cb_list));

    s_dm_gattc_is_init = 0;
    return 0;
}

#if CLI_TEST
/* ---------------- CLI (for test) ---------------- */
static void dm_gattc_cmd(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
    uint8_t buffer[256]={0};
    uint8_t dat,len;
    uint16_t handle;
    uint8_t ret1,ret2,ret3;
    dm_gattc_app_env_t *app_env_tmp = NULL;
    dm_gatt_app_env_t *common_env_tmp = NULL;
    
    os_printf("%s \r\n", __func__);
    for (uint8_t i = 0; i < argc; i++)
    {
        os_printf("argv[%d] %s\r\n", i, argv[i]);
    }

    if (argc >= 2 && memcmp(argv[1], "data_write", 10) == 0)
    {
        ret1 = sscanf(argv[2], "%x", &dat);
        ret2 = sscanf(argv[3], "%x", &len);
        ret3 = sscanf(argv[4], "%x", &handle);

        if (ret1 != 1 || ret2 != 1 || ret3 != 1)
        {
            gatt_logi("%s service param err %d,%d\n", __func__, ret1,ret2);
            return;
        }
        if (len > sizeof(buffer)) 
        {
            gatt_loge("data too long!");
            return;
        }
        memset(buffer,dat,len);

        for(uint8_t conn_id=0;conn_id<=5;conn_id++)
        {
            common_env_tmp = dm_ble_find_app_env_by_conn_id(conn_id);

            if (!common_env_tmp || !common_env_tmp->data)
            {
                gatt_logi("conn_id %d not found %d ", conn_id);
                continue;
            }
            else
            {
                gatt_logi("conn_id %d found ", conn_id);
                app_env_tmp = (typeof(app_env_tmp))common_env_tmp->data;
                for(uint8_t cnt = 0;cnt<GATT_MAX_PROFILE_COUNT;cnt++)
                {
                    gatt_logi("handle= %d ,%d ", handle,app_env_tmp->peer_write_handle[cnt]);
                    if(handle==app_env_tmp->peer_write_handle[cnt])
                    {
                        if(app_env_tmp->peer_cproperty_write[cnt]&PROPERTIES_WRITE_NO_RESPONSE)
                        {
                            dm_gattc_write(conn_id,handle,buffer, len,BK_GATT_WRITE_TYPE_NO_RSP);
                        }
                        else
                        {
                            dm_gattc_write(conn_id,handle,buffer, len,BK_GATT_WRITE_TYPE_RSP);
                        }
                    }
                }
            }
        }
    }
    else if (argc >= 3 && memcmp(argv[1], "con_dev_name", 12) == 0)
    {
        uint8_t name_len = strlen(argv[2]);

        if(name_len<sizeof(g_peer_dev.dev.name))
        {
            os_memcpy(g_peer_dev.dev.name, argv[2], name_len);
        }
        g_peer_dev.dev.name[name_len] = '\0';

        g_peer_dev.dev.len = name_len;

        gatt_logi("g_peer_dev.dev.name:%s , %d,%d\n",g_peer_dev.dev.name,g_peer_dev.dev.len,sizeof(g_peer_dev.dev.name));
    }
    else
    {
        os_printf("unsupport cmd \r\n");
    }
}

static const struct cli_command dm_gattc_commands[] =
{
    {"gattc", "gattc", dm_gattc_cmd},
};
static int dm_gattc_cli_init(void)
{
    return cli_register_commands(dm_gattc_commands, sizeof(dm_gattc_commands) / sizeof(dm_gattc_commands[0]));
}
#endif





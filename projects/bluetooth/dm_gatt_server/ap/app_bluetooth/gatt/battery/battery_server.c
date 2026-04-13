#include <common/sys_config.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <components/log.h>
#include <os/mem.h>
#include <os/os.h>

#include "components/bluetooth/bk_dm_bluetooth_types.h"
#include "components/bluetooth/bk_dm_gap_ble_types.h"
#include "components/bluetooth/bk_dm_gap_ble.h"
#include "dm_gatts.h"
#include "dm_gatt_connection.h"
#include "dm_gap_utils.h"
#include "dm_gatt.h"
#include "battery_server.h"

/*
 * UUID Definitions
 */
#define BAS_SERVER_UUID                0x180F
#define BAS_UUID_CHAR_BATTERY_LEVEL    0x2A19

/*
 * Attribute Index
 */
enum
{
    BAS_HDL_IDX_SVC = 0,
    BAS_HDL_IDX_BATTERY_LEVEL,
    BAS_HDL_IDX_BATTERY_LEVEL_CCC,
    BAS_HDL_IDX_MAX,
};

/*
 * Static Data
 */
static uint8_t _battery_level = 100;
static uint8_t _battery_ccc[2] = {0, 0};
static uint8_t s_gatts_if = 0;
static uint16_t gatts_conn_id = 0;
static uint16_t _server_bas_attr_handle_list[BAS_HDL_IDX_MAX] = {0};

/*
 * Attribute Table
 */
static const bk_gatts_attr_db_t _bas_attr_db_service[] =
{
    // Service Declaration
    {
        BK_GATT_PRIMARY_SERVICE_DECL(BAS_SERVER_UUID),
    },

    // Battery Level Characteristic
    {
        BK_GATT_CHAR_DECL(BAS_UUID_CHAR_BATTERY_LEVEL,
                          sizeof(_battery_level),
                          &_battery_level,
                          BK_GATT_CHAR_PROP_BIT_READ | BK_GATT_CHAR_PROP_BIT_NOTIFY,
                          BK_GATT_PERM_READ,
                          BK_GATT_RSP_BY_APP),
    },

    // Battery Level CCC Descriptor
    {
        BK_GATT_CHAR_DESC_DECL(BK_GATT_UUID_CHAR_CLIENT_CONFIG,
                               sizeof(_battery_ccc),
                               _battery_ccc,
                               BK_GATT_PERM_READ | BK_GATT_PERM_WRITE,
                               BK_GATT_RSP_BY_APP),
    },
};

/*
 * Local Helpers
 */
static int _match_ble_handle(uint16_t handle)
{
    for (int i = 0; i < BAS_HDL_IDX_MAX; i++)
    {
        if (handle == _server_bas_attr_handle_list[i])
            return i;
    }
    return -1;
}

/*
 * Event Callback
 */
static int32_t _bas_gatts_cb(bk_gatts_cb_event_t event, bk_gatt_if_t gatts_if, bk_ble_gatts_cb_param_t *param)
{
    switch (event)
    {
        case BK_GATTS_REG_EVT:
        {
            s_gatts_if = gatts_if;
            gatt_logi("BAS_REG_EVT gatts_if=%d", s_gatts_if);
        } break;

        case BK_GATTS_CREAT_ATTR_TAB_EVT:
        {
            if (param->add_attr_tab.status == BK_GATT_OK)
            {
                if (param->add_attr_tab.svc_uuid.len == BK_UUID_LEN_16 &&
                    param->add_attr_tab.svc_uuid.uuid.uuid16 == BAS_SERVER_UUID)
                {
                    for (uint16_t i = 0; i < param->add_attr_tab.num_handle; i++)
                    {
                        _server_bas_attr_handle_list[i] = param->add_attr_tab.handles[i];
                    }
                    gatt_logi("BAS Create Attr Table OK, num_handle=%d", param->add_attr_tab.num_handle);
                    bk_ble_gatts_start_service(_server_bas_attr_handle_list[BAS_HDL_IDX_SVC]);
                }
            }
            else
            {
                gatt_loge("BAS Create Attr Table Failed (0x%x)", param->add_attr_tab.status);
            }
        } break;

        case BK_GATTS_CONNECT_EVT:
        {
            gatts_conn_id = param->connect.conn_id;
            gatt_logi("BAS CONNECT conn_id=%d", gatts_conn_id);
        } break;

        case BK_GATTS_DISCONNECT_EVT:
        {
            gatt_logi("BAS DISCONNECT conn_id=%d", param->disconnect.conn_id);
            if (param->disconnect.conn_id == gatts_conn_id)
            {
                gatts_conn_id = 0;
            }
            _battery_ccc[0] = 0;
            _battery_ccc[1] = 0;
        } break;

        case BK_GATTS_READ_EVT:
        {
            int idx = _match_ble_handle(param->read.handle);
            if (idx != -1)
            {
                gatt_logi("BAS READ_EVT idx=%d handle=%d", idx, param->read.handle);
                if (param->read.need_rsp)
                {
                    bk_gatt_rsp_t rsp;
                    os_memset(&rsp, 0, sizeof(rsp));
                    rsp.attr_value.auth_req = BK_GATT_AUTH_REQ_NONE;
                    rsp.attr_value.handle = param->read.handle;
                    rsp.attr_value.offset = param->read.offset;

                    if (idx == BAS_HDL_IDX_BATTERY_LEVEL)
                    {
                        rsp.attr_value.value = &_battery_level;
                        rsp.attr_value.len = sizeof(_battery_level);
                        gatt_logi("Read Battery Level = %d%%", _battery_level);
                    }
                    else if (idx == BAS_HDL_IDX_BATTERY_LEVEL_CCC)
                    {
                        rsp.attr_value.value = _battery_ccc;
                        rsp.attr_value.len = sizeof(_battery_ccc);
                        gatt_logi("Read CCC = %02x %02x", _battery_ccc[0], _battery_ccc[1]);
                    }
                    else
                    {
                        rsp.attr_value.len = 0;
                    }

                    bk_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id, BK_GATT_OK, &rsp);
                }
            }
        } break;

        case BK_GATTS_WRITE_EVT:
        {
            int idx = _match_ble_handle(param->write.handle);
            if (idx != -1)
            {
                gatt_logi("BAS WRITE_EVT idx=%d handle=%d len=%d", idx, param->write.handle, param->write.len);
                if (idx == BAS_HDL_IDX_BATTERY_LEVEL_CCC)
                {
                    uint16_t cfg = param->write.value[0] | (param->write.value[1] << 8);
                    os_memcpy(_battery_ccc, param->write.value, sizeof(_battery_ccc));
                    if (cfg & 1)
                        gatt_logi("Battery Notify Enabled");
                    else if (cfg == 0)
                        gatt_logi("Battery Notify Disabled");
                }

                if (param->write.need_rsp)
                {
                    bk_gatt_rsp_t rsp;
                    os_memset(&rsp, 0, sizeof(rsp));
                    rsp.attr_value.auth_req = BK_GATT_AUTH_REQ_NONE;
                    rsp.attr_value.handle = param->write.handle;
                    rsp.attr_value.offset = param->write.offset;
                    rsp.attr_value.len = param->write.len;
                    rsp.attr_value.value = param->write.value;
                    bk_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, BK_GATT_OK, &rsp);
                }
            }
        } break;

        default:
            break;
    }
    return BK_ERR_BT_SUCCESS;
}

/*
 * Public API
 */
int battery_server_init(void)
{
    ble_err_t ret = 0;
    dm_gatts_add_gatts_callback(_bas_gatts_cb);

    ret = bk_ble_gatts_create_attr_tab((void *)&_bas_attr_db_service, s_gatts_if, BAS_HDL_IDX_MAX, 30);
    if (ret != BK_GATT_OK)
    {
        gatt_loge("BAS create_attr_tab fail %d", ret);
        return -1;
    }
    gatt_logi("Battery Service Init Done");
    return 0;
}

int battery_server_update_level(uint8_t level)
{
    if (level > 100)
        level = 100;
    _battery_level = level;
    gatt_logi("Battery Level Update: %d%%", _battery_level);

    if (gatts_conn_id && (_battery_ccc[0] & 0x01))
    {
        bk_ble_gatts_send_indicate(s_gatts_if,
                                   gatts_conn_id,
                                   _server_bas_attr_handle_list[BAS_HDL_IDX_BATTERY_LEVEL],
                                   sizeof(_battery_level),
                                   &_battery_level,
                                   false);
        gatt_logi("Battery notify sent");
    }
    return 0;
}

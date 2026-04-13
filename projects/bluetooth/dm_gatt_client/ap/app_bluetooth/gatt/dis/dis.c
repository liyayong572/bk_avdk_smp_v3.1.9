/* device_info_server.c
 *
 * Device Information Service (DIS) implementation
 * Styled to match f618_server.c patterns and callbacks.
 */

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
#include "components/bluetooth/bk_dm_gatt_types.h"
#include "components/bluetooth/bk_dm_gatts.h"

#include "dm_gatts.h"
#include "dm_gatt_connection.h"
#include "dm_gap_utils.h"
#include "dm_gatt.h"



#define INVALID_ATTR_HANDLE 0
#define MIN_VALUE(x, y) (((x) < (y)) ? (x) : (y))

/* UUIDs for DIS and characteristics */
#define DIS_SERVICE_UUID            0x180A
#define DIS_UUID_MANU_NAME         0x2A29
#define DIS_UUID_MODEL_NUM         0x2A24
#define DIS_UUID_SERIAL_NUM        0x2A25
#define DIS_UUID_HW_REV            0x2A27
#define DIS_UUID_FW_REV            0x2A26
#define DIS_UUID_SW_REV            0x2A28
#define DIS_UUID_SYSTEM_ID         0x2A23
#define DIS_UUID_PNP_ID            0x2A50

/* Handle indexes - MUST align with attribute DB order below */
enum {
    DIS_HDL_IDX_SVC = 0,
    DIS_HDL_IDX_MANU_NAME_CHAR,
    //DIS_HDL_IDX_MANU_NAME_VAL,
    DIS_HDL_IDX_MODEL_CHAR,
    //DIS_HDL_IDX_MODEL_VAL,
    DIS_HDL_IDX_SERIAL_CHAR,
    //DIS_HDL_IDX_SERIAL_VAL,
    DIS_HDL_IDX_HW_CHAR,
    //DIS_HDL_IDX_HW_VAL,
    DIS_HDL_IDX_FW_CHAR,
    //DIS_HDL_IDX_FW_VAL,
    DIS_HDL_IDX_SW_CHAR,
    //DIS_HDL_IDX_SW_VAL,
    DIS_HDL_IDX_SYSTEM_ID_CHAR,
    //DIS_HDL_IDX_SYSTEM_ID_VAL,
    DIS_HDL_IDX_PNP_ID_CHAR,
    //DIS_HDL_IDX_PNP_ID_VAL,
    DIS_HDL_IDX_MAX,
};

/* Default characteristic values (can be changed before init if needed) */
static uint8_t dis_manufacturer[] = "beken";
static uint8_t dis_model_number[] = "Model-BK";
static uint8_t dis_serial_number[] = "SN-000001";
static uint8_t dis_hw_rev[] = "HW-1.0";
static uint8_t dis_fw_rev[] = "FW-1.0";
static uint8_t dis_sw_rev[] = "SW-1.0";

/* System ID (8 bytes): manufacturer-defined; example placeholder */
static uint8_t dis_system_id[8] = { 0x01,0x23,0x45,0x67, 0x89,0xAB,0xCD,0xEF };

/* PnP ID (7 bytes): [VendorIDSource(1), VendorID(2 LSB first?), ProductID(2), ProductVersion(2)] 
   Example: VendorIDSource=1 (Bluetooth SIG-assigned), VendorID=0x00E0, ProductID=0x00FF, Version=0x0100 */
static uint8_t dis_pnp_id[7] = { 0x02, 0x5E, 0x04, 0x40, 0x00, 0x00, 0x01 };

/* saved gatts info */
static uint8_t s_gatts_if = 0;
static uint16_t s_gatts_conn_id = 0;
static uint16_t _server_dis_attr_handle_list[DIS_HDL_IDX_MAX] = {0};

/* Attribute DB in the same style as f618_server.c */
static const bk_gatts_attr_db_t _dis_attr_db[] = {
    { BK_GATT_PRIMARY_SERVICE_DECL(DIS_SERVICE_UUID) },

    /* Manufacturer Name String */
    { BK_GATT_CHAR_DECL(DIS_UUID_MANU_NAME, sizeof(dis_manufacturer), dis_manufacturer,
                        BK_GATT_CHAR_PROP_BIT_READ,
                        BK_GATT_PERM_READ,
                        BK_GATT_AUTO_RSP), },
    /* Model Number String */
    { BK_GATT_CHAR_DECL(DIS_UUID_MODEL_NUM, sizeof(dis_model_number), dis_model_number,
                        BK_GATT_CHAR_PROP_BIT_READ,
                        BK_GATT_PERM_READ,
                        BK_GATT_AUTO_RSP), },
    /* Serial Number String */
    { BK_GATT_CHAR_DECL(DIS_UUID_SERIAL_NUM, sizeof(dis_serial_number), dis_serial_number,
                        BK_GATT_CHAR_PROP_BIT_READ,
                        BK_GATT_PERM_READ,
                        BK_GATT_AUTO_RSP), },
    /* Hardware Revision String */
    { BK_GATT_CHAR_DECL(DIS_UUID_HW_REV, sizeof(dis_hw_rev), dis_hw_rev,
                        BK_GATT_CHAR_PROP_BIT_READ,
                        BK_GATT_PERM_READ,
                        BK_GATT_AUTO_RSP), },
    /* Firmware Revision String */
    { BK_GATT_CHAR_DECL(DIS_UUID_FW_REV, sizeof(dis_fw_rev), dis_fw_rev,
                        BK_GATT_CHAR_PROP_BIT_READ,
                        BK_GATT_PERM_READ,
                        BK_GATT_AUTO_RSP), },
    /* Software Revision String */
    { BK_GATT_CHAR_DECL(DIS_UUID_SW_REV, sizeof(dis_sw_rev), dis_sw_rev,
                        BK_GATT_CHAR_PROP_BIT_READ,
                        BK_GATT_PERM_READ,
                        BK_GATT_AUTO_RSP), },

    /* System ID */
    { BK_GATT_CHAR_DECL(DIS_UUID_SYSTEM_ID, sizeof(dis_system_id), dis_system_id,
                        BK_GATT_CHAR_PROP_BIT_READ,
                        BK_GATT_PERM_READ,
                        BK_GATT_AUTO_RSP), },

    /* PnP ID */
    { BK_GATT_CHAR_DECL(DIS_UUID_PNP_ID, sizeof(dis_pnp_id), dis_pnp_id,
                        BK_GATT_CHAR_PROP_BIT_READ,
                        BK_GATT_PERM_READ,
                        BK_GATT_AUTO_RSP), },
};

/* Forward declaration */
static int32_t _dis_gatts_cb(bk_gatts_cb_event_t event, bk_gatt_if_t gatts_if, bk_ble_gatts_cb_param_t *param);
static int _match_dis_handle(uint16_t handle);

/* Public init - matches f618 style */
int device_info_server_init(void)
{
    ble_err_t ret = 0;

    /* register gatts callback once */
    dm_gatts_add_gatts_callback(_dis_gatts_cb);

    /* create attribute table (async event will return handles) */
    ret = bk_ble_gatts_create_attr_tab((void *)&_dis_attr_db, s_gatts_if, DIS_HDL_IDX_MAX, 30);
    if (ret != BK_GATT_OK)
    {
        gatt_loge("dis: create_attr_tab fail %d", ret);
        return -1;
    }

    /* start service when handles available (CREAT_ATTR_TAB_EVT -> START_EVT) */
    if (_server_dis_attr_handle_list[DIS_HDL_IDX_SVC] != INVALID_ATTR_HANDLE)
    {
        bk_ble_gatts_start_service(_server_dis_attr_handle_list[DIS_HDL_IDX_SVC]);
    }

    gatt_logi("device_info_server_init done");
    return 0;
}

/* Helper to set strings before init (optional) */
void device_info_set_manufacturer(const char *s)
{
    if (!s) return;
    size_t n = MIN_VALUE(strlen(s)+1, sizeof(dis_manufacturer));
    os_memcpy(dis_manufacturer, s, n);
}

/* match handler index helper */
static int _match_dis_handle(uint16_t handle)
{
    for (int i = 0; i < DIS_HDL_IDX_MAX; i++)
    {
        if (handle == _server_dis_attr_handle_list[i])
            return i;
    }
    return -1;
}

/* GATTS callback - follows f618 style */
static int32_t _dis_gatts_cb(bk_gatts_cb_event_t event, bk_gatt_if_t gatts_if, bk_ble_gatts_cb_param_t *param)
{
    gatt_logi("dis event=%d", event);

    switch (event)
    {
        case BK_GATTS_REG_EVT:
        {
            s_gatts_if = gatts_if;
            gatt_logi("BK_GATTS_REG_EVT saved gatts_if=%d", s_gatts_if);
        } break;

        case BK_GATTS_START_EVT:
        {
            if (param->start.service_handle == _server_dis_attr_handle_list[DIS_HDL_IDX_SVC] &&
                _server_dis_attr_handle_list[DIS_HDL_IDX_SVC] != INVALID_ATTR_HANDLE)
            {
                gatt_logi("BK_GATTS_START_EVT");
            }
        } break;

        case BK_GATTS_STOP_EVT:
        {
            if (param->stop.service_handle == _server_dis_attr_handle_list[DIS_HDL_IDX_SVC] &&
                _server_dis_attr_handle_list[DIS_HDL_IDX_SVC] != INVALID_ATTR_HANDLE)
            {
                gatt_logi("BK_GATTS_STOP_EVT");
            }
        } break;

        case BK_GATTS_CONNECT_EVT:
        {
            s_gatts_conn_id = param->connect.conn_id;
            gatt_logi("BK_GATTS_CONNECT_EVT conn_id=%d", s_gatts_conn_id);
        } break;

        case BK_GATTS_DISCONNECT_EVT:
        {
            gatt_logi("BK_GATTS_DISCONNECT_EVT conn_id=%d", param->disconnect.conn_id);
            if (param->disconnect.conn_id == s_gatts_conn_id)
            {
                s_gatts_conn_id = 0;
            }
        } break;

        case BK_GATTS_CREAT_ATTR_TAB_EVT:
        {
            if (param->add_attr_tab.status == BK_GATT_OK)
            {
                if (param->add_attr_tab.svc_uuid.len == BK_UUID_LEN_16 &&
                    param->add_attr_tab.svc_uuid.uuid.uuid16 == DIS_SERVICE_UUID)
                {
                    gatt_logi("BK_GATTS_CREAT_ATTR_TAB_EVT");
                    gatt_logi("num_handle= %d", param->add_attr_tab.num_handle);
                    for (uint16_t i = 0; i < param->add_attr_tab.num_handle && i < DIS_HDL_IDX_MAX; i++)
                    {
                        _server_dis_attr_handle_list[i] = param->add_attr_tab.handles[i];
                    }
                }
            }
        } break;

        case BK_GATTS_WRITE_EVT:
        {
            /* DIS is read-only in our implementation; but handle writes defensively */
            int idx = _match_dis_handle(param->write.handle);
            gatt_logi("DIS WRITE evt handle=%d idx=%d len=%d", param->write.handle, idx, param->write.len);
            if (idx != -1)
            {
                if (param->write.need_rsp)
                {
                    bk_gatt_rsp_t rsp;
                    os_memset(&rsp, 0, sizeof(rsp));
                    rsp.attr_value.auth_req = BK_GATT_AUTH_REQ_NONE;
                    rsp.attr_value.handle = param->write.handle;
                    rsp.attr_value.offset = param->write.offset;
                    rsp.attr_value.len = 0;
                    rsp.attr_value.value = NULL;
                    bk_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, BK_GATT_OK, &rsp);
                }
            }
        } break;

        case BK_GATTS_READ_EVT:
        {
            int idx = _match_dis_handle(param->read.handle);
            if (idx != -1)
            {
                gatt_logi("BK_GATTS_READ_EVT idx %d handle=%d", idx, param->read.handle);
                gatt_logi("conn_id %d, trans %d, offset %d, need_rsp %d", param->read.conn_id, param->read.trans_id, param->read.offset, param->read.need_rsp);
                if (param->read.need_rsp)
                {
                    bk_gatt_rsp_t rsp;
                    uint16_t final_len = 0;
                    uint8_t *buffer = NULL;
                    os_memset(&rsp, 0, sizeof(rsp));

                    switch (idx)
                    {
                        case DIS_HDL_IDX_MANU_NAME_CHAR:
                            buffer = (uint8_t*)dis_manufacturer + param->read.offset;
                            final_len = sizeof(dis_manufacturer) - param->read.offset;
                            break;
                        case DIS_HDL_IDX_MODEL_CHAR:
                            buffer = (uint8_t*)dis_model_number + param->read.offset;
                            final_len = sizeof(dis_model_number) - param->read.offset;
                            break;
                        case DIS_HDL_IDX_SERIAL_CHAR:
                            buffer = (uint8_t*)dis_serial_number + param->read.offset;
                            final_len = sizeof(dis_serial_number) - param->read.offset;
                            break;
                        case DIS_HDL_IDX_HW_CHAR:
                            buffer = (uint8_t*)dis_hw_rev + param->read.offset;
                            final_len = sizeof(dis_hw_rev) - param->read.offset;
                            break;
                        case DIS_HDL_IDX_FW_CHAR:
                            buffer = (uint8_t*)dis_fw_rev + param->read.offset;
                            final_len = sizeof(dis_fw_rev) - param->read.offset;
                            break;
                        case DIS_HDL_IDX_SW_CHAR:
                            buffer = (uint8_t*)dis_sw_rev + param->read.offset;
                            final_len = sizeof(dis_sw_rev) - param->read.offset;
                            break;
                        case DIS_HDL_IDX_SYSTEM_ID_CHAR:
                            buffer = dis_system_id + param->read.offset;
                            final_len = sizeof(dis_system_id) - param->read.offset;
                            break;
                        case DIS_HDL_IDX_PNP_ID_CHAR:
                            buffer = dis_pnp_id + param->read.offset;
                            final_len = sizeof(dis_pnp_id) - param->read.offset;
                            break;
                        default:
                            break;
                    }

                    if (buffer != NULL && final_len > 0)
                    {
                        rsp.attr_value.auth_req = BK_GATT_AUTH_REQ_NONE;
                        rsp.attr_value.handle = param->read.handle;
                        rsp.attr_value.offset = param->read.offset;
                        rsp.attr_value.value = buffer;
                        rsp.attr_value.len = final_len;
                        bk_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id, BK_GATT_OK, &rsp);
                    }
                    else
                    {
                        bk_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id, BK_GATT_REQ_NOT_SUPPORTED, &rsp);
                    }
                }
            }
        } break;

        default:
            break;
    }

    return BK_ERR_BT_SUCCESS;
}

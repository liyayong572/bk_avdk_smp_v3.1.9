/* hogpd_server.c
 *
 * HID over GATT Profile Device (HOGPD) - single-file server implementation
 * Reworked to match f618_server.c style and logic.
 */

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
#include "components/bluetooth/bk_dm_gatt_types.h"
#include "components/bluetooth/bk_dm_gatts.h"

#include "dm_gatts.h"
#include "dm_gatt_connection.h"
#include "dm_gap_utils.h"
#include "dm_gatt.h"
#include "dm_gattc.h"

#include "hogpd.h"
#include "bk_cli.h"

/* Keep logging style consistent with f618_server.c */
#ifndef gatt_logi
#define gatt_logi(fmt, ...) BK_LOGI("hogpd", fmt, ##__VA_ARGS__)
#endif
#ifndef gatt_loge
#define gatt_loge(fmt, ...) BK_LOGE("hogpd", fmt, ##__VA_ARGS__)
#endif


#define CHAR_BUFFER_SIZE            64
#define INVALID_ATTR_HANDLE         0
#define MIN_VALUE(x, y) (((x) < (y)) ? (x) : (y))
#ifndef SYNC_CMD_TIMEOUT_MS
#define SYNC_CMD_TIMEOUT_MS         4000
#endif

enum
{
    HOGPD_HDL_IDX_SVC = 0,               /* 0: Primary Service */

    HOGPD_HDL_IDX_HID_INFO,              /* 1: HID Information (char) */
    HOGPD_HDL_IDX_CONTROL_POINT,         /* 2: Control Point (char) */

    HOGPD_HDL_IDX_REPORT_MAP,            /* 3: Report Map (char) */
    HOGPD_HDL_IDX_REPORT_MAP_DESC,       /* 4: Report Map - Ext Rpt Ref Desc */

    HOGPD_HDL_IDX_PROTO_MODE,            /* 5: Protocol Mode (char) */

    HOGPD_HDL_IDX_BOOT_KB_INPUT,         /* 6: Boot Keyboard Input (char) */
    HOGPD_HDL_IDX_BOOT_KB_INPUT_CCC,     /* 7: Boot Keyboard Input - CCCD */
    HOGPD_HDL_IDX_BOOT_KB_INPUT_REF,     /* 8: Boot Keyboard Input - Report Ref */

    HOGPD_HDL_IDX_BOOT_KB_OUTPUT,        /* 9: Boot Keyboard Output (char) */
    HOGPD_HDL_IDX_BOOT_KB_OUTPUT_REF,    /*10: Boot Keyboard Output - Report Ref */

    HOGPD_HDL_IDX_BOOT_MOUSE_INPUT,      /*11: Boot Mouse Input (char) */
    HOGPD_HDL_IDX_BOOT_MOUSE_INPUT_CCC,  /*12: Boot Mouse Input - CCCD */
    HOGPD_HDL_IDX_BOOT_MOUSE_INPUT_REF,  /*13: Boot Mouse Input - Report Ref */

    HOGPD_HDL_IDX_INPUT_REPORT_KB,       /*14: Input Report - Keyboard (char) */
    HOGPD_HDL_IDX_INPUT_REPORT_KB_CCC,   /*15: Input Report KB - CCCD */
    HOGPD_HDL_IDX_INPUT_REPORT_KB_REF,   /*16: Input Report KB - Report Ref */

    HOGPD_HDL_IDX_INPUT_REPORT_MOUSE,    /*17: Input Report - Mouse (char) */
    HOGPD_HDL_IDX_INPUT_REPORT_MOUSE_CCC,/*18: Input Report Mouse - CCCD */
    HOGPD_HDL_IDX_INPUT_REPORT_MOUSE_REF,/*19: Input Report Mouse - Report Ref */

    HOGPD_HDL_IDX_INPUT_REPORT_MM,       /*20: Input Report - Multimedia (char) */
    HOGPD_HDL_IDX_INPUT_REPORT_MM_CCC,   /*21: Input Report MM - CCCD */
    HOGPD_HDL_IDX_INPUT_REPORT_MM_REF,   /*22: Input Report MM - Report Ref */

    HOGPD_HDL_IDX_OUTPUT_REPORT,         /*23: Output Report (char) */
    HOGPD_HDL_IDX_OUTPUT_REPORT_REF,     /*24: Output Report - Report Ref */

    HOGPD_HDL_IDX_MAX                    /*25: total */
};

/* ---------- HID Report Map (composite: keyboard, mouse, consumer MM) ---------- */
#define HIDS_KB_REPORT_ID    1
#define HIDS_MOUSE_REPORT_ID 5
#define HIDS_MM_KB_REPORT_ID 3

static const uint8_t _hid_report_map[] =
{
    /* Keyboard (Report ID 1) */
    0x05, 0x01,             // Usage Page (Generic Desktop)
    0x09, 0x06,             // Usage (Keyboard)
    0xA1, 0x01,             // Collection (Application)
    0x85, HIDS_KB_REPORT_ID,// Report ID 1
    0x05, 0x07,             // Usage Page (Key Codes)
    0x19, 0xE0, 0x29, 0xE7, // Usage Min/Max
    0x15, 0x00, 0x25, 0x01, // Logical Min/Max
    0x75, 0x01, 0x95, 0x08, // Report Size, Count (8 bits * 8)
    0x81, 0x02,             // Input (Data,Var,Abs)
    0x95, 0x01, 0x75, 0x08, // 1 byte reserved
    0x81, 0x01,
    0x95, 0x05, 0x75, 0x01, // LEDs
    0x05, 0x08, 0x19, 0x01, 0x29, 0x05,
    0x91, 0x02,
    0x95, 0x01, 0x75, 0x03, 0x91, 0x01,
    0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0xFF,
    0x05, 0x07, 0x19, 0x00, 0x29, 0xFF, 0x81, 0x00,
    0xC0,

    /* Mouse (Report ID 5) */
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01,
    0x85, HIDS_MOUSE_REPORT_ID, // Report ID 5
    0x09, 0x01, 0xA1, 0x00,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x03,
    0x15, 0x00, 0x25, 0x01,
    0x95, 0x03, 0x75, 0x01, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x05, 0x81, 0x01,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x38,
    0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x03, 0x81, 0x06,
    0xC0, 0xC0,

    /* Consumer / Multimedia (Report ID 3) */
    0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01,
    0x85, HIDS_MM_KB_REPORT_ID, // Report ID 3
    0x19, 0x00, 0x2A, 0x9C, 0x02,
    0x15, 0x00, 0x26, 0x9C, 0x02, 0x95, 0x01, 0x75, 0x10,
    0x81, 0x00, 0xC0
};

/* ---------- Internal buffers and state (per f618 style) ---------- */
static beken_semaphore_t hogpd_sema = NULL;
static uint8_t _proto_mode = 1;
static uint16_t _report_map_desc = 0;

/* Input reports */
static uint8_t _input_report_keyboard[8];
static uint16_t _input_report_keyboard_ccc;
static const uint8_t _input_report_keyboard_ref[] = {HIDS_KB_REPORT_ID, 0x01};

static uint8_t _input_report_mouse[6];
static uint16_t _input_report_mouse_ccc;
static const uint8_t _input_report_mouse_ref[] = {HIDS_MOUSE_REPORT_ID, 0x01};

static uint8_t _input_report_mm[2];
static uint16_t _input_report_mm_ccc;
static const uint8_t _input_report_mm_ref[] = {HIDS_MM_KB_REPORT_ID, 0x01};

/* Output/Feature */
static uint8_t _output_report[3];
static const uint8_t _output_report_ref[] = {HIDS_KB_REPORT_ID, 0x02};


/* HID Info, Boot reports */
static const uint8_t _hid_info[] = {0x11, 0x01, 0x00, 0x03};
/* bcdHID = 0x0111 (v1.11), country = 0x00 (not localized), flags = 0x02 (normally connectable) */

static uint8_t _boot_kb_input[8];
static uint16_t _boot_kb_input_ccc;
static const uint8_t _boot_kb_input_ref[] = {0x00, 0x01}; /* report id 0, type=input */

static uint8_t _boot_kb_output[3];
static const uint8_t _boot_kb_output_ref[] = {0x00, 0x02}; /* report id 0, type=output */

/* saved GATTS info */
static uint8_t s_gatts_if = 0;
static uint16_t s_gatts_conn_id = 0;
static uint16_t _server_hogpd_attr_handle_list[HOGPD_HDL_IDX_MAX] = {0};

/* Attribute DB - same layout style as f618_server.c */
static const bk_gatts_attr_db_t _hogpd_attr_db[] =
{
    { BK_GATT_PRIMARY_SERVICE_DECL(BK_GATT_UUID_HID_SVC) },

    /* HID Information */
    { BK_GATT_CHAR_DECL(BK_GATT_UUID_HID_INFORMATION, sizeof(_hid_info), (uint8_t*)_hid_info,
                        BK_GATT_CHAR_PROP_BIT_READ,
                        BK_GATT_PERM_READ,
                        BK_GATT_AUTO_RSP), },

    /* Control Point */
    { BK_GATT_CHAR_DECL(BK_GATT_UUID_HID_CONTROL_POINT, 0, NULL,
                        BK_GATT_CHAR_PROP_BIT_WRITE_NR,
                        BK_GATT_PERM_WRITE,
                        BK_GATT_AUTO_RSP), },

    /* Report Map */
    { BK_GATT_CHAR_DECL(BK_GATT_UUID_HID_REPORT_MAP, sizeof(_hid_report_map), (uint8_t*)_hid_report_map,
                        BK_GATT_CHAR_PROP_BIT_READ,
                        BK_GATT_PERM_READ_ENCRYPTED,
                        BK_GATT_AUTO_RSP), },
    { BK_GATT_CHAR_DESC_DECL(BK_GATT_UUID_EXT_RPT_REF_DESCR, sizeof(_report_map_desc), (uint8_t*)&_report_map_desc,
                             BK_GATT_PERM_READ,
                             BK_GATT_AUTO_RSP), },

    /* Protocol Mode */
    { BK_GATT_CHAR_DECL(BK_GATT_UUID_HID_PROTO_MODE, sizeof(_proto_mode), &_proto_mode,
                        BK_GATT_CHAR_PROP_BIT_READ | BK_GATT_CHAR_PROP_BIT_WRITE_NR,
                        BK_GATT_PERM_READ | BK_GATT_PERM_WRITE,
                        BK_GATT_RSP_BY_APP), },

    /* Boot Keyboard Input/Output */
    { BK_GATT_CHAR_DECL(BK_GATT_UUID_HID_BT_KB_INPUT, sizeof(_boot_kb_input), _boot_kb_input,
                        BK_GATT_CHAR_PROP_BIT_READ | BK_GATT_CHAR_PROP_BIT_NOTIFY,
                        BK_GATT_PERM_READ | BK_GATT_PERM_WRITE,
                        BK_GATT_RSP_BY_APP), },
    { BK_GATT_CHAR_DESC_DECL(BK_GATT_UUID_CHAR_CLIENT_CONFIG, sizeof(_boot_kb_input_ccc), (uint8_t*)&_boot_kb_input_ccc,
                             BK_GATT_PERM_READ | BK_GATT_PERM_WRITE,
                             BK_GATT_RSP_BY_APP), },
    { BK_GATT_CHAR_DESC_DECL(BK_GATT_UUID_RPT_REF_DESCR, sizeof(_boot_kb_input_ref), (uint8_t*)_boot_kb_input_ref,
                             BK_GATT_PERM_READ,
                             BK_GATT_AUTO_RSP), },

    { BK_GATT_CHAR_DECL(BK_GATT_UUID_HID_BT_KB_OUTPUT, sizeof(_boot_kb_output), _boot_kb_output,
                        BK_GATT_CHAR_PROP_BIT_READ | BK_GATT_CHAR_PROP_BIT_WRITE | BK_GATT_CHAR_PROP_BIT_WRITE_NR,
                        BK_GATT_PERM_READ | BK_GATT_PERM_WRITE,
                        BK_GATT_RSP_BY_APP), },
    { BK_GATT_CHAR_DESC_DECL(BK_GATT_UUID_RPT_REF_DESCR, sizeof(_boot_kb_output_ref), (uint8_t*)_boot_kb_output_ref,
                             BK_GATT_PERM_READ,
                             BK_GATT_AUTO_RSP), },

    /* Boot Keyboard Input/Output */
    { BK_GATT_CHAR_DECL(BK_GATT_UUID_HID_BT_MOUSE_INPUT, sizeof(_boot_kb_input), _boot_kb_input,
                        BK_GATT_CHAR_PROP_BIT_READ | BK_GATT_CHAR_PROP_BIT_NOTIFY,
                        BK_GATT_PERM_READ | BK_GATT_PERM_WRITE,
                        BK_GATT_RSP_BY_APP), },
    { BK_GATT_CHAR_DESC_DECL(BK_GATT_UUID_CHAR_CLIENT_CONFIG, sizeof(_boot_kb_input_ccc), (uint8_t*)&_boot_kb_input_ccc,
                             BK_GATT_PERM_READ | BK_GATT_PERM_WRITE,
                             BK_GATT_RSP_BY_APP), },
    { BK_GATT_CHAR_DESC_DECL(BK_GATT_UUID_RPT_REF_DESCR, sizeof(_boot_kb_input_ref), (uint8_t*)_boot_kb_input_ref,
                             BK_GATT_PERM_READ,
                             BK_GATT_AUTO_RSP), },

    /* Input Report - Keyboard */
    { BK_GATT_CHAR_DECL(BK_GATT_UUID_HID_REPORT, sizeof(_input_report_keyboard), _input_report_keyboard,
                        BK_GATT_CHAR_PROP_BIT_READ | BK_GATT_CHAR_PROP_BIT_WRITE | BK_GATT_CHAR_PROP_BIT_NOTIFY,
                        BK_GATT_PERM_READ | BK_GATT_PERM_WRITE,
                        BK_GATT_RSP_BY_APP), },
    { BK_GATT_CHAR_DESC_DECL(BK_GATT_UUID_CHAR_CLIENT_CONFIG, sizeof(_input_report_keyboard_ccc), (uint8_t*)&_input_report_keyboard_ccc,
                             BK_GATT_PERM_READ | BK_GATT_PERM_WRITE,
                             BK_GATT_RSP_BY_APP), },
    { BK_GATT_CHAR_DESC_DECL(BK_GATT_UUID_RPT_REF_DESCR, sizeof(_input_report_keyboard_ref), (uint8_t*)_input_report_keyboard_ref,
                             BK_GATT_PERM_READ,
                             BK_GATT_AUTO_RSP), },

    /* Input Report - Mouse */
    { BK_GATT_CHAR_DECL(BK_GATT_UUID_HID_REPORT, sizeof(_input_report_mouse), _input_report_mouse,
                        BK_GATT_CHAR_PROP_BIT_READ | BK_GATT_CHAR_PROP_BIT_WRITE | BK_GATT_CHAR_PROP_BIT_NOTIFY,
                        BK_GATT_PERM_READ | BK_GATT_PERM_WRITE,
                        BK_GATT_RSP_BY_APP), },
    { BK_GATT_CHAR_DESC_DECL(BK_GATT_UUID_CHAR_CLIENT_CONFIG, sizeof(_input_report_mouse_ccc), (uint8_t*)&_input_report_mouse_ccc,
                             BK_GATT_PERM_READ | BK_GATT_PERM_WRITE,
                             BK_GATT_RSP_BY_APP), },
    { BK_GATT_CHAR_DESC_DECL(BK_GATT_UUID_RPT_REF_DESCR, sizeof(_input_report_mouse_ref), (uint8_t*)_input_report_mouse_ref,
                             BK_GATT_PERM_READ,
                             BK_GATT_AUTO_RSP), },

    /* Input Report - Multimedia */
    { BK_GATT_CHAR_DECL(BK_GATT_UUID_HID_REPORT, sizeof(_input_report_mm), _input_report_mm,
                        BK_GATT_CHAR_PROP_BIT_READ | BK_GATT_CHAR_PROP_BIT_WRITE | BK_GATT_CHAR_PROP_BIT_NOTIFY,
                        BK_GATT_PERM_READ | BK_GATT_PERM_WRITE,
                        BK_GATT_RSP_BY_APP), },
    { BK_GATT_CHAR_DESC_DECL(BK_GATT_UUID_CHAR_CLIENT_CONFIG, sizeof(_input_report_mm_ccc), (uint8_t*)&_input_report_mm_ccc,
                             BK_GATT_PERM_READ | BK_GATT_PERM_WRITE,
                             BK_GATT_RSP_BY_APP), },
    { BK_GATT_CHAR_DESC_DECL(BK_GATT_UUID_RPT_REF_DESCR, sizeof(_input_report_mm_ref), (uint8_t*)_input_report_mm_ref,
                             BK_GATT_PERM_READ,
                             BK_GATT_AUTO_RSP), },

    /* Output Report */
    { BK_GATT_CHAR_DECL(BK_GATT_UUID_HID_REPORT, sizeof(_output_report), _output_report,
                        BK_GATT_CHAR_PROP_BIT_READ | BK_GATT_CHAR_PROP_BIT_WRITE | BK_GATT_CHAR_PROP_BIT_WRITE_NR,
                        BK_GATT_PERM_READ | BK_GATT_PERM_WRITE,
                        BK_GATT_RSP_BY_APP), },
    { BK_GATT_CHAR_DESC_DECL(BK_GATT_UUID_RPT_REF_DESCR, sizeof(_output_report_ref), (uint8_t*)_output_report_ref,
                             BK_GATT_PERM_READ,
                             BK_GATT_AUTO_RSP), },
};

/* forward declarations */
static int32_t _hogpd_gatts_cb(bk_gatts_cb_event_t event, bk_gatt_if_t gatts_if, bk_ble_gatts_cb_param_t *param);
static int _match_ble_handle(uint16_t handle);
static void hogpd_receive_write_data(uint16_t handle, uint8_t *buffer, uint16_t len);
static int cli_ble_hogpd_init(void);

/* ---------------- public API (f618 style) ---------------- */

int hogpd_server_init(void)
{
    ble_err_t ret = 0;

    /* init semaphore if not inited */
    if (hogpd_sema == NULL)
    {
        ret = rtos_init_semaphore(&hogpd_sema, 1);
        if (ret != 0)
        {
            gatt_loge("init semaphore err %d", ret);
            return -1;
        }
    }

    /* register gatts callback once */
    dm_gatts_add_gatts_callback(_hogpd_gatts_cb);

    /* create attribute table (async: CRET_ATTR_TAB_EVT will return handles) */
    ret = bk_ble_gatts_create_attr_tab((void *)&_hogpd_attr_db, s_gatts_if, HOGPD_HDL_IDX_MAX, 30);
    if (ret != BK_GATT_OK)
    {
        gatt_loge("create_attr_tab fail %d", ret);
        return -1;
    }

    /* start service when handles available (CREAT_ATTR_TAB_EVT will populate _server_hogpd_attr_handle_list) */
    if (_server_hogpd_attr_handle_list[HOGPD_HDL_IDX_SVC] != INVALID_ATTR_HANDLE)
    {
        bk_ble_gatts_start_service(_server_hogpd_attr_handle_list[HOGPD_HDL_IDX_SVC]);
    }

    /* CLI registration for test commands */
    cli_ble_hogpd_init();

    return 0;
}

/* Notify/Indicate input report (keyboard by default index) */
int hogpd_ntf_input(uint8_t *buffer, uint16_t len)
{
    uint16_t handle = INVALID_ATTR_HANDLE;

    if (buffer == NULL || len == 0)
    {
        gatt_loge("ntf_input invalid params");
        return -1;
    }
    if (s_gatts_conn_id == 0)
    {
        gatt_loge("ntf_input no connection");
        return -1;
    }

    if(len==8)
    {
        handle = _server_hogpd_attr_handle_list[HOGPD_HDL_IDX_INPUT_REPORT_KB];
    }
    else if(len==2)
    {
        handle = _server_hogpd_attr_handle_list[HOGPD_HDL_IDX_INPUT_REPORT_MM];
    }
    else if(len==6)
    {
        handle = _server_hogpd_attr_handle_list[HOGPD_HDL_IDX_INPUT_REPORT_MOUSE];
    }

    bk_err_t err = bk_ble_gatts_send_indicate(s_gatts_if, (uint16_t)s_gatts_conn_id, handle, len, buffer, 0);
    if (err != BK_GATT_OK)
    {
        gatt_loge("send_indicate fail %d", err);
        return -1;
    }

    err = rtos_get_semaphore(&hogpd_sema, SYNC_CMD_TIMEOUT_MS);
    if (err != kNoErr)
    {
        gatt_loge("indicate timeout %d", err);
        return -1;
    }

    return 0;
}

int hogpd_server_deinit(void)
{
    dm_gatts_unreg_db((bk_gatts_attr_db_t*)_hogpd_attr_db);

    if (hogpd_sema)
    {
        rtos_deinit_semaphore(&hogpd_sema);
        hogpd_sema = NULL;
    }

    memset(_server_hogpd_attr_handle_list, 0, sizeof(_server_hogpd_attr_handle_list));
    s_gatts_conn_id = 0;
    s_gatts_if = 0;

    return 0;
}

/* ---------------- helpers & callbacks (f618 style) ---------------- */

static void hogpd_receive_write_data(uint16_t handle, uint8_t *buffer, uint16_t len)
{
    if (buffer == NULL || len == 0) return;

    for (uint16_t i = 0; i < len; i++)
    {
        gatt_logi("%02x", buffer[i]);
    }

}

static int _match_ble_handle(uint16_t handle)
{
    for (int i = 0; i < HOGPD_HDL_IDX_MAX; i++)
    {
        if (handle == _server_hogpd_attr_handle_list[i])
            return i;
    }
    return -1;
}

static int32_t _hogpd_gatts_cb(bk_gatts_cb_event_t event, bk_gatt_if_t gatts_if, bk_ble_gatts_cb_param_t *param)
{
    gatt_logi("event=%d", event);

    switch (event)
    {
        case BK_GATTS_REG_EVT:
        {
            s_gatts_if = gatts_if;
            gatt_logi("BK_GATTS_REG_EVT saved gatts_if=%d", s_gatts_if);
        } break;

        case BK_GATTS_START_EVT:
        {
            if (param->start.service_handle == _server_hogpd_attr_handle_list[HOGPD_HDL_IDX_SVC] &&
                _server_hogpd_attr_handle_list[HOGPD_HDL_IDX_SVC] != INVALID_ATTR_HANDLE)
            {
                gatt_logi("BK_GATTS_START_EVT");
            }
        } break;

        case BK_GATTS_STOP_EVT:
        {
            if (param->stop.service_handle == _server_hogpd_attr_handle_list[HOGPD_HDL_IDX_SVC] &&
                _server_hogpd_attr_handle_list[HOGPD_HDL_IDX_SVC] != INVALID_ATTR_HANDLE)
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
                    param->add_attr_tab.svc_uuid.uuid.uuid16 == BK_GATT_UUID_HID_SVC)
                {
                    gatt_logi("BK_GATTS_CREAT_ATTR_TAB_EVT");
                    gatt_logi("num_handle= %d", param->add_attr_tab.num_handle);
                    for (uint16_t i = 0; i < param->add_attr_tab.num_handle; i++)
                    {
                        _server_hogpd_attr_handle_list[i] = param->add_attr_tab.handles[i];
                    }
                }
            }
        } break;

        case BK_GATTS_WRITE_EVT:
        {
            int idx = _match_ble_handle(param->write.handle);
            if (idx != -1)
            {
                bk_gatt_rsp_t rsp;
                uint16_t final_len = 0;
                os_memset(&rsp, 0, sizeof(rsp));

                /* Protocol Mode */
                if (idx == HOGPD_HDL_IDX_PROTO_MODE)
                {
                    final_len = MIN_VALUE(param->write.len, (uint16_t)sizeof(_proto_mode) - param->write.offset);
                    os_memcpy((uint8_t*)&_proto_mode + param->write.offset, param->write.value, final_len);
                    gatt_logi("proto_mode write %d", param->write.len);
                    if (param->write.need_rsp)
                    {
                        rsp.attr_value.auth_req = BK_GATT_AUTH_REQ_NONE;
                        rsp.attr_value.handle = param->write.handle;
                        rsp.attr_value.offset = param->write.offset;
                        rsp.attr_value.len = final_len;
                        rsp.attr_value.value = (uint8_t*)&_proto_mode + param->write.offset;
                        bk_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, BK_GATT_OK, &rsp);
                    }
                }
                /* Keyboard input report write */
                else if (idx == HOGPD_HDL_IDX_INPUT_REPORT_KB)
                {
                    final_len = MIN_VALUE(param->write.len, (uint16_t)sizeof(_input_report_keyboard) - param->write.offset);
                    os_memcpy(_input_report_keyboard + param->write.offset, param->write.value, final_len);
                    gatt_logi("input_report_keyboard recv %d", param->write.len);
                    if (param->write.need_rsp)
                    {
                        rsp.attr_value.auth_req = BK_GATT_AUTH_REQ_NONE;
                        rsp.attr_value.handle = param->write.handle;
                        rsp.attr_value.offset = param->write.offset;
                        rsp.attr_value.len = final_len;
                        rsp.attr_value.value = _input_report_keyboard + param->write.offset;
                        bk_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, BK_GATT_OK, &rsp);
                    }
                }
                /* Keyboard CCCD */
                else if (idx == HOGPD_HDL_IDX_INPUT_REPORT_KB_CCC)
                {
                    uint16_t config = (((uint16_t)(param->write.value[1])) << 8) | param->write.value[0];
                    if (config & 1)
                    {
                        gatt_logi("client notify open (kb)");
                    }
                    else if (config & 2)
                    {
                        gatt_logi("client indicate open (kb)");
                    }
                    else if (config == 0)
                    {
                        gatt_logi("client config close (kb)");
                    }

                    if (param->write.need_rsp)
                    {
                        final_len = MIN_VALUE(param->write.len, (uint16_t)sizeof(_input_report_keyboard_ccc) - param->write.offset);
                        os_memcpy((uint8_t*)&_input_report_keyboard_ccc + param->write.offset, param->write.value, final_len);

                        rsp.attr_value.auth_req = BK_GATT_AUTH_REQ_NONE;
                        rsp.attr_value.handle = param->write.handle;
                        rsp.attr_value.offset = param->write.offset;
                        rsp.attr_value.len = final_len;
                        rsp.attr_value.value = (uint8_t*)&_input_report_keyboard_ccc + param->write.offset;

                        bk_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, BK_GATT_OK, &rsp);
                    }
                }
                /* Mouse input report */
                else if (idx == HOGPD_HDL_IDX_INPUT_REPORT_MOUSE)
                {
                    final_len = MIN_VALUE(param->write.len, (uint16_t)sizeof(_input_report_mouse) - param->write.offset);
                    os_memcpy(_input_report_mouse + param->write.offset, param->write.value, final_len);
                    gatt_logi("input_report_mouse recv %d", param->write.len);
                    if (param->write.need_rsp)
                    {
                        rsp.attr_value.auth_req = BK_GATT_AUTH_REQ_NONE;
                        rsp.attr_value.handle = param->write.handle;
                        rsp.attr_value.offset = param->write.offset;
                        rsp.attr_value.len = final_len;
                        rsp.attr_value.value = _input_report_mouse + param->write.offset;
                        bk_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, BK_GATT_OK, &rsp);
                    }
                }
                /* Mouse CCCD */
                else if (idx == HOGPD_HDL_IDX_INPUT_REPORT_MOUSE_CCC)
                {
                    uint16_t config = (((uint16_t)(param->write.value[1])) << 8) | param->write.value[0];
                    gatt_logi("write input_report_mouse ccc config=0x%x", config);
                    if (param->write.need_rsp)
                    {
                        final_len = MIN_VALUE(param->write.len, (uint16_t)sizeof(_input_report_mouse_ccc) - param->write.offset);
                        os_memcpy((uint8_t*)&_input_report_mouse_ccc + param->write.offset, param->write.value, final_len);

                        rsp.attr_value.auth_req = BK_GATT_AUTH_REQ_NONE;
                        rsp.attr_value.handle = param->write.handle;
                        rsp.attr_value.offset = param->write.offset;
                        rsp.attr_value.len = final_len;
                        rsp.attr_value.value = (uint8_t*)&_input_report_mouse_ccc + param->write.offset;

                        bk_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, BK_GATT_OK, &rsp);
                    }
                }
                /* Multimedia input report */
                else if (idx == HOGPD_HDL_IDX_INPUT_REPORT_MM)
                {
                    final_len = MIN_VALUE(param->write.len, (uint16_t)sizeof(_input_report_mm) - param->write.offset);
                    os_memcpy(_input_report_mm + param->write.offset, param->write.value, final_len);
                    gatt_logi("input_report_mm recv %d", param->write.len);
                    if (param->write.need_rsp)
                    {
                        rsp.attr_value.auth_req = BK_GATT_AUTH_REQ_NONE;
                        rsp.attr_value.handle = param->write.handle;
                        rsp.attr_value.offset = param->write.offset;
                        rsp.attr_value.len = final_len;
                        rsp.attr_value.value = _input_report_mm + param->write.offset;
                        bk_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, BK_GATT_OK, &rsp);
                    }
                }
                else if(idx == HOGPD_HDL_IDX_INPUT_REPORT_MM_CCC)
                {
                    uint16_t config = (((uint16_t)(param->write.value[1])) << 8) | param->write.value[0];
                    gatt_logi("write mm ccc config=0x%x", config);
                    if (param->write.need_rsp)
                    {
                        final_len = MIN_VALUE(param->write.len, sizeof(_input_report_mm_ccc) - param->write.offset);
                        os_memcpy((uint8_t*)&_input_report_mm_ccc + param->write.offset, param->write.value, final_len);

                        rsp.attr_value.auth_req = BK_GATT_AUTH_REQ_NONE;
                        rsp.attr_value.handle = param->write.handle;
                        rsp.attr_value.offset = param->write.offset;
                        rsp.attr_value.len = final_len;
                        rsp.attr_value.value = (uint8_t*)&_input_report_mm_ccc + param->write.offset;
                        bk_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, BK_GATT_OK, &rsp);
                    }
                }
                else if(idx == HOGPD_HDL_IDX_OUTPUT_REPORT)
                {
                    final_len = MIN_VALUE(param->write.len, (uint16_t)sizeof(_output_report) - param->write.offset);
                    os_memcpy(_output_report + param->write.offset, param->write.value, final_len);
                    gatt_logi("output_report recv %d", param->write.len);
                    if (param->write.need_rsp)
                    {
                        rsp.attr_value.auth_req = BK_GATT_AUTH_REQ_NONE;
                        rsp.attr_value.handle = param->write.handle;
                        rsp.attr_value.offset = param->write.offset;
                        rsp.attr_value.len = final_len;
                        rsp.attr_value.value = _output_report + param->write.offset;
                        bk_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, BK_GATT_OK, &rsp);
                    }
                    hogpd_receive_write_data(param->write.handle, param->write.value, param->write.len);
                }
                else if (idx == HOGPD_HDL_IDX_CONTROL_POINT)
                {
                    gatt_logi("control point write");
                    if (param->write.need_rsp)
                    {
                        rsp.attr_value.auth_req = BK_GATT_AUTH_REQ_NONE;
                        rsp.attr_value.handle = param->write.handle;
                        rsp.attr_value.offset = param->write.offset;
                        rsp.attr_value.len = 0;
                        rsp.attr_value.value = NULL;
                        bk_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, BK_GATT_OK, &rsp);
                    }
                }
                else if ((idx == HOGPD_HDL_IDX_BOOT_KB_INPUT_CCC)||(idx ==HOGPD_HDL_IDX_BOOT_MOUSE_INPUT_CCC))
                {
                    uint16_t config = (((uint16_t)(param->write.value[1])) << 8) | param->write.value[0];
                    gatt_logi("write bootkb input ccc config=0x%x", config);
                    if (param->write.need_rsp)
                    {
                        final_len = MIN_VALUE(param->write.len, (uint16_t)sizeof(_boot_kb_input_ccc) - param->write.offset);
                        os_memcpy((uint8_t*)&_boot_kb_input_ccc + param->write.offset, param->write.value, final_len);

                        rsp.attr_value.auth_req = BK_GATT_AUTH_REQ_NONE;
                        rsp.attr_value.handle = param->write.handle;
                        rsp.attr_value.offset = param->write.offset;
                        rsp.attr_value.len = final_len;
                        rsp.attr_value.value = (uint8_t*)&_boot_kb_input_ccc + param->write.offset;

                        bk_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, BK_GATT_OK, &rsp);
                    }
                }
                else if (idx == HOGPD_HDL_IDX_BOOT_KB_OUTPUT)
                {
                    final_len = MIN_VALUE(param->write.len, (uint16_t)sizeof(_boot_kb_output) - param->write.offset);
                    os_memcpy(_boot_kb_output + param->write.offset, param->write.value, final_len);
                    gatt_logi("boot_kb_output recv %d", param->write.len);
                    if (param->write.need_rsp)
                    {
                        rsp.attr_value.auth_req = BK_GATT_AUTH_REQ_NONE;
                        rsp.attr_value.handle = param->write.handle;
                        rsp.attr_value.offset = param->write.offset;
                        rsp.attr_value.len = final_len;
                        rsp.attr_value.value = _boot_kb_output + param->write.offset;
                        bk_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, BK_GATT_OK, &rsp);
                    }
                    hogpd_receive_write_data(param->write.handle, param->write.value, param->write.len);
                }
            }
        } break;

        case BK_GATTS_READ_EVT:
        {
            int idx = _match_ble_handle(param->read.handle);
            if (idx != -1)
            {
                gatt_logi("BK_GATTS_READ_EVT idx %d", idx);
                gatt_logi("conn_id %d,trans %d,hdl %d,offset %d,need_rsp %d,is_long %d",
                          param->read.conn_id, param->read.trans_id, param->read.handle, param->read.offset,
                          param->read.need_rsp, param->read.is_long);
                if (param->read.need_rsp)
                {
                    bk_gatt_rsp_t rsp;
                    uint16_t final_len = 0;
                    uint8_t *buffer = NULL;
                    os_memset(&rsp, 0, sizeof(rsp));

                    if (idx == HOGPD_HDL_IDX_PROTO_MODE)
                    {
                        buffer = (uint8_t*)&_proto_mode + param->read.offset;
                        final_len = sizeof(_proto_mode) - param->read.offset;
                    }
                    else if (idx == HOGPD_HDL_IDX_REPORT_MAP)
                    {
                        buffer = (uint8_t*)_hid_report_map + param->read.offset;
                        final_len = sizeof(_hid_report_map) - param->read.offset;
                    }
                    else if (idx == HOGPD_HDL_IDX_REPORT_MAP_DESC)
                    {
                        buffer = (uint8_t*)&_report_map_desc + param->read.offset;
                        final_len = sizeof(_report_map_desc) - param->read.offset;
                    }
                    else if (idx == HOGPD_HDL_IDX_INPUT_REPORT_KB)
                    {
                        buffer = _input_report_keyboard + param->read.offset;
                        final_len = sizeof(_input_report_keyboard) - param->read.offset;
                    }
                    else if (idx == HOGPD_HDL_IDX_INPUT_REPORT_KB_CCC)
                    {
                        buffer = (uint8_t*)&_input_report_keyboard_ccc + param->read.offset;
                        final_len = sizeof(_input_report_keyboard_ccc) - param->read.offset;
                    }
                    else if (idx == HOGPD_HDL_IDX_INPUT_REPORT_KB_REF)
                    {
                        buffer = (uint8_t*)_input_report_keyboard_ref + param->read.offset;
                        final_len = sizeof(_input_report_keyboard_ref) - param->read.offset;
                    }
                    else if (idx == HOGPD_HDL_IDX_INPUT_REPORT_MOUSE)
                    {
                        buffer = _input_report_mouse + param->read.offset;
                        final_len = sizeof(_input_report_mouse) - param->read.offset;
                    }
                    else if (idx == HOGPD_HDL_IDX_INPUT_REPORT_MOUSE_CCC)
                    {
                        buffer = (uint8_t*)&_input_report_mouse_ccc + param->read.offset;
                        final_len = sizeof(_input_report_mouse_ccc) - param->read.offset;
                    }
                    else if (idx == HOGPD_HDL_IDX_INPUT_REPORT_MOUSE_REF)
                    {
                        buffer = (uint8_t*)_input_report_mouse_ref + param->read.offset;
                        final_len = sizeof(_input_report_mouse_ref) - param->read.offset;
                    }
                    else if (idx == HOGPD_HDL_IDX_INPUT_REPORT_MM)
                    {
                        buffer = _input_report_mm + param->read.offset;
                        final_len = sizeof(_input_report_mm) - param->read.offset;
                    }
                    else if (idx == HOGPD_HDL_IDX_INPUT_REPORT_MM_CCC)
                    {
                        buffer = (uint8_t*)&_input_report_mm_ccc + param->read.offset;
                        final_len = sizeof(_input_report_mm_ccc) - param->read.offset;
                    }
                    else if (idx == HOGPD_HDL_IDX_INPUT_REPORT_MM_REF)
                    {
                        buffer = (uint8_t*)_input_report_mm_ref + param->read.offset;
                        final_len = sizeof(_input_report_mm_ref) - param->read.offset;
                    }
                    else if (idx == HOGPD_HDL_IDX_OUTPUT_REPORT)
                    {
                        buffer = _output_report + param->read.offset;
                        final_len = sizeof(_output_report) - param->read.offset;
                    }
                    else if (idx == HOGPD_HDL_IDX_OUTPUT_REPORT_REF)
                    {
                        buffer = (uint8_t*)_output_report_ref + param->read.offset;
                        final_len = sizeof(_output_report_ref) - param->read.offset;
                    }
                    else if (idx == HOGPD_HDL_IDX_HID_INFO)
                    {
                        buffer = (uint8_t*)_hid_info + param->read.offset;
                        final_len = sizeof(_hid_info) - param->read.offset;
                    }
                    else if (idx == HOGPD_HDL_IDX_BOOT_KB_INPUT)
                    {
                        buffer = _boot_kb_input + param->read.offset;
                        final_len = sizeof(_boot_kb_input) - param->read.offset;
                    }
                    else if (idx == HOGPD_HDL_IDX_BOOT_KB_INPUT_CCC)
                    {
                        buffer = (uint8_t*)&_boot_kb_input_ccc + param->read.offset;
                        final_len = sizeof(_boot_kb_input_ccc) - param->read.offset;
                    }
                    else if (idx == HOGPD_HDL_IDX_BOOT_KB_INPUT_REF)
                    {
                        buffer = (uint8_t*)_boot_kb_input_ref + param->read.offset;
                        final_len = sizeof(_boot_kb_input_ref) - param->read.offset;
                    }
                    else if (idx == HOGPD_HDL_IDX_BOOT_KB_OUTPUT)
                    {
                        buffer = _boot_kb_output + param->read.offset;
                        final_len = sizeof(_boot_kb_output) - param->read.offset;
                    }
                    else if (idx == HOGPD_HDL_IDX_BOOT_KB_OUTPUT_REF)
                    {
                        buffer = (uint8_t*)_boot_kb_output_ref + param->read.offset;
                        final_len = sizeof(_boot_kb_output_ref) - param->read.offset;
                    }

                    if (buffer != NULL)
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

        case BK_GATTS_EXEC_WRITE_EVT:
        {
            gatt_logi("BK_GATTS_EXEC_WRITE_EVT");
        } break;

        case BK_GATTS_CONF_EVT:
        {
            /* only handle confirm for input_report_kb to wake waiting semaphore (like f618) */
            if (param->conf.status == 0 &&
              ((param->conf.handle == _server_hogpd_attr_handle_list[HOGPD_HDL_IDX_INPUT_REPORT_KB]) ||
               (param->conf.handle == _server_hogpd_attr_handle_list[HOGPD_HDL_IDX_INPUT_REPORT_MM]) ||
               (param->conf.handle == _server_hogpd_attr_handle_list[HOGPD_HDL_IDX_INPUT_REPORT_MOUSE]) ||
               (param->conf.handle == _server_hogpd_attr_handle_list[HOGPD_HDL_IDX_BOOT_KB_INPUT]) ) )
            {
                gatt_logi("confirm handle=%x", param->conf.handle);
                if (hogpd_sema != NULL)
                {
                    rtos_set_semaphore(&hogpd_sema);
                }
            }
            else if (param->conf.status != 0)
            {
                gatt_loge("indicate confirm status error %x", param->conf.status);
            }
        } break;

        default:
            break;
    }
    return BK_ERR_BT_SUCCESS;
}

/* ---------------- CLI helper (test) ---------------- */

static void cmd_hogpd(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
    
    os_printf("%s \r\n", __func__);
    for (uint8_t i = 0; i < argc; i++)
    {
        os_printf("argv[%d] %s\r\n", i, argv[i]);
    }

    if (argc >= 2 && memcmp(argv[1], "data_send", 9) == 0)
    {
        if(ble_get_host_device_manufacture())
        {
            static uint8_t key_press_cnt=0;
            uint8_t key_press_add[2] = {0xE9, 0x00};
            uint8_t key_press_sub[2] = {0xEA, 0x00};
            uint8_t key_release[2] = {0x00, 0x00};

            key_press_cnt++;
            if(key_press_cnt%2==0)
            {
                hogpd_ntf_input(key_press_add, 2);
            }
            else
            {
                hogpd_ntf_input(key_press_sub, 2);
            }
            
            rtos_delay_milliseconds(20);
            hogpd_ntf_input(key_release, 2);
        }
        else
        {
            uint8_t key_press[8] = {0x00, 0x00,0x28,0x00, 0x00,0x00, 0x00,0x00};
            uint8_t key_release[8] = {0x00, 0x00,0x00,0x00, 0x00,0x00, 0x00,0x00};
            hogpd_ntf_input(key_press, 8);
            rtos_delay_milliseconds(20);
            hogpd_ntf_input(key_release, 8);
        }
    }
    else
    {
        os_printf("unsupport cmd \r\n");
    }
}

static const struct cli_command s_ble_hogpd_commands[] =
{
    {"hogpd", "hogpd", cmd_hogpd},
};
static int cli_ble_hogpd_init(void)
{
    return cli_register_commands(s_ble_hogpd_commands, sizeof(s_ble_hogpd_commands) / sizeof(s_ble_hogpd_commands[0]));
}

#pragma once

#include "components/bluetooth/bk_dm_bluetooth_types.h"
#include "components/bluetooth/bk_dm_gap_ble_types.h"
#include "components/bluetooth/bk_dm_gap_ble.h"
#include "components/bluetooth/bk_dm_ble_types.h"
#include "components/bluetooth/bk_dm_gattc.h"
#include "components/bluetooth/bk_dm_gatt_common.h"
#include "dm_gatt.h"


enum
{
    GATTC_CONNECT=0X10,
    GATTC_DISCONNECT,
    GATTC_SCAN_COMPLETE,
    
};
enum
{
    PROPERTIES_BROADCAST=0x01,
    PROPERTIES_READ=0x02,
    PROPERTIES_WRITE_NO_RESPONSE=0x04,
    PROPERTIES_WRITE=0x08,
    PROPERTIES_NOTIFY=0x10,
    PROPERTIES_INDICATE=0x20,
    PROPERTIES_AUTH_WRITE=0x40,
    
};
typedef int32_t (* dm_ble_gattc_app_cb)(bk_gattc_cb_event_t event, bk_gatt_if_t gattc_if, bk_ble_gattc_cb_param_t *comm_param);
typedef struct
{
    uint8_t len;
    uint8_t name[50];
} ble_device_name_t;

typedef struct
{
    uint8_t state;
    uint8_t addr_type;
    bd_addr_t bdaddr;
    ble_device_name_t dev;
} ble_device_info_t;



typedef struct
{
    uint16_t prf_id;
    uint16_t att_idx;
    uint16_t len;
    char *data;
} ble_gap_evt_write_t;
/**@brief Event structure for @ref FMNA_BLE_GAP_EVT_CONNECTED. */
typedef struct
{
    uint16_t con_interval;
} ble_gap_evt_connected_t;

/**@brief Event structure for @ref FMNA_BLE_GAP_EVT_DISCONNECTED. */
typedef struct
{
    uint8_t reason;
} ble_gap_evt_disconnected_t;

/**@brief Event structure for @ref FMNA_BLE_GAP_EVT_CONN_PARAM_UPDATE. */
typedef struct
{
    uint8_t addr_type;
    bk_bd_addr_t addr;
} ble_gap_evt_scan_complete_t;


typedef struct
{
    uint8_t type;
    uint8_t conidx;                                     /**< Connection Handle on which event occurred. */
    union                                                     /**< union alternative identified by evt_id in enclosing struct. */
    {
        ble_gap_evt_connected_t            connected;                    /**< Connected Event Parameters. */
        ble_gap_evt_disconnected_t         disconnected;                 /**< Disconnected Event Parameters. */
        ble_gap_evt_scan_complete_t        scan_complete;            /**< Connection Parameter Update Parameters. */
        ble_gap_evt_write_t                write;
    } params;                                                                 /**< Event Parameters. */
} gattc_ble_gap_evt_t;



int dm_gattc_init(void);
int dm_gattc_deinit(void);
int32_t dm_gattc_connect(uint8_t *addr, uint32_t addr_type, uint32_t s_timeout);
int32_t dm_gattc_disconnect(uint8_t *addr);
int32_t dm_gattc_connect_cancel(void);
int32_t dm_gattc_discover(uint16_t conn_id);
int32_t dm_gattc_write(uint16_t conn_id, uint16_t attr_handle, uint8_t *data, uint32_t len,uint8_t write_type);
int32_t dm_gattc_write_ext(uint16_t gatt_conn_id, uint16_t attr_handle, uint8_t *data, uint32_t len, uint8_t write_req);
int32_t dm_gattc_read(uint16_t gatt_conn_id, uint16_t attr_handle, uint8_t *data, uint32_t len);
int32_t dm_gattc_send_mtu_req(uint8_t *mac, uint8_t gatt_conn_id);
int ble_bond_status_set(uint8_t bond_status);
int ble_get_host_device_manufacture(void);

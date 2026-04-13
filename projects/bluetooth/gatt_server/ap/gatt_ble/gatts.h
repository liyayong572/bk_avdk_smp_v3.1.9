#ifndef __BLE_TEMP_H__
#define __BLE_TEMP_H__

#define LOOP_BACK_TEST          0
#define ADV_TYPE_LOCAL_NAME                 (0x09)
#define ADV_MAX_SIZE (251)
#define ADV_NAME_HEAD "BK7258_BLE"

enum
{
    PRF_TASK_ID_FA00,
    PRF_TASK_ID_F618,
    //add your profile id
    PRF_TASK_ID_MAX,
};
enum prf_conf
{
    /// Stop notification/indication
    PRF_CLI_STOP_NTFIND = 0x0000,
    /// Start notification
    PRF_CLI_START_NTF,
    /// Start indication
    PRF_CLI_START_IND,
};

enum
{
    GATTS_CONNECT=0X10,
    GATTS_DISCONNECT,
    GATTS_WRITE_REQUEST,
    GATTS_CONNECT_UPDATA_PARAM_COMPLETE,
    GATTS_CONN_SEC_UPDATE,
    GATTS_ENCRYPT_REQ,
    GATTS_ENCRYPT_IND,
};
enum
{
    GATT_DEBUG_LEVEL_ERROR,
    GATT_DEBUG_LEVEL_WARNING,
    GATT_DEBUG_LEVEL_INFO,
    GATT_DEBUG_LEVEL_DEBUG,
    GATT_DEBUG_LEVEL_VERBOSE,
};
    
#define BOARDING_UUID              (0xFE01)
#define GATTS_CB_LIST_SIZE          PRF_TASK_ID_MAX
#ifndef SYNC_CMD_TIMEOUT_MS
#define SYNC_CMD_TIMEOUT_MS         4000
#endif
#define GATT_DEBUG_LEVEL GATT_DEBUG_LEVEL_INFO
#define gatt_loge(format, ...) do{if(GATT_DEBUG_LEVEL >= GATT_DEBUG_LEVEL_ERROR)   BK_LOGE("app_gatt", "%s:" format "\n", __func__, ##__VA_ARGS__);} while(0)
#define gatt_logw(format, ...) do{if(GATT_DEBUG_LEVEL >= GATT_DEBUG_LEVEL_WARNING) BK_LOGW("app_gatt", "%s:" format "\n", __func__, ##__VA_ARGS__);} while(0)
#define gatt_logi(format, ...) do{if(GATT_DEBUG_LEVEL >= GATT_DEBUG_LEVEL_INFO)    BK_LOGI("app_gatt", "%s:" format "\n", __func__, ##__VA_ARGS__);} while(0)
#define gatt_logd(format, ...) do{if(GATT_DEBUG_LEVEL >= GATT_DEBUG_LEVEL_DEBUG)   BK_LOGI("app_gatt", "%s:" format "\n", __func__, ##__VA_ARGS__);} while(0)
#define gatt_logv(format, ...) do{if(GATT_DEBUG_LEVEL >= GATT_DEBUG_LEVEL_VERBOSE) BK_LOGI("app_gatt", "%s:" format "\n", __func__, ##__VA_ARGS__);} while(0)


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
    /// Connection interval minimum
    uint16_t intv_min;
    /// Connection interval maximum
    uint16_t intv_max;
    /// Latency
    uint16_t latency;
    /// Supervision timeout
    uint16_t time_out;
} ble_gap_evt_conn_param_update_t;

/**@brief Event structure for @ref FMNA_BLE_GAP_EVT_SEC_INFO_REQUEST. */
typedef struct
{
    uint16_t task_id;
} ble_gap_evt_sec_info_request_t;

/**@brief Event structure for @ref FMNA_BLE_GAP_EVT_INDICATE_CMP. */
typedef struct
{
  uint16_t          handle;                       /**< Attribute Handle. */
} ble_gatts_evt_indicate_cmp_t;

typedef struct
{
    uint8_t type;
    uint8_t conidx;                                     /**< Connection Handle on which event occurred. */
    union                                                     /**< union alternative identified by evt_id in enclosing struct. */
    {
        ble_gap_evt_connected_t                   connected;                    /**< Connected Event Parameters. */
        ble_gap_evt_disconnected_t                disconnected;                 /**< Disconnected Event Parameters. */
        ble_gap_evt_conn_param_update_t           conn_param_update;            /**< Connection Parameter Update Parameters. */
        ble_gap_evt_sec_info_request_t            sec_info_request;             /**< Security Information Request Event Parameters. */
        ble_gatts_evt_indicate_cmp_t              indicate_cmp;                 /**< indicate cmp Event Parameters. */
        ble_gap_evt_write_t                       write;
    } params;                                                                 /**< Event Parameters. */
} gatts_ble_gap_evt_t;






int ble_gatts_init(void);
void app_set_advertising_data(uint8_t *data, uint32_t len);
void app_set_adv_param(void);
void app_start_advertising(void);
void app_stop_advertising(void);
void app_delete_advertising(void);
void app_disconnect_cur_connect(uint8_t index);
void app_set_random_static_bt_addr(uint8_t *identity_addr);
void gatts_push_to_que(uint8_t type, void *param);
#endif

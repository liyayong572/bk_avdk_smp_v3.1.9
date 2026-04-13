#include <common/bk_include.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <components/log.h>
#include <os/mem.h>
#include <os/str.h>

#if CONFIG_BLE
#include <driver/flash.h>
#include <driver/flash_partition.h>
#include "flash_driver.h"
#include "gatts.h"
#include "fa00_server.h"
#include "f618_server.h"


#include "components/bluetooth/bk_dm_bluetooth_types.h"
#include "components/bluetooth/bk_dm_gap_ble_types.h"
#include "components/bluetooth/bk_ble_types.h"
#include "components/bluetooth/bk_ble.h"
#include "components/bluetooth/bk_dm_ble_types.h"


#define BEKEN_COMPANY_ID           (0x05F0)
#define TAG "ble_gatts"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)


static beken_queue_t gatts_msg_que = NULL;
static beken_thread_t gatts_thread_handle = NULL;
static beken_semaphore_t gatts_sema = NULL;
static uint8_t s_conn_ind = ~0;
static bk_ble_key_t s_ble_enc_key;

typedef void (* bk_gatts_cb_t)(ble_notice_t notice, void *param);
static bk_gatts_cb_t _gatts_cb_list[GATTS_CB_LIST_SIZE] = {NULL};

static int ble_gatts_register_callback(void* cb)
{
    if(cb == NULL)
    {
        return -1;
    }
    for (int i = 0; i < sizeof(_gatts_cb_list) / sizeof(_gatts_cb_list[0]); ++i)
    {
        if (!_gatts_cb_list[i])
        {
            _gatts_cb_list[i] = cb;
            return 0;
        }
    }
    return -2;
}

static void ble_gatts_cb(ble_notice_t notice, void *param)
{
    os_printf("ble_gatts_cb notice=%d \r\n", notice);
    switch (notice)
    {
        case BLE_5_STACK_OK:    
        break;
        case BLE_5_WRITE_EVENT:
        {
            ble_write_req_t *w_req = (ble_write_req_t *)param;
            
            if (_gatts_cb_list[w_req->prf_id])
            {
                _gatts_cb_list[w_req->prf_id](notice,param);
            }
            gatts_push_to_que(GATTS_WRITE_REQUEST,w_req);
            
        }break;
        case BLE_5_READ_EVENT:
        {
            ble_read_req_t *r_req = (ble_read_req_t *)param;
            
            if (_gatts_cb_list[r_req->prf_id])
            {
                _gatts_cb_list[r_req->prf_id](notice,param);
            }
            
        }break;
        case BLE_5_CONNECT_EVENT:
        {
            ble_conn_ind_t *c_ind = (ble_conn_ind_t *)param;
            os_printf("c_ind:conn_idx:%d, addr_type:%d, peer_addr:%02x:%02x:%02x:%02x:%02x:%02x\r\n",
                      c_ind->conn_idx, c_ind->peer_addr_type, c_ind->peer_addr[0], c_ind->peer_addr[1],
                      c_ind->peer_addr[2], c_ind->peer_addr[3], c_ind->peer_addr[4], c_ind->peer_addr[5]);
            s_conn_ind = c_ind->conn_idx;
            for(int i = 0; i < GATTS_CB_LIST_SIZE; i++)
            {
                if(_gatts_cb_list[i])
                {
                    _gatts_cb_list[i](notice,param);
                }
            }
            gatts_push_to_que(GATTS_CONNECT,c_ind);
            bk_ble_gatt_mtu_change(c_ind->conn_idx);
            uint16_t manu_uuid=GATT_MANUFACTURER_NAME_CHARACTERISTIC;
            bk_ble_gattc_read_by_uuid(c_ind->conn_idx,0x01,0xffff,(uint8_t *)&manu_uuid,2);
        }break;
        case BLE_5_DISCONNECT_EVENT:
        {
            ble_discon_ind_t *d_ind = (ble_discon_ind_t *)param;
            os_printf("d_ind:conn_idx:%d,reason:%d\r\n", d_ind->conn_idx, d_ind->reason);
            s_conn_ind = ~0;

            for(int i = 0; i < GATTS_CB_LIST_SIZE; i++)
            {
                if(_gatts_cb_list[i])
                {
                    _gatts_cb_list[i](notice,param);
                }
            }
            gatts_push_to_que(GATTS_DISCONNECT,d_ind);
        }break;
        case BLE_5_REPORT_ADV:
        break;
        case BLE_5_MTU_CHANGE:
        {
            ble_mtu_change_t *m_ind = (ble_mtu_change_t *)param;
            os_printf("%s m_ind:conn_idx:%d, mtu_size:%d\r\n", __func__, m_ind->conn_idx, m_ind->mtu_size);
        }break;
        case BLE_5_ATT_INFO_REQ:
        {
            ble_att_info_req_t *a_ind = (ble_att_info_req_t *)param;
            os_printf("a_ind:conn_idx:%d\r\n", a_ind->conn_idx);
            a_ind->length = 128;
            a_ind->status = BK_ERR_BLE_SUCCESS;
        }break;
        case BLE_5_CREATE_DB:
        {
            ble_create_db_t *cd_ind = (ble_create_db_t *)param;
            os_printf("cd_ind:prf_id:%d, status:%d\r\n", cd_ind->prf_id, cd_ind->status);
             if(_gatts_cb_list[cd_ind->prf_id])
            {
                os_printf("start_hdl:%d\r\n", cd_ind->start_hdl);
                _gatts_cb_list[cd_ind->prf_id](notice,param);
            }
        }break;
        case BLE_5_TX_DONE:
        {
            bk_ble_gatt_cmp_evt_t *event = (bk_ble_gatt_cmp_evt_t *)param;
            
            if (_gatts_cb_list[event->prf_id])
            {
                _gatts_cb_list[event->prf_id](notice,param);
            }
            
        }break;
        case BLE_5_PAIRING_REQ:
        {
            os_printf("BLE_5_PAIRING_REQ\r\n");
            ble_smp_ind_t *s_ind = (ble_smp_ind_t *)param;
            bk_ble_sec_send_auth_mode_ext(s_ind->conn_idx, GAP_AUTH_REQ_NO_MITM_BOND, BK_BLE_GAP_IO_CAP_NO_INPUT_NO_OUTPUT,
                                      GAP_SEC1_NOAUTH_PAIR_ENC, GAP_OOB_AUTH_DATA_NOT_PRESENT,
                                      BK_BLE_GAP_KDIST_ENCKEY | BK_BLE_GAP_KDIST_IDKEY, BK_BLE_GAP_KDIST_ENCKEY | BK_BLE_GAP_KDIST_IDKEY);
        }break;
        case BLE_5_PAIRING_SUCCEED:
        {
            bk_printf("BLE_5_PAIRING_SUCCEED\r\n");
            for(int i = 0; i < GATTS_CB_LIST_SIZE; i++)
            {
                if(_gatts_cb_list[i])
                {
                    _gatts_cb_list[i](notice,param);
                }
            }
        }break;
        case BLE_5_PAIRING_FAILED:
        {
            bk_printf("BLE_5_PAIRING_FAILED\r\n");
            os_memset(&s_ble_enc_key, 0, sizeof(s_ble_enc_key));
            
        }break;
        case BLE_5_PARING_PASSKEY_REQ:
            break;
        case BLE_5_ENCRYPT_EVENT:
            bk_printf("BLE_5_ENCRYPT_EVENT\r\n");
            for(int i = 0; i < GATTS_CB_LIST_SIZE; i++)
            {
                if(_gatts_cb_list[i])
                {
                    _gatts_cb_list[i](notice,param);
                }
            }
        break;
        case BLE_5_INIT_CONNECT_EVENT:
        {
            ble_conn_ind_t *c_ind = (ble_conn_ind_t *)param;
            os_printf("BLE_5_INIT_CONNECT_EVENT:conn_idx:%d, addr_type:%d, peer_addr:%02x:%02x:%02x:%02x:%02x:%02x\r\n",
                      c_ind->conn_idx, c_ind->peer_addr_type, c_ind->peer_addr[0], c_ind->peer_addr[1],
                      c_ind->peer_addr[2], c_ind->peer_addr[3], c_ind->peer_addr[4], c_ind->peer_addr[5]);
        }break;
        case BLE_5_INIT_DISCONNECT_EVENT:
        {
            ble_discon_ind_t *d_ind = (ble_discon_ind_t *)param;
            os_printf("BLE_5_INIT_DISCONNECT_EVENT:conn_idx:%d,reason:%d\r\n", d_ind->conn_idx, d_ind->reason);
        }break;
        case BLE_5_INIT_CONNECT_FAILED_EVENT:
        case BLE_5_SDP_REGISTER_FAILED:
            break;
        case BLE_5_READ_PHY_EVENT:
        {
            ble_read_phy_t *phy_param = (ble_read_phy_t *)param;
            os_printf("BLE_5_READ_PHY_EVENT:tx_phy:0x%02x, rx_phy:0x%02x\r\n", phy_param->tx_phy, phy_param->rx_phy);
        }break;
        case BLE_5_CONN_UPDATA_EVENT:
        {
            ble_conn_param_t *updata_param = (ble_conn_param_t *)param;
            os_printf("BLE_5_CONN_UPDATA_EVENT:conn_interval:0x%04x, con_latency:0x%04x, sup_to:0x%04x\r\n", updata_param->intv_max,
                      updata_param->con_latency, updata_param->sup_to);
        }break;
        case BLE_5_PERIODIC_SYNC_CMPL_EVENT:
        case BLE_5_DISCOVERY_PRIMARY_SERVICE_EVENT:
        case BLE_5_DISCOVERY_CHAR_EVENT:
        case BLE_5_RECV_NOTIFY_EVENT:
        case BLE_5_ATT_READ_RESPONSE:
        case BLE_5_CONN_UPD_PAR_ASK:
        case BLE_5_SHUTDOWN_SUCCEED:
        case BLE_5_DELETE_SERVICE_DONE:
        break;
        case BLE_5_GAP_CMD_CMP_EVENT:
        {
            ble_cmd_cmp_evt_t *event = (ble_cmd_cmp_evt_t *)param;

            switch (event->cmd)
            {
                case BLE_CONN_ENCRYPT:
                {
                    bk_printf("BLE_5_GAP_CMD_CMP_EVENT(BLE_CONN_ENCRYPT) , status %x\r\n", event->status);
                    if (event->status)
                    {
                        os_memset(&s_ble_enc_key, 0, sizeof(s_ble_enc_key));
                        bk_ble_disconnect(event->conn_idx);
                    }
                }
                break;
                default:
                    break;
            }
        }break;
        
        case BLE_5_ADV_STOPPED_EVENT:
        case BLE_5_READ_RSSI_CMPL_EVENT:
        case BLE_5_KEY_EVENT:
        case BLE_5_BOND_INFO_REQ_EVENT:
        case BLE_5_READ_BLOB_EVENT:
        case BLE_5_PAIRING_SECURITY_REQ_EVENT:
        case BLE_5_PARING_NUMBER_COMPARE_REQ_EVENT:
        case BLE_5_SCAN_STOPPED_EVENT:

        default:
            break;
    }
}

static void ble_gatts_cmd_cb(ble_cmd_t cmd, ble_cmd_param_t *param)
{
    if(0== param->status)
    {
        switch (cmd)
        {
            case BLE_CREATE_ADV:
            case BLE_SET_ADV_DATA:
            case BLE_SET_RSP_DATA:
            case BLE_START_ADV:
            case BLE_STOP_ADV:
            case BLE_CREATE_SCAN:
            case BLE_START_SCAN:
            case BLE_STOP_SCAN:
            case BLE_INIT_CREATE:
            case BLE_INIT_START_CONN:
            case BLE_INIT_STOP_CONN:
            case BLE_CONN_DIS_CONN:
            case BLE_CONN_UPDATE_PARAM:
            case BLE_DELETE_ADV:
            case BLE_DELETE_SCAN:
            case BLE_CONN_READ_PHY:
            case BLE_CONN_SET_PHY:
            case BLE_CONN_UPDATE_MTU:
            case BLE_SET_ADV_RANDOM_ADDR:
                if (gatts_sema != NULL)
                    rtos_set_semaphore( &gatts_sema );
                break;
            default:
                break;
        }
    }
    else
    {
        os_printf("error param->status=%x\n",param->status);
    }

}

static void ble_gatts_set_dev_name(void)
{
    bk_err_t ret = BK_FAIL;
    uint8_t dev_name_data[32] = {0};
    ret = sprintf((char *)&dev_name_data[0], "%s",ADV_NAME_HEAD);

    bk_ble_appm_set_dev_name(ret, &dev_name_data[0]);
}

static void ble_gatts_set_adv_param(void)
{
    bk_err_t ret = BK_FAIL;
    ble_adv_param_t adv_param;
    int actv_idx = 0;

    os_memset(&adv_param, 0, sizeof(ble_adv_param_t));
    adv_param.chnl_map = ADV_ALL_CHNLS;
    adv_param.own_addr_type = OWN_ADDR_TYPE_PUBLIC_ADDR;//OWN_ADDR_TYPE_RANDOM_ADDR;
    adv_param.adv_type = ADV_TYPE_LEGACY;
    adv_param.adv_prop = ADV_PROP_CONNECTABLE_BIT | ADV_PROP_SCANNABLE_BIT;
    adv_param.prim_phy = INIT_PHY_TYPE_LE_1M;
    adv_param.second_phy = INIT_PHY_TYPE_LE_1M;

    adv_param.adv_intv_min = 160;
    adv_param.adv_intv_max = 160;
    


    ret = bk_ble_create_advertising(actv_idx, &adv_param, ble_gatts_cmd_cb);
    if (ret != BK_ERR_BLE_SUCCESS)
    {
        os_printf("config adv paramters failed %d\n", ret);
        goto error;
    }

    ret = rtos_get_semaphore(&gatts_sema, SYNC_CMD_TIMEOUT_MS);
    if (ret != BK_OK)
    {
        os_printf("wait semaphore failed at %d, %d\n", ret, __LINE__);
        goto error;
    }
    else
    {
        os_printf("set adv paramters success\n");
    }
    return;
    error:
    os_printf("%s fail \n", __func__);
}

static void ble_gatts_set_adv_random_addr(uint8_t *random_addr)
{
    bk_err_t ret = BK_FAIL;
    uint8_t actv_idx=0;

    ret = bk_ble_set_adv_random_addr(actv_idx, (uint8_t *)random_addr, ble_gatts_cmd_cb);
    if (ret != BK_ERR_BLE_SUCCESS)
    {
        os_printf("set adv random addr failed %d\n", ret);
        goto error;
    }

    ret = rtos_get_semaphore(&gatts_sema, SYNC_CMD_TIMEOUT_MS);
    if (ret != BK_OK)
    {
        os_printf("wait adv random addr semaphore failed at %d, %d\n", ret, __LINE__);
        goto error;
    }
    else
    {
        os_printf("set adv random addr success\n");
    }
    return;
    error:
    os_printf("%s fail \n", __func__);
}

static void ble_gatts_set_advertising_data(void)
{
    bk_err_t ret = BK_FAIL;
    uint8_t adv_data[ADV_MAX_SIZE] = {0};
    uint8_t adv_index = 0;
    uint8_t len_index = 0;
    uint8_t actv_idx=0;

    /* flags */
    len_index = adv_index;
    adv_data[adv_index++] = 0x00;
    adv_data[adv_index++] = BK_BLE_AD_TYPE_FLAG;
    adv_data[adv_index++] = 0x06;
    adv_data[len_index] = 2;

    /* 16bit uuid */
    len_index = adv_index;
    adv_data[adv_index++] = 0x00;
    adv_data[adv_index++] = BK_BLE_AD_TYPE_SERVICE_DATA;
    adv_data[adv_index++] = BOARDING_UUID & 0xFF;
    adv_data[adv_index++] = BOARDING_UUID >> 8;
    adv_data[len_index] = 3;
    /* manufacturer */
    len_index = adv_index;
    adv_data[adv_index++] = 0x00;
    adv_data[adv_index++] = BK_BLE_AD_TYPE_MANU;
    adv_data[adv_index++] = BEKEN_COMPANY_ID & 0xFF;
    adv_data[adv_index++] = BEKEN_COMPANY_ID >> 8;
    adv_data[len_index] = 3;

    // name
    len_index = adv_index;
    adv_data[adv_index++] = 0x00;
    adv_data[adv_index++] = BK_BLE_AD_TYPE_NAME_CMPL;
    ret = sprintf((char *)&adv_data[adv_index], "%s", ADV_NAME_HEAD);
    adv_index += ret;
    adv_data[len_index] = ret + 1;

    /* set adv data */
    ret = bk_ble_set_adv_data(actv_idx, adv_data, adv_index, ble_gatts_cmd_cb);
    if (ret != BK_ERR_BLE_SUCCESS)
    {
        os_printf("set adv data failed %d\n", ret);
        goto error;
    }

    ret = rtos_get_semaphore(&gatts_sema, SYNC_CMD_TIMEOUT_MS);
    if (ret != BK_OK)
    {
        os_printf("wait semaphore failed at %d, %d\n", ret, __LINE__);
        goto error;
    }
    else
    {
        os_printf("set adv data success\n");
    }

    os_printf("%s success\n", __func__);
    return;
error:
    os_printf("%s fail \n", __func__);
}

static void ble_gatts_set_scan_rsp_data(void)
{
    bk_err_t ret = BK_FAIL;
    uint8_t scan_rsp_data[32] = {0};
    uint8_t adv_index = 0;
    uint8_t actv_idx=0;
    uint8_t name_len = 0;
    
    /* 检查ADV_NAME_HEAD是否为NULL */
    if (ADV_NAME_HEAD == NULL) {
        os_printf("%s: ADV_NAME_HEAD is NULL\n", __func__);
        return;
    }
    
    /* 设置AD结构类型：完整的本地名称 */
    scan_rsp_data[++adv_index] = BK_BLE_AD_TYPE_NAME_CMPL;
    
    /* 复制设备名称到扫描响应数据中，并检查长度防止溢出 */
    name_len = os_strlen(ADV_NAME_HEAD);
    if (name_len > (sizeof(scan_rsp_data) - adv_index - 1)) {
        name_len = sizeof(scan_rsp_data) - adv_index - 1;
        os_printf("%s: Device name truncated to %d bytes\n", __func__, name_len);
    }
    adv_index++;
    os_memcpy(&scan_rsp_data[adv_index], ADV_NAME_HEAD, name_len);
    adv_index += name_len;

    
    /* 设置AD结构的长度字段：类型字节 + 数据字节数 */
    scan_rsp_data[0] = adv_index;
    
    /* 检查广播数据总长度是否超过BLE规定的最大长度（31字节） */
    if (adv_index + 1 > 31) {
        os_printf("%s: BLE scan response data length exceeds maximum allowed (31 bytes)\n", __func__);
        return;
    }
    
    os_printf("scan_rsp_data:\n");
    for(int i=0;i<=adv_index;i++)
    {
        os_printf("0x%02x ",scan_rsp_data[i]);
    }
    os_printf("\n");
    
    /* 设置扫描响应数据 */
    ret = bk_ble_set_scan_rsp_data(actv_idx, scan_rsp_data, adv_index + 1, ble_gatts_cmd_cb);
    if (ret != BK_ERR_BLE_SUCCESS)
    {
        os_printf("set rsp adv data failed %d\n", ret);
        goto error;
    }

    ret = rtos_get_semaphore(&gatts_sema, SYNC_CMD_TIMEOUT_MS);
    if (ret != BK_OK)
    {
        os_printf("wait semaphore failed at %d, %d\n", ret, __LINE__);
        goto error;
    }
    else
    {
        os_printf("set rsp adv data success\n");
    }
    return;
    error:
    os_printf("%s fail \n", __func__);
}

static void ble_gatts_start_adv(void)
{
    bk_err_t ret = BK_FAIL;
    uint8_t actv_idx=0;
    
    ret = bk_ble_start_advertising(actv_idx, 0, ble_gatts_cmd_cb);
    if (ret != BK_ERR_BLE_SUCCESS)
    {
        os_printf("start adv failed %d\n", ret);
        goto error;
    }

    ret = rtos_get_semaphore(&gatts_sema, SYNC_CMD_TIMEOUT_MS);
    if (ret != BK_OK)
    {
        os_printf("wait semaphore failed at %d, %d\n", ret, __LINE__);
        goto error;
    }
    else
    {
        os_printf("start adv success\n");
    }

    os_printf("%s success\n", __func__);
    return;
error:
    os_printf("%s fail \n", __func__);
}

static void ble_gatts_stop_adv(void)
{
    bk_err_t ret = BK_FAIL;
    uint8_t actv_idx=0;
    
    ret = bk_ble_stop_advertising(actv_idx, ble_gatts_cmd_cb);
    if (ret != BK_ERR_BLE_SUCCESS)
    {
        os_printf("stop adv failed %d\n", ret);
        goto error;
    }

    ret = rtos_get_semaphore(&gatts_sema, SYNC_CMD_TIMEOUT_MS);
    if (ret != BK_OK)
    {
        os_printf("wait semaphore failed at %d, %d\n", ret, __LINE__);
        goto error;
    }
    else
    {
        os_printf("stop adv success\n");
    }

    os_printf("%s success\n", __func__);
    return;
error:
    os_printf("%s fail \n", __func__);
}

static void ble_gatts_delete_adv(void)
{
    bk_err_t ret = BK_FAIL;
    uint8_t actv_idx=0;
    
    ret = bk_ble_delete_advertising(actv_idx, ble_gatts_cmd_cb);
    if (ret != BK_ERR_BLE_SUCCESS)
    {
        os_printf("delete adv failed %d\n", ret);
        goto error;
    }

    ret = rtos_get_semaphore(&gatts_sema, SYNC_CMD_TIMEOUT_MS);
    if (ret != BK_OK)
    {
        os_printf("wait semaphore failed at %d, %d\n", ret, __LINE__);
        goto error;
    }
    else
    {
        os_printf("delete adv success\n");
    }

    os_printf("%s success\n", __func__);
    return;
error:
    os_printf("%s fail \n", __func__);
}

static void gatts_main(void)
{
    while (1)
    {
        bk_err_t err;
        gatts_ble_gap_evt_t msg;

        os_memset(&msg, 0, sizeof(msg));
        err = rtos_pop_from_queue(&gatts_msg_que, &msg, BEKEN_WAIT_FOREVER);

        if (err == kNoErr)
        {
            switch (msg.type)
            {
                case GATTS_CONNECT:
                {
                    os_printf("BLE CONNECT\n");
                }break;
                case GATTS_DISCONNECT:
                {
                    os_printf("BLE DISCONNECT\n");
                    ble_gatts_start_adv();
                }break;
                case GATTS_WRITE_REQUEST:
                {
                    uint16_t len = msg.params.write.len;
                    uint8_t *data = (uint8_t *)msg.params.write.data;
                    uint16_t gatts_idx = msg.params.write.att_idx;
                    uint16_t prf_id = msg.params.write.prf_id;
                    os_printf("prf_id=%x,gatts_idx=%x\n",prf_id,gatts_idx);
                    for (uint16_t i = 0; i < len; i++)
                        os_printf("%02x,", data[i]);
                    
                    #if LOOP_BACK_TEST
                    if(prf_id==PRF_TASK_ID_FA00)
                    {
                        if(FA00_IDX_EA02_VAL==gatts_idx)
                        {
                            fa00_notify_ea01(0,data,len);
                        }
                    }
                    else if(prf_id==PRF_TASK_ID_F618)
                    {
                        if(F618_IDX_B002_VAL==gatts_idx)
                        {
                            f618_notify_b001(0,data,len);
                        }
                    }
                    #endif
                }break;
              
            }
        }
    }
    // never executed normally
    if (gatts_msg_que)
    {
        rtos_deinit_queue(&gatts_msg_que);
        gatts_msg_que = NULL;
    }
    gatts_thread_handle = NULL;
    rtos_delete_thread(NULL);
}

static int gatts_task_init(void)
{
    bk_err_t ret = 0;
    if ((!gatts_thread_handle) && (!gatts_msg_que))
    {
        ret = rtos_init_queue(&gatts_msg_que,"gatts_msg_que",sizeof(gatts_ble_gap_evt_t), 5);
        if (ret != kNoErr)
        {
            LOGE("msg queue init failed");
            return BK_FAIL;
        }

        ret = rtos_create_thread(&gatts_thread_handle,
                                 5,
                                 "gatts_main",
                                 (beken_thread_function_t)gatts_main,
                                 2048,
                                 (beken_thread_arg_t)0);
        if (ret != kNoErr)
        {
            LOGE("control point write task create failed");
            rtos_deinit_queue(&gatts_msg_que);
            gatts_msg_que = NULL;
            gatts_thread_handle = NULL;
            return BK_FAIL;
        }

        return kNoErr;
    }
    else
    {
        LOGI("control task already init");
        return kInProgressErr;
    }
}
void gatts_push_to_que(uint8_t type, void *param)
{
    ble_err_t ret = 0;
    gatts_ble_gap_evt_t msg;
    os_memset(&msg, 0, sizeof(msg));
    msg.type = type;

    switch (type)
    {
        case GATTS_WRITE_REQUEST:
        {
            ble_write_req_t *write_req = (ble_write_req_t *)param;
            
            msg.conidx=write_req->conn_idx;
            msg.params.write.prf_id=write_req->prf_id;
            msg.params.write.att_idx = write_req->att_idx;
            msg.params.write.len    = write_req->len;
            msg.params.write.data   = NULL;

            if (msg.params.write.len)
            {
                msg.params.write.data = os_malloc(msg.params.write.len);
                if (msg.params.write.data == NULL)
                {
                    LOGE("malloc error for write data");
                    return;
                }
                os_memcpy(msg.params.write.data, write_req->value, msg.params.write.len);
            }
        }
        break;

        case GATTS_CONNECT:
        {
            ble_conn_ind_t *c_ind = (ble_conn_ind_t *)param;
            
            msg.conidx = c_ind->conn_idx;
            msg.params.connected.con_interval = c_ind->con_interval;
        }
        break;

        case GATTS_DISCONNECT:
        {
            ble_discon_ind_t *c_ind = (ble_discon_ind_t *)param;

            msg.conidx = c_ind->conn_idx;
            msg.params.disconnected.reason = c_ind->reason;
        }
        break;

        default:
            LOGE("unknown message type %d", type);
            return;
    }

    if (gatts_msg_que)
    {
        ret = rtos_push_to_queue(&gatts_msg_que, &msg, 100);
        if (ret != kNoErr)
        {
            LOGE("rtos_push_to_queue error");

            if (msg.type == GATTS_WRITE_REQUEST && msg.params.write.data)
            {
                os_free(msg.params.write.data);
                msg.params.write.data = NULL;
            }
        }
    }
    else
    {
        LOGE("gatts_msg_que is NULL");

        if (msg.type == GATTS_WRITE_REQUEST && msg.params.write.data)
        {
            os_free(msg.params.write.data);
            msg.params.write.data = NULL;
        }
    }
}
void ble_gattc_read_charac_cb(CHAR_TYPE type, uint8 conidx, uint16_t hdl, uint16_t len, uint8 *data)
{
    LOGD("%s,type:%x conidx:%d,handle:0x%02x(%d),len:%d,0x",__func__, type, conidx, hdl, hdl, len);

    LOGD("%s \n", data);
    if(data!=NULL && len>0)
    {

        if(0==memcmp("Apple Inc",data,len-1))
        {
            gatt_logi("the host is IOS device");
        }
    }

}
int ble_gatts_init()
{
    bk_err_t ret = BK_FAIL;
    ret = rtos_init_semaphore(&gatts_sema, 1);
    if(ret != BK_OK)
    {
        os_printf("gatts init sema fial \n");
        return ret;
    }
    rtos_delay_milliseconds(100);

    bk_ble_set_notice_cb(ble_gatts_cb);
    ble_gatts_register_callback(fa00_gatts_cb);
    fa00_init();

    ble_gatts_register_callback(f618_gatts_cb);
    f618_init();

    ble_gatts_set_dev_name();
    ble_gatts_set_adv_param();
    //ble_gatts_set_adv_random_addr();
    ble_gatts_set_advertising_data();
    ble_gatts_set_scan_rsp_data();
    ble_gatts_start_adv();
    gatts_task_init();

    bk_ble_set_max_mtu(517);
    extern void register_app_sdp_charac_callback(app_sdp_charac_callback cb);
    register_app_sdp_charac_callback(ble_gattc_read_charac_cb);
    return BK_OK;
}


void app_set_adv_param(void)
{
    ble_gatts_set_adv_param();
    //ble_gatts_set_scan_rsp_data();
}

void app_start_advertising(void)
{
    ble_gatts_start_adv();
}

void app_stop_advertising(void)
{
    ble_gatts_stop_adv();
}

void app_delete_advertising(void)
{
    ble_gatts_delete_adv();
}

void app_disconnect_cur_connect(uint8_t index)
{
    os_printf("%s,%d\n", __func__,index);
    bk_ble_disconnect(index);
}

void app_set_random_static_bt_addr(uint8_t *identity_addr)
{
    ble_gatts_set_adv_random_addr(identity_addr);
}

#endif
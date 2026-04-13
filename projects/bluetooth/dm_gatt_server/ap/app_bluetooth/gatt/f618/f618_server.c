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
#include "dm_gatts.h"
#include "dm_gatt_connection.h"
#include "dm_gap_utils.h"
#include "dm_gatt.h"
#include "f618_server.h"
#include "bk_cli.h"

#define CLI_TEST                    1
#define LOOP_BACK_TEST              0
#define CHAR_BUFFER_SIZE            512
#define INVALID_ATTR_HANDLE         0
#define F618_SERVER_UUID            0xF618
#define F618_SERVER_UUID_CHAR_B002  0xB002
#define F618_SERVER_UUID_CHAR_B001  0xB001
#define MIN_VALUE(x, y) (((x) < (y)) ? (x): (y))
#ifndef SYNC_CMD_TIMEOUT_MS
#define SYNC_CMD_TIMEOUT_MS         4000
#endif

#define USE_128_UUID            1
const uint8_t F618_SERVER_UUID_128[16]= 
{0xF6,0x18,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e};
const uint8_t F618_SERVER_UUID_128_CHAR_B002[16] =  
{0xB0,0x02,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e};
const uint8_t F618_SERVER_UUID_128_CHAR_B001[16] = 
{0xB0,0x01,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e};

enum 
{
    F618_HDL_IDX_SVC=0,
    F618_HDL_IDX_B002,
    F618_HDL_IDX_B001,
    F618_HDL_IDX_B001_DESC,
    F618_HDL_IDX_MAX,
};


static beken_semaphore_t f618_sema = NULL;
static uint8_t _char_b002_buffer[CHAR_BUFFER_SIZE];
static uint8_t _char_b001_buffer[CHAR_BUFFER_SIZE];
static uint8_t _char_b001_desc_buffer[2];
static uint8_t s_gatts_if=0;
static uint16_t gatts_conn_id=0;
static uint16_t _server_f618_attr_handle_list[F618_HDL_IDX_MAX] = {0};

#if USE_128_UUID
static const bk_gatts_attr_db_t _f618_attr_db_service[] =
{
    //service
    {
        BK_GATT_PRIMARY_SERVICE_DECL_128(F618_SERVER_UUID_128),
    },

    //char 1
    {
        BK_GATT_CHAR_DECL_128(F618_SERVER_UUID_128_CHAR_B002,
                          CHAR_BUFFER_SIZE, _char_b002_buffer,
                          //BK_GATT_CHAR_PROP_BIT_READ | BK_GATT_CHAR_PROP_BIT_WRITE_NR | BK_GATT_CHAR_PROP_BIT_WRITE,
                          BK_GATT_CHAR_PROP_BIT_WRITE_NR,
                          //BK_GATT_PERM_READ | BK_GATT_PERM_WRITE,
                          BK_GATT_PERM_READ,
                          BK_GATT_RSP_BY_APP),
    },

    //char 2
    {
        BK_GATT_CHAR_DECL_128(F618_SERVER_UUID_128_CHAR_B001,
                          CHAR_BUFFER_SIZE, _char_b001_buffer,
                          BK_GATT_CHAR_PROP_BIT_READ |BK_GATT_CHAR_PROP_BIT_NOTIFY ,
                          BK_GATT_PERM_READ ,
                          BK_GATT_RSP_BY_APP),
    },
    {
        BK_GATT_CHAR_DESC_DECL(BK_GATT_UUID_CHAR_CLIENT_CONFIG,
                               sizeof(_char_b001_desc_buffer), _char_b001_desc_buffer,
                               BK_GATT_PERM_READ | BK_GATT_PERM_WRITE,
                               BK_GATT_RSP_BY_APP),
    },

};
#else
static const bk_gatts_attr_db_t _f618_attr_db_service[] =
{
    //service
    {
        BK_GATT_PRIMARY_SERVICE_DECL(F618_SERVER_UUID),
    },

    //char 1
    {
        BK_GATT_CHAR_DECL(F618_SERVER_UUID_CHAR_B002,
                          CHAR_BUFFER_SIZE, _char_b002_buffer,
                          //BK_GATT_CHAR_PROP_BIT_READ | BK_GATT_CHAR_PROP_BIT_WRITE_NR | BK_GATT_CHAR_PROP_BIT_WRITE,
                          BK_GATT_CHAR_PROP_BIT_WRITE_NR,
                          //BK_GATT_PERM_READ | BK_GATT_PERM_WRITE,
                          BK_GATT_PERM_READ,
                          BK_GATT_RSP_BY_APP),
    },

    //char 2
    {
        BK_GATT_CHAR_DECL(F618_SERVER_UUID_CHAR_B001,
                          CHAR_BUFFER_SIZE, _char_b001_buffer,
                          BK_GATT_CHAR_PROP_BIT_READ |BK_GATT_CHAR_PROP_BIT_NOTIFY ,
                          BK_GATT_PERM_READ ,
                          BK_GATT_RSP_BY_APP),
    },
    {
        BK_GATT_CHAR_DESC_DECL(BK_GATT_UUID_CHAR_CLIENT_CONFIG,
                               sizeof(_char_b001_desc_buffer), _char_b001_desc_buffer,
                               BK_GATT_PERM_READ | BK_GATT_PERM_WRITE,
                               BK_GATT_RSP_BY_APP),
    },

};


#endif

static int32_t _f618_gatts_cb(bk_gatts_cb_event_t event, bk_gatt_if_t gatts_if, bk_ble_gatts_cb_param_t *param);
static int _match_ble_handle(uint16_t handle);
static int _cli_ble_f618_init(void);

int f618_server_init(void)
{
    ble_err_t ret = 0;

    // init semaphore if not inited
    if (f618_sema == NULL) 
    {
        ret = rtos_init_semaphore(&f618_sema, 1);
        if (ret != 0)
        {
            gatt_loge("f618_init_semaphore err %d", ret);
            return -1;
        }
    }
    // register gatts callback once
    dm_gatts_add_gatts_callback(_f618_gatts_cb);

    //2. create server attr
    ret = bk_ble_gatts_create_attr_tab((void *)&_f618_attr_db_service, s_gatts_if, F618_HDL_IDX_MAX, 30);
    if (ret != BK_GATT_OK)
    {
        gatt_loge("f618: create_attr_tab fail %d", ret);
        return -1;
    }
    bk_ble_gatts_start_service(_server_f618_attr_handle_list[F618_HDL_IDX_SVC]);
#if CLI_TEST
    _cli_ble_f618_init();
#endif
    return 0;
}

int f618_ntf_data(uint8_t *buffer, uint16_t len)
{
    uint16_t handle = _server_f618_attr_handle_list[F618_HDL_IDX_B001];
    if (handle == INVALID_ATTR_HANDLE) 
    {
        gatt_loge("f618_ntf_data invalid handle");
        return -1;
    }
    if (buffer == NULL || len == 0) 
    {
        gatt_loge("f618_ntf_data invalid params");
        return -1;
    }
    if (gatts_conn_id == 0) 
    {
        gatt_loge("f618_ntf_data no connection");
        return -1;
    }
    bk_err_t err = bk_ble_gatts_send_indicate(s_gatts_if, (uint16_t)gatts_conn_id, handle, len, buffer, 0);
    if (err != BK_GATT_OK)
    {
        gatt_loge("send_indicate fail %d", err);
        return -1;
    }

    err = rtos_get_semaphore(&f618_sema, SYNC_CMD_TIMEOUT_MS);
    if (err != kNoErr)
    {
        gatt_loge("f618 indicate timeout %d", err);
        return -1;
    }

    return 0;
}
static void f618_receive_write_data(uint16_t handle,uint8_t *buffer, uint16_t len)
{
    if (buffer == NULL || len == 0) return;
    
    for (uint16_t i = 0; i < len; i++)
    {
        gatt_logi("%02x", buffer[i]);
    }

#if(LOOP_BACK_TEST)
    /* for test, */
    f618_ntf_data(buffer, len);
#endif
}

static int _match_ble_handle(uint16_t handle)
{
    for(int i=0;i<F618_HDL_IDX_MAX;i++)
    {
        if(handle == _server_f618_attr_handle_list[i])
        {
            return i;
        }
    }
    return -1;
}

static int32_t _f618_gatts_cb(bk_gatts_cb_event_t event, bk_gatt_if_t gatts_if, bk_ble_gatts_cb_param_t *param)
{
    gatt_logi("event=%d",event);
    switch(event)
    {
        case BK_GATTS_REG_EVT:
        {
            s_gatts_if = gatts_if;
            gatt_logi("BK_GATTS_REG_EVT saved gatts_if=%d", s_gatts_if);
        } break;
        case BK_GATTS_START_EVT:
        {
            if(param->start.service_handle == _server_f618_attr_handle_list[F618_HDL_IDX_SVC] && 
              _server_f618_attr_handle_list[F618_HDL_IDX_SVC] != INVALID_ATTR_HANDLE)
            {
                gatt_logi("BK_GATTS_START_EVT");
            }
        }break;
        
        case BK_GATTS_STOP_EVT:
        {
            if(param->stop.service_handle == _server_f618_attr_handle_list[F618_HDL_IDX_SVC] && 
              _server_f618_attr_handle_list[F618_HDL_IDX_SVC] != INVALID_ATTR_HANDLE)
            {
                gatt_logi("BK_GATTS_STOP_EVT");
            }
        }break;
        case BK_GATTS_CONNECT_EVT:
        {
            gatts_conn_id = param->connect.conn_id;
            gatt_logi("BK_GATTS_CONNECT_EVT conn_id=%d", gatts_conn_id);
        }break;

        case BK_GATTS_DISCONNECT_EVT:
        {
            gatt_logi("BK_GATTS_DISCONNECT_EVT conn_id=%d", param->disconnect.conn_id);
            if (param->disconnect.conn_id == gatts_conn_id)
            {
                gatts_conn_id = 0;
            }

        } break;
        case BK_GATTS_CREAT_ATTR_TAB_EVT:
        {
            if(param->add_attr_tab.status == BK_GATT_OK)
            {
            #if USE_128_UUID
                if (param->add_attr_tab.svc_uuid.len == BK_UUID_LEN_128 &&
                     memcmp(param->add_attr_tab.svc_uuid.uuid.uuid128,F618_SERVER_UUID_128,BK_UUID_LEN_128) == 0)
                {
                    for (uint16_t i = 0; i < param->add_attr_tab.num_handle; i++)
                    {
                        _server_f618_attr_handle_list[i] = param->add_attr_tab.handles[i];
                    }
                    gatt_logi("BK_GATTS_CREAT_ATTR_TAB_EVT");
                    gatt_logi("num_handle= %d",param->add_attr_tab.num_handle);
                }

            #else
                if(param->add_attr_tab.svc_uuid.len == BK_UUID_LEN_16 && param->add_attr_tab.svc_uuid.uuid.uuid16 == F618_SERVER_UUID)
                {
                    gatt_logi("BK_GATTS_CREAT_ATTR_TAB_EVT");
                    gatt_logi("num_handle= %d",param->add_attr_tab.num_handle);
                    for(uint16_t i=0;i<param->add_attr_tab.num_handle;i++)
                    {
                        _server_f618_attr_handle_list[i] = param->add_attr_tab.handles[i];
                    }
                }
            #endif
            }
        }break;
        case BK_GATTS_WRITE_EVT:
        {
            int idx = _match_ble_handle(param->write.handle);
            if(idx != -1)
            {
                bk_gatt_rsp_t rsp;
                uint16_t final_len = 0;
                os_memset(&rsp, 0, sizeof(rsp));
                if(idx == F618_HDL_IDX_B002)
                {
                    final_len = MIN_VALUE(param->write.len, sizeof(_char_b002_buffer) - param->write.offset);
                    os_memcpy(_char_b002_buffer + param->write.offset, param->write.value, final_len);
                    gatt_logi("b002 recv %d",param->write.len);
                    if (param->write.need_rsp)
                    {
                        rsp.attr_value.auth_req = BK_GATT_AUTH_REQ_NONE;
                        rsp.attr_value.handle = param->write.handle;
                        rsp.attr_value.offset = param->write.offset;
                        rsp.attr_value.len = final_len;
                        rsp.attr_value.value = _char_b002_buffer + param->write.offset;

                        bk_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, BK_GATT_OK, &rsp);
                    }
                    f618_receive_write_data(param->write.handle,param->write.value,param->write.len);
                }
                else if(idx == F618_HDL_IDX_B001)
                {
                    final_len = MIN_VALUE(param->write.len, sizeof(_char_b001_buffer) - param->write.offset);
                    os_memcpy(_char_b001_buffer + param->write.offset, param->write.value, final_len);
                    gatt_logi("b001 recv %d",param->write.len);
                    if (param->write.need_rsp)
                    {
                        rsp.attr_value.auth_req = BK_GATT_AUTH_REQ_NONE;
                        rsp.attr_value.handle = param->write.handle;
                        rsp.attr_value.offset = param->write.offset;
                        rsp.attr_value.len = final_len;
                        rsp.attr_value.value = _char_b001_buffer + param->write.offset;

                        bk_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, BK_GATT_OK, &rsp);
                    }
                }
                else if(idx == F618_HDL_IDX_B001_DESC)
                {
                    uint16_t config = (((uint16_t)(param->write.value[1])) << 8) | param->write.value[0];
                    if(config & 1)
                    {
                        gatt_logi("client notify open");
                    }
                    else if(config & 2)
                    {
                        gatt_logi("client indicate open");
                    }
                    else if(config == 0)
                    {
                        gatt_logi("client config close");
                    }
                    if (param->write.need_rsp)
                    {
                        final_len = MIN_VALUE(param->write.len, sizeof(_char_b001_desc_buffer) - param->write.offset);
                        os_memcpy(_char_b001_desc_buffer + param->write.offset, param->write.value, final_len);

                        rsp.attr_value.auth_req = BK_GATT_AUTH_REQ_NONE;
                        rsp.attr_value.handle = param->write.handle;
                        rsp.attr_value.offset = param->write.offset;
                        rsp.attr_value.len = final_len;
                        rsp.attr_value.value = _char_b001_desc_buffer + param->write.offset;

                        bk_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, BK_GATT_OK, &rsp);
                    }
                }
            }
        }break;

        case BK_GATTS_READ_EVT:
        {
            int idx = _match_ble_handle(param->read.handle);
            if(idx != -1)
            {
                gatt_logi("BK_GATTS_READ_EVT idx %d",idx);
                gatt_logi("conn_id %d,trans %d,hdl %d,offset %d,need rsp %d,is_long %d\r\n",
                    param->read.conn_id,param->read.trans_id,param->read.handle,param->read.offset,
                    param->read.need_rsp,param->read.is_long);
                if (param->read.need_rsp)
                {
                    bk_gatt_rsp_t rsp;
                    uint16_t final_len  = 0;
                    uint8_t *buffer =  NULL;
                    os_memset(&rsp, 0, sizeof(rsp));
                    if(idx == F618_HDL_IDX_B002)
                    {
                        buffer = _char_b002_buffer + param->read.offset;
                        final_len = sizeof(_char_b002_buffer) - param->read.offset;
                    }
                    else if(idx == F618_HDL_IDX_B001)
                    {
                        buffer = _char_b001_buffer + param->read.offset;
                        final_len = sizeof(_char_b001_buffer) - param->read.offset;
                    }
                    else if(idx == F618_HDL_IDX_B001_DESC)
                    {
                        buffer = _char_b001_desc_buffer + param->read.offset;
                        final_len = sizeof(_char_b001_desc_buffer) - param->read.offset;
                    }
                    if(buffer !=  NULL)
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
        }break;
        case BK_GATTS_EXEC_WRITE_EVT:
        {
            gatt_logi("BK_GATTS_EXEC_WRITE_EVT");
        } break;

        case BK_GATTS_CONF_EVT:
        {
            if(param->conf.status==0 && param->conf.handle==_server_f618_attr_handle_list[F618_HDL_IDX_B001])
            {
                gatt_logi("handle=%x",param->conf.handle);
                if (f618_sema != NULL)
                {
                    rtos_set_semaphore( &f618_sema );
                }
            }
            else if(param->conf.status!=0)
            {
                gatt_loge("indicate confirm status error %x", param->conf.status);
            }
        } break;

        default:
            break;
    }
    return BK_ERR_BT_SUCCESS;
}

#if CLI_TEST
/* ---------------- CLI (for test) ---------------- */
static void _cmd_f618(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
    uint8_t buffer[256]={0};
    uint8_t dat,len;
    uint8_t ret1,ret2;
    
    os_printf("%s \r\n", __func__);
    for (uint8_t i = 0; i < argc; i++)
    {
        os_printf("argv[%d] %s\r\n", i, argv[i]);
    }

    if (argc >= 2 && memcmp(argv[1], "data_send", 9) == 0)
    {
        ret1 = sscanf(argv[2], "%x", &dat);
        ret2 = sscanf(argv[3], "%x", &len);

        if (ret1 != 1 || ret2 != 1)
        {
            gatt_logi("%s service param err %d,%d\n", __func__, ret1,ret2);
            return;
        }
        memset(buffer,dat,len);
        f618_ntf_data( buffer, len);

    }
    else
    {
        os_printf("unsupport cmd \r\n");
    }
}

static const struct cli_command s_ble_f618_commands[] =
{
    {"f618", "f618", _cmd_f618},
};
static int _cli_ble_f618_init(void)
{
    return cli_register_commands(s_ble_f618_commands, sizeof(s_ble_f618_commands) / sizeof(s_ble_f618_commands[0]));
}
#endif

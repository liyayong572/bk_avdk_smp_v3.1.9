#include <common/sys_config.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <components/log.h>
#include <os/mem.h>
#include <os/os.h>
#include "components/bluetooth/bk_ble_types.h"
#include "components/bluetooth/bk_ble.h"
#include "gatts.h"
#include "fa00_server.h"
#include "bk_cli.h"


#define CLI_TEST                    1
#define CHAR_BUFFER_SIZE            128
#define SYNC_CMD_TIMEOUT_MS         4000

#define FA00_SERVER_UUID            0xFA00
#define FA00_CHAR_EA01_UUID         0xEA01
#define FA00_CHAR_EA02_UUID         0xEA02
#define FA00_CHAR_EA05_UUID         0xEA05
#define FA00_CHAR_EA06_UUID         0xEA06

#define DECL_PRIMARY_SERVICE_16     {0x00, 0x28}
#define DECL_CHARACTERISTIC_16      {0x03, 0x28}
#define DESC_CLIENT_CHAR_CFG_16     {0x02, 0x29}

struct fa00_env_tag
{
    uint16_t start_hdl;
    uint8_t ea01_ntf_cfg[2];
};

static struct fa00_env_tag *fa00_env = NULL;
static beken_semaphore_t fa00_sema = NULL;

static uint8_t ea02_buf[CHAR_BUFFER_SIZE];
static uint8_t ea05_buf[CHAR_BUFFER_SIZE];
static uint8_t ea06_buf[CHAR_BUFFER_SIZE];
static int fa00_cli_init(void);

ble_attm_desc_t fa00_att_db[FA00_IDX_NB] =
{
    /* Service */
    [FA00_IDX_SVC] =
    {
        { FA00_SERVER_UUID & 0xFF, FA00_SERVER_UUID >> 8 },
        BK_BLE_PERM_SET(RD, ENABLE),
        0, 0
    },

    /* EA01 Notify */
    [FA00_IDX_EA01_CHAR] =
    {
        DECL_CHARACTERISTIC_16,
        BK_BLE_PERM_SET(RD, ENABLE),
        0, 0
    },
    [FA00_IDX_EA01_VAL] =
    {
        { FA00_CHAR_EA01_UUID & 0xFF, FA00_CHAR_EA01_UUID >> 8 },
        BK_BLE_PERM_SET(NTF, ENABLE),
        BK_BLE_PERM_SET(RI, ENABLE),
        0
    },
    [FA00_IDX_EA01_NTF_CFG] =
    {
        DESC_CLIENT_CHAR_CFG_16,
        BK_BLE_PERM_SET(RD, ENABLE) | BK_BLE_PERM_SET(WRITE_REQ, ENABLE),
        0, 2
    },

    /* EA02 Write */
    [FA00_IDX_EA02_CHAR] =
    {
        DECL_CHARACTERISTIC_16,
        BK_BLE_PERM_SET(RD, ENABLE),
        0, 0
    },
    [FA00_IDX_EA02_VAL] =
    {
        { FA00_CHAR_EA02_UUID & 0xFF, FA00_CHAR_EA02_UUID >> 8 },
        BK_BLE_PERM_SET(WRITE_REQ, ENABLE),
        BK_BLE_PERM_SET(RI, ENABLE),
        CHAR_BUFFER_SIZE
    },

    /* EA05 Read / Write */
    [FA00_IDX_EA05_CHAR] =
    {
        DECL_CHARACTERISTIC_16,
        BK_BLE_PERM_SET(RD, ENABLE),
        0, 0
    },
    [FA00_IDX_EA05_VAL] =
    {
        { FA00_CHAR_EA05_UUID & 0xFF, FA00_CHAR_EA05_UUID >> 8 },
        BK_BLE_PERM_SET(RD, ENABLE) | BK_BLE_PERM_SET(WRITE_REQ, ENABLE),
        BK_BLE_PERM_SET(RI, ENABLE),
        CHAR_BUFFER_SIZE
    },

    /* EA06 Read / Write */
    [FA00_IDX_EA06_CHAR] =
    {
        DECL_CHARACTERISTIC_16,
        BK_BLE_PERM_SET(RD, ENABLE),
        0, 0
    },
    [FA00_IDX_EA06_VAL] =
    {
        { FA00_CHAR_EA06_UUID & 0xFF, FA00_CHAR_EA06_UUID >> 8 },
        BK_BLE_PERM_SET(RD, ENABLE) | BK_BLE_PERM_SET(WRITE_REQ, ENABLE),
        BK_BLE_PERM_SET(RI, ENABLE),
        CHAR_BUFFER_SIZE
    },
};


static uint16_t _co_read16p(void const *ptr16)
{
    return ((uint8_t *)ptr16)[0] | (((uint8_t *)ptr16)[1] << 8);
}
void fa00_gatts_cb(ble_notice_t notice, void *param)
{
    switch (notice)
    {
        case BLE_5_WRITE_EVENT:
        {
            ble_write_req_t *w = (ble_write_req_t *)param;
            uint16_t ntf_cfg;
            
            if(w->prf_id != PRF_TASK_ID_FA00)
                 return;

            switch (w->att_idx)
            {
                case FA00_IDX_EA01_NTF_CFG:
                    ntf_cfg = _co_read16p(w->value);
                    fa00_env->ea01_ntf_cfg[w->conn_idx] = (ntf_cfg == PRF_CLI_START_NTF);
                    break;

                case FA00_IDX_EA02_VAL:
                    memcpy(ea02_buf, w->value, MIN(w->len, CHAR_BUFFER_SIZE));
                    break;

                case FA00_IDX_EA05_VAL:
                    memcpy(ea05_buf, w->value, MIN(w->len, CHAR_BUFFER_SIZE));
                    break;

                case FA00_IDX_EA06_VAL:
                    memcpy(ea06_buf, w->value, MIN(w->len, CHAR_BUFFER_SIZE));
                    break;

                default:
                    break;
            }
        } break;

        case BLE_5_READ_EVENT:
        {
            ble_read_req_t *r_req = (ble_read_req_t *)param;
            os_printf("read_cb:conn_idx:%d, prf_id:%d, att_idx:%d\r\n",r_req->conn_idx, r_req->prf_id, r_req->att_idx);
            if(r_req->prf_id != PRF_TASK_ID_FA00)
                 return;

            if (r_req->att_idx==FA00_IDX_EA01_NTF_CFG)
            {
                bk_ble_read_response_value(r_req->conn_idx, 2, fa00_env->ea01_ntf_cfg, r_req->prf_id, r_req->att_idx);
            }

        }break;
        case BLE_5_CREATE_DB:
        {
            ble_create_db_t *cd = (ble_create_db_t *)param;
            if (cd->prf_id != PRF_TASK_ID_FA00)
                return;
            fa00_env->start_hdl = cd->start_hdl;
            rtos_set_semaphore(&fa00_sema);
        } break;

        case BLE_5_TX_DONE:
        {
            bk_ble_gatt_cmp_evt_t *event = (bk_ble_gatt_cmp_evt_t *)param;
            
            if (event->prf_id != PRF_TASK_ID_FA00)
                return;
            rtos_set_semaphore(&fa00_sema);
        }break;
        default:
            break;
    }
}


bk_err_t fa00_init(void)
{
    struct bk_ble_db_cfg db_cfg;
    bk_err_t ret;
    os_printf("%s\n",__func__);
    rtos_init_semaphore(&fa00_sema, 1);

    fa00_env = os_malloc(sizeof(struct fa00_env_tag));
    if (!fa00_env)
        return BK_FAIL;
    memset(fa00_env, 0, sizeof(*fa00_env));

    memset(&db_cfg, 0, sizeof(db_cfg));
    db_cfg.att_db = fa00_att_db;
    db_cfg.att_db_nb = FA00_IDX_NB;
    db_cfg.prf_task_id = PRF_TASK_ID_FA00;
    db_cfg.start_hdl = 0;
    db_cfg.uuid[0] = FA00_SERVER_UUID & 0xFF;
    db_cfg.uuid[1] = FA00_SERVER_UUID >> 8;

    ret = bk_ble_create_db(&db_cfg);
    if (ret != BK_ERR_BLE_SUCCESS)
        return BK_FAIL;

    if (rtos_get_semaphore(&fa00_sema, SYNC_CMD_TIMEOUT_MS) != kNoErr)
        return BK_FAIL;
#if CLI_TEST
    fa00_cli_init();
#endif
    return BK_OK;
}


int fa00_notify_ea01(uint16_t conn_idx, uint8_t *data, uint16_t len)
{

    if (!fa00_env || !fa00_env->ea01_ntf_cfg[conn_idx])
        return -1;


    bk_ble_send_noti_value(conn_idx, len, data,PRF_TASK_ID_FA00, FA00_IDX_EA01_VAL);

    rtos_get_semaphore(&fa00_sema, SYNC_CMD_TIMEOUT_MS);
    return 0;
}

#if CLI_TEST
/* ---------------- CLI (for test) ---------------- */
static void _cmd_fa00(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
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
        fa00_notify_ea01(0, buffer, len);

    }
    else
    {
        os_printf("unsupport cmd \r\n");
    }
}

static const struct cli_command s_ble_fa00_commands[] =
{
    {"fa00", "fa00", _cmd_fa00},
};
static int fa00_cli_init(void)
{
    return cli_register_commands(s_ble_fa00_commands, sizeof(s_ble_fa00_commands) / sizeof(s_ble_fa00_commands[0]));
}
#endif



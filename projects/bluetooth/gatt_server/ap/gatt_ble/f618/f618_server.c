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
#include "f618_server.h"
#include "bk_cli.h"

#define CLI_TEST                    1
#define CHAR_BUFFER_SIZE            512
#define SYNC_CMD_TIMEOUT_MS         4000
#define F618_SERVER_UUID            0xF618
#define F618_CHAR_B002_UUID         0xB002
#define F618_CHAR_B001_UUID         0xB001

#define DECL_PRIMARY_SERVICE_16     {0x00, 0x28}
#define DECL_CHARACTERISTIC_16      {0x03, 0x28}
#define DESC_CLIENT_CHAR_CFG_16     {0x02, 0x29}


struct f618_env_tag
{
    uint16_t start_hdl;
    uint8_t b001_ntf_cfg[2];
};

static struct f618_env_tag *f618_env = NULL;
static beken_semaphore_t f618_sema = NULL;

static uint8_t b002_buf[CHAR_BUFFER_SIZE];
//static uint8_t b001_buf[CHAR_BUFFER_SIZE];
static int f618_cli_init(void);


ble_attm_desc_t f618_att_db[F618_IDX_NB] =
{
    /* Service */
    [F618_IDX_SVC] =
    {
        { F618_SERVER_UUID & 0xFF, F618_SERVER_UUID >> 8 },
        BK_BLE_PERM_SET(RD, ENABLE),
        0, 0
    },

    /* ---------- B002 : Write No Response ---------- */
    [F618_IDX_B002_CHAR] =
    {
        DECL_CHARACTERISTIC_16,
        BK_BLE_PERM_SET(RD, ENABLE),
        0, 0
    },
    [F618_IDX_B002_VAL] =
    {
        { F618_CHAR_B002_UUID & 0xFF, F618_CHAR_B002_UUID >> 8 },
        BK_BLE_PERM_SET(WRITE_COMMAND, ENABLE),
        BK_BLE_PERM_SET(RI, ENABLE),
        CHAR_BUFFER_SIZE
    },

    /* ---------- B001 : Notify ---------- */
    [F618_IDX_B001_CHAR] =
    {
        DECL_CHARACTERISTIC_16,
        BK_BLE_PERM_SET(RD, ENABLE),
        0, 0
    },
    [F618_IDX_B001_VAL] =
    {
        { F618_CHAR_B001_UUID & 0xFF, F618_CHAR_B001_UUID >> 8 },
        BK_BLE_PERM_SET(NTF, ENABLE),
        BK_BLE_PERM_SET(RI, ENABLE),
        0
    },
    [F618_IDX_B001_NTF_CFG] =
    {
        DESC_CLIENT_CHAR_CFG_16,
        BK_BLE_PERM_SET(RD, ENABLE) | BK_BLE_PERM_SET(WRITE_REQ, ENABLE),
        0, 2
    },
};

static uint16_t _co_read16p(void const *ptr16)
{
    return ((uint8_t *)ptr16)[0] | (((uint8_t *)ptr16)[1] << 8);
}

void f618_gatts_cb(ble_notice_t notice, void *param)
{
    switch (notice)
    {
        case BLE_5_WRITE_EVENT:
        {
            ble_write_req_t *w = (ble_write_req_t *)param;
            uint16_t ntf_cfg;

            if (w->prf_id != PRF_TASK_ID_F618)
                return;

            switch (w->att_idx)
            {
                case F618_IDX_B001_NTF_CFG:
                    ntf_cfg = _co_read16p(w->value);
                    f618_env->b001_ntf_cfg[w->conn_idx] = (ntf_cfg == PRF_CLI_START_NTF);
                    break;

                case F618_IDX_B002_VAL:
                    memcpy(b002_buf, w->value,MIN(w->len, CHAR_BUFFER_SIZE));
                    break;

                default:
                    break;
            }
        } break;

        case BLE_5_READ_EVENT:
        {
            ble_read_req_t *r = (ble_read_req_t *)param;

            if (r->prf_id != PRF_TASK_ID_F618)
                return;
            os_printf("read_cb:conn_idx:%d, prf_id:%d, att_idx:%d\r\n",r->conn_idx, r->prf_id, r->att_idx);

            if (r->att_idx == F618_IDX_B001_NTF_CFG)
            {
                bk_ble_read_response_value(r->conn_idx,2,&f618_env->b001_ntf_cfg[r->conn_idx], r->prf_id,r->att_idx);
            }
        } break;

        case BLE_5_CREATE_DB:
        {
            ble_create_db_t *cd = (ble_create_db_t *)param;
            if (cd->prf_id != PRF_TASK_ID_F618)
                return;
            f618_env->start_hdl = cd->start_hdl;
            rtos_set_semaphore(&f618_sema);
        } break;

        case BLE_5_TX_DONE:
        {
            bk_ble_gatt_cmp_evt_t *event = (bk_ble_gatt_cmp_evt_t *)param;
            
            if (event->prf_id != PRF_TASK_ID_F618)
                return;
            
            os_printf("TX_DONE:status:%d, att_id:%d\r\n",event->status, event->att_id);
            rtos_set_semaphore(&f618_sema);
        }break;
        default:
            break;
    }
}

bk_err_t f618_init(void)
{
    struct bk_ble_db_cfg db_cfg;
    bk_err_t ret;
    os_printf("%s\n",__func__);
    rtos_init_semaphore(&f618_sema, 1);

    f618_env = os_malloc(sizeof(struct f618_env_tag));
    if (!f618_env)
        return BK_FAIL;

    memset(f618_env, 0, sizeof(*f618_env));
    memset(&db_cfg, 0, sizeof(db_cfg));

    db_cfg.att_db = f618_att_db;
    db_cfg.att_db_nb = F618_IDX_NB;
    db_cfg.prf_task_id = PRF_TASK_ID_F618;
    db_cfg.start_hdl = 0;
    db_cfg.uuid[0] = F618_SERVER_UUID & 0xFF;
    db_cfg.uuid[1] = F618_SERVER_UUID >> 8;

    ret = bk_ble_create_db(&db_cfg);
    if (ret != BK_ERR_BLE_SUCCESS)
        return BK_FAIL;

    if (rtos_get_semaphore(&f618_sema, SYNC_CMD_TIMEOUT_MS) != kNoErr)
        return BK_FAIL;
    
#if CLI_TEST
    f618_cli_init();
#endif
    return BK_OK;
}


int f618_notify_b001(uint16_t conn_idx, uint8_t *data, uint16_t len)
{
    if (!f618_env || !f618_env->b001_ntf_cfg[conn_idx])
        return -1;

    bk_ble_send_noti_value(conn_idx,len,data,PRF_TASK_ID_F618,F618_IDX_B001_VAL);

    rtos_get_semaphore(&f618_sema, SYNC_CMD_TIMEOUT_MS);
    return 0;
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
        f618_notify_b001( 0,buffer, len);

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
static int f618_cli_init(void)
{
    return cli_register_commands(s_ble_f618_commands, sizeof(s_ble_f618_commands) / sizeof(s_ble_f618_commands[0]));
}
#endif


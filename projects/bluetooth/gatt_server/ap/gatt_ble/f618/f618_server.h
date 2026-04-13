#ifndef __F618_SERVER_H__
#define __F618_SERVER_H__
#include "components/bluetooth/bk_ble_types.h"
#include "components/bluetooth/bk_ble.h"



enum
{
    F618_IDX_SVC = 0,

    /* B002 : Write No Response */
    F618_IDX_B002_CHAR,
    F618_IDX_B002_VAL,

    /* B001 : Notify */
    F618_IDX_B001_CHAR,
    F618_IDX_B001_VAL,
    F618_IDX_B001_NTF_CFG,

    F618_IDX_NB,
};
bk_err_t f618_init(void);
void f618_gatts_cb(ble_notice_t notice, void *param);
int f618_notify_b001(uint16_t conn_idx, uint8_t *data, uint16_t len);


#endif


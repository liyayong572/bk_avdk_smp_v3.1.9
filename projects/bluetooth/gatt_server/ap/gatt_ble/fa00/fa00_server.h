#ifndef __FA00_SERVER_H__
#define __FA00_SERVER_H__
#include "components/bluetooth/bk_ble_types.h"
#include "components/bluetooth/bk_ble.h"




enum
{
    FA00_IDX_SVC = 0,

    FA00_IDX_EA01_CHAR,
    FA00_IDX_EA01_VAL,
    FA00_IDX_EA01_NTF_CFG,

    FA00_IDX_EA02_CHAR,
    FA00_IDX_EA02_VAL,

    FA00_IDX_EA05_CHAR,
    FA00_IDX_EA05_VAL,

    FA00_IDX_EA06_CHAR,
    FA00_IDX_EA06_VAL,

    FA00_IDX_NB,
};
bk_err_t fa00_init(void);
void fa00_gatts_cb(ble_notice_t notice, void *param);

int fa00_notify_ea01(uint16_t conn_idx, uint8_t *data, uint16_t len);

#endif


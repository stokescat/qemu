#ifndef HW_MISC_EFIZZER_H
#define HW_MISC_EFIZZER_H


/* for PCIDevice */
#include "hw/sysbus.h"
/* for QIOChannelSocket */
#include "io/channel-socket.h"

#define TYPE_EFIZZER "efizzer"
#define TYPE_EFIZZER_PROP_SOCKET "socket"
OBJECT_DECLARE_SIMPLE_TYPE(EfizzerState, EFIZZER)

#define MMEP_MMIO_SIZE    (0x1000)
#define MMEP_REG_HIT      (0x0000)
#define MMEP_REG_MODGUID  (0x0008)
#define MMEP_REG_MODSIZE  (0x0018)
#define MMEP_REG_MODADDR  (0x0020)
#define MMEP_REG_CMD      (0x0038)
#define MMEP_REG_BUF      (0x0040)

#define MMEP_HIT_EVENT_MSGLEN  16
#define MMEP_MOD_EVENT_MSGLEN  40
#define MMEP_RDY_EVENT_MSGLEN  8
#define MMEP_RUN_EVENT_MSGLEN  8
#define MMEP_ERR_EVENT_MSGLEN  8
#define MMEP_FIN_EVENT_MSGLEN  8

typedef struct EfizzerState {
    SysBusDevice      pdev;

    MemoryRegion      mmio;

    uint64_t          regw_hit;
    uint8_t           regw_modguid[16];
    uint64_t          regw_modsize;
    uint64_t          regw_modaddr;

    uint64_t          regw_cmd;
    uint64_t          regr_cmd;
    uint64_t          regr_buf[504];

    char              *sock_addr;
    QIOChannelSocket  *sock_io;

    uint8_t           hitEventBuf[MMEP_HIT_EVENT_MSGLEN];
    uint8_t           modEventBuf[MMEP_MOD_EVENT_MSGLEN];

} EfizzerState;

#endif

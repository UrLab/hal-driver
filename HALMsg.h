#ifndef DEFINE_HALMSG_HEADER
#define DEFINE_HALMSG_HEADER

#include "HALResource.h"
#include <stdio.h>

typedef enum {
    BOOT='#',
    HAL_PING='*',
    VERSION='?',
    TREE='$',

    TRIGGER='T',
    SWITCH='S',
    SENSOR='C',
    ANIMATION_FRAMES='F',
    ANIMATION_DELAY='D',
    ANIMATION_LOOP='L',
    ANIMATION_PLAY='P',

    PARAM_CHANGE=0x80,
    PARAM_ASK=0x00
} HALCommand;

typedef struct HALMsg_t {
    unsigned char cmd; //!< Hal command
    unsigned char rid; //!< Resource id
    unsigned char len; //!< Data len
    unsigned char chk; //!< Checksum
    unsigned char data[255]; //!< Command data, if any
} HALMsg;


unsigned char HALMsg_checksum(HALMsg *msg);

/* Compute checksum and write message to HAL. */
void HALMsg_write(HAL_t *hal, HALMsg *msg);

/* Read a message from serial link */
void HALMsg_read(HAL_t *hal, HALMsg *res);

/* Wait for cmd, then read a message of this type from serial link. */
void HALMsg_read_command(HAL_t *hal, HALMsg *res, unsigned char cmd);

static inline void RESET(HALMsg *msg){memset(msg, 0, sizeof(HALMsg));}
static inline void DESCRIBE(HALMsg *msg)
{
    printf("%c %hhu [%hhu] <%hhu>", msg->cmd, msg->rid, msg->len, msg->chk);
}

static inline bool CMD(HALMsg *msg, HALCommand type)
{
    return (msg->cmd & ~PARAM_CHANGE) == type;
}

static inline bool IS_CHANGE(HALMsg *msg)
{
    return (msg->cmd & PARAM_CHANGE) == PARAM_CHANGE;
}

static inline bool IS_VALID(HALMsg *msg)
{
    return (msg)->chk == HALMsg_checksum(msg);
}

#endif

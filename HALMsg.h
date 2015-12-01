#ifndef DEFINE_HALMSG_HEADER
#define DEFINE_HALMSG_HEADER

#include <stdio.h>

typedef enum {
    BOOT='#',
    HAL_PING='*',
    VERSION='?',
    TREE='$',

    TRIGGER='T',
    SWITCH='S',
    SENSOR='C',
    DHTSENSOR='H',
    ANIMATION_FRAMES='F',
    ANIMATION_DELAY='D',
    ANIMATION_LOOP='L',
    ANIMATION_PLAY='P',
    RGB='R',

    PARAM_CHANGE=0x80,
    PARAM_ASK=0x00
} HALCommand;

typedef struct HALMsg_t {
    unsigned char chk;        //!< Checksum
    unsigned char seq;        //!< Sequence id of the message
    unsigned char cmd;        //!< Hal command
    unsigned char rid;        //!< Resource id
    unsigned char len;        //!< Data len
    unsigned char data[255];  //!< Command data, if any
} HALMsg;

#define HALMSG_SEQ_MAX 0x7f
#define ABSOLUTE_SEQ(x) ((x) & 0x7f)
#define DRIVER_SEQ(x) ABSOLUTE_SEQ(x)
#define ARDUINO_SEQ(x) (0x80 | ABSOLUTE_SEQ(x))
#define IS_DRIVER_SEQ(x) ((x & 0x80) == 0x00)
#define IS_ARDUINO_SEQ(x) ((x & 0x80) == 0x80)

#define MSG_TYPE(msg) (((msg)->cmd) & 0x7f)
#define MSG_IS_CHANGE(msg) (((msg)->cmd) & 0x80)

static inline unsigned char HALMsg_checksum(HALMsg *msg)
{
    unsigned char res = 0;

    /* Sum of bytes above chk */
    unsigned char *bytes = (unsigned char *) msg + 1;
    size_t nbytes = ((size_t) msg->len) + 4;
    for (size_t i=0; i<nbytes; i++){
        res += bytes[i];
    }

    return res;
}

#endif

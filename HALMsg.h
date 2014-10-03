#ifndef DEFINE_HALMSG_HEADER
#define DEFINE_HALMSG_HEADER

typedef enum {
    PING='*',
    VERSION='?',
    TREE='$',

    TRIGGER='T',
    SWITCH='S',
    SENSOR='C',
    ANIMATION_FRAMES='F',
    ANIMATION_DELAY='D',
    ANIMATION_LOOP='L',
    ANIMATION_PLAY='P',

    CHANGE=0x20,
    ASK=0
} HALCommand;

#define IS(msg, type) ((((msg).cmd) & (type)) == (type))
typedef struct HALMsg_t {
    unsigned char cmd; //!< Hal command
    unsigned char rid; //!< Resource id
    unsigned char len; //!< Data len
    unsigned char chk; //!< Checksum
    unsigned char data[255]; //!< Command data, if any
} HALMsg;

static inline unsigned char HALMsg_checksum(HALMsg *msg)
{
    unsigned char res = 0;
    res += msg->cmd;
    res += msg->rid;
    res += msg->len;
    for (unsigned char i=0; i<msg->len; i++)
        res += msg->data[i];
    return res;
}

#endif

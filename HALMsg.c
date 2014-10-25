#include <unistd.h>
#include <pthread.h>
#include <assert.h>
#include "HALMsg.h"
#include "utils.h"
#include "logger.h"

unsigned char HALMsg_checksum(HALMsg *msg)
{
    unsigned char res = 0;
    res += msg->cmd;
    res += msg->rid;
    res += msg->len;
    for (unsigned char i=0; i<msg->len; i++)
        res += msg->data[i];
    return res;
}

static inline void write_byte(int fd, unsigned char b)
{
    int r = 0;
    while (r != 1)
        r = write(fd, &b, 1);
}

void HALMsg_write(HAL_t *hal, HALMsg *msg)
{
    msg->chk = HALMsg_checksum(msg);

    write_byte(hal->serial_fd, msg->cmd);
    write_byte(hal->serial_fd, msg->rid);
    write_byte(hal->serial_fd, msg->len);
    write_byte(hal->serial_fd, msg->chk);
    for (unsigned char i=0; i<msg->len; i++)
        write_byte(hal->serial_fd, msg->data[i]);


    if (LOG_LEVEL >= 4){
        printf("\033[31m << "); DESCRIBE(msg); printf("\033[0m\n");
    }
}

static inline unsigned char read_byte(int fd)
{
    unsigned char b = 0;
    int r = 0;
    while (r != 1){
        r = read(fd, &b, 1);
        minisleep(0.001);
    }
    return b;
}

static inline void HALMsg_read_atoffset(HAL_t *hal, HALMsg *res, size_t offset)
{
    assert(offset < 4);
    unsigned char *buf = (unsigned char *) res;

    /* Read header */
    for (size_t i=offset; i<4; i++)
        buf[i] = read_byte(hal->serial_fd);

    for (unsigned char i=0; i<res->len; i++)
        res->data[i] = read_byte(hal->serial_fd);

    if (LOG_LEVEL >= 4){
        printf("\033[32m >> "); DESCRIBE(res); printf("\033[0m\n");
    }
}

void HALMsg_read(HAL_t *hal, HALMsg *res)
{
    memset(res, 0, sizeof(HALMsg));
    HALMsg_read_atoffset(hal, res, 0);
}

void HALMsg_read_command(HAL_t *hal, HALMsg *res, unsigned char cmd)
{
    memset(res, 0, sizeof(HALMsg));
    while (read_byte(hal->serial_fd) != cmd);
    HALMsg_read_atoffset(hal, res, 1);
}

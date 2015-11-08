#include "com.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <poll.h>
#include <termios.h>
#include <sys/fcntl.h>

static const unsigned char SYNC = 0xff;
static const unsigned char  ESC = 0xaa;

struct HALConnection {
    /* Arduino FD */
    int fd;

    /* Current emit seq number */
    unsigned int current_seq;

    /* Multithreading for the reader */
    pthread_mutex_t mutex;
    pthread_t reader_thread;

    /* A table containing pending requests, indexed by seq number */
    pthread_cond_t  waits[HALMSG_SEQ_MAX+1];
    unsigned char    used[HALMSG_SEQ_MAX+1];
    HALMsg      responses[HALMSG_SEQ_MAX+1];

    /* Stats */
    size_t rx_bytes;
    size_t tx_bytes;
    time_t start_time;
};

static int set_termios_opts(int fd)
{
    struct termios toptions;

    if (tcgetattr(fd, &toptions) < 0) {
        return 0;
    }

    speed_t brate = B115200; // let you override switch below if needed
    cfsetispeed(&toptions, brate);
    cfsetospeed(&toptions, brate);

    // 8N1
    toptions.c_cflag &= ~PARENB;
    toptions.c_cflag &= ~CSTOPB;
    toptions.c_cflag &= ~CSIZE;
    toptions.c_cflag |= CS8;
    
    toptions.c_cflag |= CREAD | CLOCAL;  // turn on READ & ignore ctrl lines
    toptions.c_iflag &= ~(IXON | IXOFF | IXANY); // turn off s/w flow ctrl

    toptions.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG); // make raw
    toptions.c_oflag &= ~OPOST; // make raw

    // see: http://unixwiz.net/techtips/termios-vmin-vtime.html
    toptions.c_cc[VMIN]  = 0;
    toptions.c_cc[VTIME] = 0;
    
    tcsetattr(fd, TCSANOW, &toptions);
    if(tcsetattr(fd, TCSAFLUSH, &toptions) < 0) {
        return 0;
    }

    return 1;
}

HALConnection *HALConn_open(const char *path)
{
    int fd = open(path, O_RDWR);
    
    if (fd < 0)  {
        HAL_ERROR(UNKNERR, "Unable to open port [ERRNO %d: %s]", errno, strerror(errno));
        return NULL;
    }
    if (! set_termios_opts(fd)){
        close(fd);
        HAL_ERROR(UNKNERR, "Unable to set serial port options [ERRNO %d: %s]\n", errno, strerror(errno));
        return NULL;
    }

    HALConnection *res = calloc(1, sizeof(HALConnection));
    res->fd = fd;
    pthread_mutex_init(&res->mutex, NULL);
    for (size_t i=0; i<HALMSG_SEQ_MAX+1; i++){
        pthread_cond_init(res->waits+i, NULL);
    }

    return res;
}

void HALConn_close(HALConnection *conn)
{
    close(conn->fd);
    free(conn);
}

/* Write a single byte, escaping ESC and SYNC */
static HALErr HAL_write_byte(HALConnection *conn, unsigned char the_byte)
{
    HALErr r;

    /* Escape special bytes */
    if (the_byte == SYNC || the_byte == ESC){
        r = write(conn->fd, &ESC, 1);
        if (r != 1){
            return WRITEERR;
        }
        conn->tx_bytes++;
    }

    /* Write the byte itself */
    r = write(conn->fd, &the_byte, 1);
    if (r != 1){
        return WRITEERR;
    }
    conn->tx_bytes++;
    return OK;
}

static const struct timespec read_sleep = {.tv_sec=0, .tv_nsec=10000};
static inline int wrap_read(int fd, unsigned char *dest)
{
    int r = read(fd, dest, 1);
    while (r == 0){
        nanosleep(&read_sleep, NULL);
        r = read(fd, dest, 1);
    }
    return r;
}

/* Read a (potentially escaped) single byte */
static HALErr HAL_read_byte(HALConnection *conn, unsigned char *the_byte)
{
    /* Read byte */
    int r = wrap_read(conn->fd, the_byte);
    if (r < 0){
        return READERR;
    }
    conn->rx_bytes++;

    if (*the_byte == SYNC){
        return OUTOFSYNC;
    }

    /* If it was escaped read 1 more byte */
    else if (*the_byte == ESC){
        r = wrap_read(conn->fd, the_byte);
        if (r < 0){
            HAL_WARN("Error when reading byte [ERRNO %d: %s]",
                errno, strerror(errno));
            return READERR;
        }
        conn->rx_bytes++;
    }

    return OK;
}

/* Write a full message */
HALErr HALConn_write_message(HALConnection *conn, const HALMsg *msg)
{
    const unsigned char *bytes = (const unsigned char *) msg;
    HALErr r = 0;

    /* Write 3 SYNCs bytes (prelude) */
    for (int i=0; i<3; i++){
        r = write(conn->fd, &SYNC, 1);
        if (r != 1){
            return WRITEERR;
        }
        conn->tx_bytes++;
    }

    /* Write message itself */
    for (int i=0; i<5+msg->len; i++){
        r = HAL_write_byte(conn, bytes[i]);
        if (r != OK){
            return r;
        }
    }

    dump_message(msg, " << ");

    return OK;
}

/* Read a full message */
HALErr HALConn_read_message(HALConnection *conn, HALMsg *msg)
{
    int sync_count = 0;
    unsigned char c;
    int r;

    /* 1. Find 3 consecutive SYNC */
    while (sync_count != 3){
        r = wrap_read(conn->fd, &c);
        if (r < 0){
            return READERR;
        }
        conn->rx_bytes++;

        if (c == SYNC){
            sync_count ++;
        } else {
            sync_count = 0;
        }
    }

    /* 2. Read message header */
    unsigned char *bytes = (unsigned char *)msg;
    for (int i=0; i<5; i++){
        r = HAL_read_byte(conn, bytes+i);
        if (r != OK){
            return r;
        }
    }

    /* 3. Read body */
    for (int i=0; i<msg->len; i++){
        r = HAL_read_byte(conn, msg->data+i);
        if (r != OK){
            return r;
        }
    }

    dump_message(msg, " >> ");

    /* 4. Verify checksum */
    return (HALMsg_checksum(msg) == msg->chk) ? OK : CHKERR;
}

HALErr HALConn_request(HALConnection *conn, HALMsg *msg)
{
    HALErr retval;

    /* Acquire lock on connection */
    int r = pthread_mutex_lock(&conn->mutex);
    if (r != 0){
        return LOCKERR;
    }

    /* Acquire next SEQ no */
    unsigned char seq = DRIVER_SEQ(conn->current_seq + 1);
    if (conn->used[ABSOLUTE_SEQ(seq)]){
        retval = SEQERR;
    }
    else {
        /* Attribute SEQ no and emit message */
        conn->used[ABSOLUTE_SEQ(seq)] = 1;
        msg->seq = conn->current_seq = seq;
        /* Compute and store checksum in msg */
        msg->chk = HALMsg_checksum(msg);

        r = HALConn_write_message(conn, msg);
        if (r != OK){
            retval = r;
        }
        else {
            /* Set timeout in 500ms */
            struct timespec timeout;
            clock_gettime(CLOCK_REALTIME, &timeout);
            unsigned long int nsecs = timeout.tv_nsec + 500000000;
            if (nsecs > 1000000000l){
                timeout.tv_sec++;
            }
            timeout.tv_nsec = nsecs % 1000000000l;

            /* Wait for response */
            r = pthread_cond_timedwait(conn->waits+seq, &conn->mutex, &timeout);
            if (r == ETIMEDOUT){
                retval = TIMEOUT;
            }
            else if (r != 0){
                retval = UNKNERR;
            }
            else {
                memcpy(msg, conn->responses+seq, sizeof(HALMsg));
                retval = OK;
            }
        }

        /* Mark as unused */
        conn->used[ABSOLUTE_SEQ(seq)] = 0;
    }

    /* Release lock; we're done */
    pthread_mutex_unlock(&conn->mutex);
    return retval;
}

static void HALConn_dispatch(HALConnection *conn, HALMsg *msg)
{
    if (IS_DRIVER_SEQ(msg->seq)){
        size_t i = ABSOLUTE_SEQ(msg->seq);
        memcpy(conn->responses+i, msg, sizeof(HALMsg));
        pthread_cond_signal(conn->waits+i);
    }
    else {
        if (MSG_TYPE(msg) == HAL_PING){
            HALConn_write_message(conn, msg);
        } else if (MSG_TYPE(msg) == BOOT){
            HAL_WARN("Arduino rebooted");
        }
    }
}

static void *HALConn_reader_thread(void *arg)
{
    HALConnection *conn = (HALConnection *) arg;
    int r = 0;
    HALMsg msg;
    struct pollfd polled = {.fd = conn->fd, .events = POLLIN};

    r = pthread_mutex_lock(&conn->mutex);
    if (r != 0){
        return NULL;
    }
    conn->start_time = time(NULL);
    HAL_INFO("Reader thread started");
    pthread_mutex_unlock(&conn->mutex);


    while (1){
        /* Wait for arduino readyness */
        do {r = poll(&polled, 1, 1000);}
        while (r != 1);

        /* Acquire lock and read message */
        r = pthread_mutex_lock(&conn->mutex);
        if (r == 0){
            r = HALConn_read_message(conn, &msg);
            if (r == OK){
                HALConn_dispatch(conn, &msg);
            } else {
                HAL_ERROR(r, "Error while acquiring message in reader thread");
            }
            pthread_mutex_unlock(&conn->mutex);
        }
    }

    return NULL;
}

int HALConn_run_reader(HALConnection *conn)
{
    return pthread_create(&conn->reader_thread, NULL, HALConn_reader_thread, conn);
}

size_t HALConn_rx_bytes(HALConnection *conn)
{
    size_t res = 0;
    pthread_mutex_lock(&conn->mutex);
    res = conn->rx_bytes;
    pthread_mutex_unlock(&conn->mutex);
    return res;
}

size_t HALConn_tx_bytes(HALConnection *conn)
{
    size_t res = 0;
    pthread_mutex_lock(&conn->mutex);
    res = conn->tx_bytes;
    pthread_mutex_unlock(&conn->mutex);
    return res;
}

int HALConn_uptime(HALConnection *conn)
{
    int res = 0;
    pthread_mutex_lock(&conn->mutex);
    res = time(NULL) - conn->start_time;
    pthread_mutex_unlock(&conn->mutex);
    return res;
}

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
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/stat.h>

static const unsigned char SYNC = 0xff;
static const unsigned char  ESC = 0xaa;

#ifndef HALCONN_SOCK_CLIENTS
#define HALCONN_SOCK_CLIENTS 42
#endif

struct HALConnection {
    /* Arduino FD */
    int fd;

    /* Current emit seq number */
    unsigned int current_seq;

    /* Multithreading for the reader */
    pthread_mutex_t mutex;
    pthread_t reader_thread;
    int running;

    /* A table containing pending requests, indexed by seq number */
    pthread_cond_t  waits[HALMSG_SEQ_MAX+1];
    unsigned char    used[HALMSG_SEQ_MAX+1];
    HALMsg      responses[HALMSG_SEQ_MAX+1];

    /* Event socket */
    int sock;
    int sock_clients[HALCONN_SOCK_CLIENTS];
    size_t n_sock_clients;
    const char *sock_path;

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

    speed_t brate = B115200;
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
    if (tcsetattr(fd, TCSAFLUSH, &toptions) < 0) {
        return 0;
    }

    return 1;
}

HALConnection *HALConn_open(const char *path, const char *sock_path)
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

    res->start_time = time(NULL);
    res->sock_path = strndup(sock_path, 256);

    struct sockaddr_un sock_desc;
    strcpy(sock_desc.sun_path, sock_path);

    size_t len = strlen(sock_desc.sun_path) + sizeof(sock_desc.sun_family);
    res->sock = socket(AF_UNIX, SOCK_STREAM, 0);
    sock_desc.sun_family = AF_UNIX;

    bind(res->sock, (struct sockaddr *)&sock_desc, len);
    listen(res->sock, HALCONN_SOCK_CLIENTS);
    chmod(sock_desc.sun_path, 0777);

    return res;
}

void HALConn_close(HALConnection *conn)
{
    if (HALConn_is_running(conn)){
        HALConn_stop_reader(conn);
    }
    for (size_t i=0; i<conn->n_sock_clients; i++){
        close(conn->sock_clients[i]);
    }
    unlink(conn->sock_path);
    free((void*) conn->sock_path);
    pthread_mutex_destroy(&conn->mutex);
    for (size_t i=0; i<HALMSG_SEQ_MAX; i++){
        pthread_cond_destroy(conn->waits+i);
    }
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
    size_t nbytes = ((size_t) msg->len) + 5;
    for (size_t i=0; i<nbytes; i++){
        r = HAL_write_byte(conn, bytes[i]);
        if (r != OK){
            return r;
        }
    }

    dump_message(msg, " \033[1;34m<<\033[0m ");

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

    dump_message(msg, " \033[1;35m>>\033[0m ");

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
                HAL_WARN("Timeout on message");
                dump_message(msg, "Timeouted request");
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

struct reader_opts {
    HALConnection *conn;
    const char **trigger_names;
    size_t n_triggers;
};

static void HALConn_trigger_socket(HALConnection *conn, const char *name, int state)
{
    int r;
    char buf[256];
    int len = snprintf(buf, sizeof(buf)-1, "%s:%d\n", name, state);

    for (size_t i=0; i<conn->n_sock_clients; i++){
        HAL_DEBUG("%d << %s", conn->sock_clients[i], buf);
        r = send(conn->sock_clients[i], buf, len, MSG_NOSIGNAL);
        if (r < 0){
            HAL_WARN("Lost listener %d", conn->sock_clients[i]);
            close(conn->sock_clients[i]);
            conn->n_sock_clients--;
            if (conn->n_sock_clients > 0){
                conn->sock_clients[i] = conn->sock_clients[conn->n_sock_clients];
            }
        }
    }
}

static void HALConn_dispatch(HALConnection *conn, HALMsg *msg, struct reader_opts *opts)
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
        } else if (msg->cmd == (TRIGGER|PARAM_CHANGE)){
            unsigned char trigger_id = msg->rid;
            unsigned char state = msg->data[0];
            if (trigger_id < opts->n_triggers){
                HALConn_trigger_socket(conn, opts->trigger_names[trigger_id], state);
            }
        }
    }
}

static void *HALConn_reader_thread(void *arg)
{
    struct reader_opts *opts = (struct reader_opts *) arg;
    HALConnection *conn = opts->conn;
    int r = 0;
    HALMsg msg;
    struct pollfd polled[2] = {
        {.fd = conn->fd, .events = POLLIN},
        {.fd = conn->sock, .events = POLLIN},
    };

    HAL_INFO("Reader thread started");

    while (HALConn_is_running(conn)){
        /* Wait for arduino readyness */
        do {r = poll(polled, 2, 1000);}
        while (r == 0);

        if (r < 0){
            HAL_ERROR(UNKNERR, "Poll error");
            continue;
        }

        /* Acquire lock and read message */
        r = pthread_mutex_lock(&conn->mutex);
        if (r == 0){
            if ((polled[0].revents) & POLLIN){
                r = HALConn_read_message(conn, &msg);
                if (r == OK){
                    HALConn_dispatch(conn, &msg, opts);
                } else {
                    HAL_ERROR(r, "Error while acquiring message in reader thread");
                }
                polled[0].revents = 0;
            }

            if ((polled[1].revents) & POLLIN){
                int fd = accept(conn->sock, NULL, NULL);
                conn->sock_clients[conn->n_sock_clients] = fd;
                conn->n_sock_clients++;
                HAL_INFO("New listener: %d", fd);
                polled[1].revents = 0;
            }

            pthread_mutex_unlock(&conn->mutex);
        }
    }

    HAL_INFO("Reader thread terminated");

    return NULL;
}

int HALConn_run_reader(HALConnection *conn, const char **trigger_names, size_t n_triggers)
{
    struct reader_opts *opts = calloc(1, sizeof(struct reader_opts));
    opts->conn = conn;
    opts->trigger_names = trigger_names;
    opts->n_triggers = n_triggers;
    conn->running = 1;
    return pthread_create(&conn->reader_thread, NULL, HALConn_reader_thread, opts);
}

void HALConn_stop_reader(HALConnection *conn)
{
    pthread_mutex_lock(&conn->mutex);
    conn->running = 0;
    pthread_mutex_unlock(&conn->mutex);

    void *retval;
    pthread_join(conn->reader_thread, &retval);
}

int HALConn_is_running(HALConnection *conn)
{
    int r = 0;
    pthread_mutex_lock(&conn->mutex);
    r = conn->running;
    pthread_mutex_unlock(&conn->mutex);
    return r;
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

const char *HALConn_sock_path(HALConnection *conn)
{
    return conn->sock_path;
}

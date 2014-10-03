#include "arduino-serial-lib.h"
#include "HALResource.h"
#include "HALMsg.h"
#include "utils.h"
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <errno.h>

#define HAL_LOCK(r) pthread_mutex_lock(&((r)->hal->mutex))
#define HAL_UNLOCK(r) pthread_mutex_unlock(&((r)->hal->mutex))

#define HAL_SIGNAL(r) pthread_cond_signal(&(r)->cond)
#define HAL_WAIT(r) pthread_cond_timedwait(&((r)->cond), &((r)->hal->mutex), &HAL_req_timeout)

static const struct timespec HAL_req_timeout = {.tv_sec=0, .tv_nsec=500000000};

static void HAL_send_msg(struct HAL_t *hal, HALMsg *msg);
static bool HAL_read_msg(struct HAL_t *hal, HALMsg *res);
static bool HAL_sync_read(
    struct HAL_t *hal,
    HALCommand cmd,
    HALMsg *res,
    size_t retry
);


/* Establish communication with arduino && initialize HAL structure */
bool HAL_init(struct HAL_t *hal, const char *arduino_dev)
{
    memset(hal, 0, sizeof(struct HAL_t));
    hal->serial_fd = serialport_init(arduino_dev, 115200);
    if (hal->serial_fd < 0)
        return false;

    sleep(1);

    HALMsg msg = {.cmd=VERSION};
    HAL_send_msg(hal, &msg);
    if (! HAL_sync_read(hal, VERSION, &msg, 512))
        goto fail;

    /* Get HAL structure */
    msg.cmd = TREE;
    HAL_send_msg(hal, &msg);
    if (! HAL_sync_read(hal, VERSION, &msg, 512))
        goto fail;
    
    /* For the 4 resources type */
    for (int i=0; i<4; i++){
        if (! HAL_sync_read(hal, TREE, &msg, 512))
            goto fail;

        /* Allocate N items */
        unsigned char N = msg.rid;
        HALCommand res_type = msg.data[0];
        HALResource *res = calloc(N, sizeof(HALResource));
        if (! res)
            goto fail;

        /* Assign in hal */
        if (res_type == ANIMATION_FRAMES){
            hal->animations = res;
            hal->n_animations = N;
        }
        else if (res_type == SENSOR){
            hal->sensors = res;
            hal->n_sensors = N;
        }
        else if (res_type == TRIGGER){
            hal->triggers = res;
            hal->n_triggers = N;
        }
        else{
            hal->switchs = res;
            hal->n_switchs = N;
        }


        /* Get list of items */
        for (unsigned char j=0; j<N; j++){
            if (! HAL_read_msg(hal, &msg))
                goto fail;
            msg.data[msg.len] = '\0';
            HALResource_init(res+j, (const char *) msg.data+1, res_type, msg.rid, hal);
        }
    }

    pthread_mutex_init(&hal->mutex, NULL);

    hal->ready = true;
    return true;

    fail:
        if (hal->n_triggers) HALResource_destroyAll(&hal->n_triggers, hal->triggers);
        if (hal->n_sensors) HALResource_destroyAll(&hal->n_sensors, hal->sensors);
        if (hal->n_switchs) HALResource_destroyAll(&hal->n_switchs, hal->switchs);
        if (hal->n_animations) HALResource_destroyAll(&hal->n_animations, hal->animations);
        serialport_close(hal->serial_fd);
        memset(hal, 0, sizeof(struct HAL_t));
        return false;
}

/* Create && bind HAL events socket */
void HAL_socket_open(struct HAL_t *hal, const char *path)
{
    size_t len;
    struct sockaddr_un sock_desc;
    hal->socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);

    sock_desc.sun_family = AF_UNIX;
    strcpy(sock_desc.sun_path, path);
    unlink(sock_desc.sun_path);
    
    len = strlen(sock_desc.sun_path) + sizeof(sock_desc.sun_family);
    bind(hal->socket_fd, (struct sockaddr *)&sock_desc, len);
    listen(hal->socket_fd, HAL_SOCK_MAXCLIENTS);
    chmod(sock_desc.sun_path, 0777);
}

/* Writes msg to every clients of HAL events socket */
static void HAL_socket_write(struct HAL_t *hal, const char *msg)
{
    int len = (int) strlen(msg);
    for (int i=0; i<hal->socket_n_clients; i++){
        int client = hal->socket_clients[i];
        if (write(client, msg, len) != len){
            close(client);
            hal->socket_clients[i] = hal->socket_clients[hal->socket_n_clients-1];
            hal->socket_n_clients--;
        }
    }
}

/* Accept new client on HAL events socket; timeouts in 1ms */
static void HAL_socket_accept(struct HAL_t *hal)
{
    fd_set set;
    struct timeval timeout = {.tv_sec = 0, .tv_usec = 1000};
    FD_SET(hal->socket_fd, &set);
    select(hal->socket_fd+1, &set, NULL, NULL, &timeout);
    if (! FD_ISSET(hal->socket_fd, &set))
        return;

    int client = accept(hal->socket_fd, NULL, NULL);
    hal->socket_clients[hal->socket_n_clients] = client;
    hal->socket_n_clients++;
}

static inline bool HAL_readbyte_timeout(
    struct HAL_t *hal, 
    unsigned char *dest, 
    double timeout_s, 
    int n_retry
){
    int r = 0;
    for (int j=0; j<n_retry; j++){
        r = read(hal->serial_fd, dest, 1);
        if (r == 1) break;
        else minisleep(timeout_s);
    }
    return (r == 1);
}

static inline bool HAL_reset_msg(HALMsg *msg)
{
    memset(msg, 0, sizeof(HALMsg));
    return false;
}

static bool HAL_sync_read(
    struct HAL_t *hal,
    HALCommand cmd,
    HALMsg *res,
    size_t retry
){
    size_t i;
    unsigned char b = 0;

    for (i=0; i<retry && b != cmd; i++)
        HAL_readbyte_timeout(hal, &b, 0.0001, 5);
    if (b != cmd) 
        return HAL_reset_msg(res);

    unsigned char *buf = (unsigned char *) res;
    for (i=1; i<3; i++)
        if (! HAL_readbyte_timeout(hal, buf+i, 0.0001, 5))
            return HAL_reset_msg(res);

    for (i=0; i<res->len; i++)
        if (! HAL_readbyte_timeout(hal, buf+i, 0.0001, 5))
            return HAL_reset_msg(res);

    return HALMsg_checksum(res) == res->chk;
}

static bool HAL_read_msg(struct HAL_t *hal, HALMsg *res)
{
    unsigned char i;
    unsigned char *buf = (unsigned char *) res;

    for (i=0; i<4; i++)
        if (! HAL_readbyte_timeout(hal, buf+i, 0.0001, 5))
            return HAL_reset_msg(res);

    for (i=0; i<res->len; i++)
        if (! HAL_readbyte_timeout(hal, buf+i, 0.0001, 5))
            return HAL_reset_msg(res);

    return HALMsg_checksum(res) == res->chk;
}

static void HAL_send_msg(struct HAL_t *hal, HALMsg *msg)
{
    const unsigned char *buf = (const unsigned char *) msg;
    size_t len = msg->len + 4;

    msg->chk = HALMsg_checksum(msg);
    for (size_t i=0; i<len; i++){
        for (int j=0; j<5; j++){
            if (serialport_writebyte(hal->serial_fd, buf[i]) != 0)
                minisleep(0.001);
            else
                break;
        }
    }

    printf("\033[31;1 << %c%hhu [%hhu]\033[0m\n", msg->cmd, msg->rid, msg->len);
    hal->tx_bytes += msg->len + 4;
}

/* Main input routine */
void *HAL_read_thread(void *args)
{
    struct HAL_t *hal = (struct HAL_t *) args;
    HALMsg msg;
    HALResource *res;

    while (true){
        res = NULL;
        HAL_socket_accept(hal);

        if (! HAL_read_msg(hal, &msg))
            continue;

        pthread_mutex_lock(&hal->mutex);
        hal->rx_bytes += msg.len + 4;
        pthread_mutex_unlock(&hal->mutex);

        printf("\033[32;1 >> %c%hhu [%hhu]\033[0m\n", msg.cmd, msg.rid, msg.len);

        if (IS(msg, PING)){
            HAL_reset_msg(&msg);
            msg.cmd = PING;
            pthread_mutex_lock(&hal->mutex);
            HAL_send_msg(hal, &msg);
            pthread_mutex_unlock(&hal->mutex);
        }
        else if (IS(msg, SENSOR)){
            if ((msg.rid) >= hal->n_sensors)
                continue;
            res = hal->sensors + msg.rid;
            float val = (((msg.data[0])<<8) | (msg.data[1])); // MSB first
            val /= 1023;

            HAL_LOCK(res);
            res->data.f = val;
            HAL_SIGNAL(res);
            HAL_UNLOCK(res);
        } 
        else if (IS(msg, TRIGGER)){
            if ((msg.rid) >= hal->n_triggers)
                continue;
            res = hal->triggers + msg.rid;

            HAL_LOCK(res);
            res->data.b = msg.data[0] != 0;
            HAL_SIGNAL(res);
            HAL_UNLOCK(res);
        }
        else if (IS(msg, SWITCH)){
            if ((msg.rid) >= hal->n_switchs)
                continue;
            res = hal->switchs + msg.rid;

            HAL_LOCK(res);
            res->data.b = msg.data[0] != 0;
            HAL_SIGNAL(res);
            HAL_UNLOCK(res);
        }
        else {
            // Asume it's an animation for now (will be discarded later if not)
            if (msg.rid >= hal->n_animations)
                continue;
            res = hal->animations + msg.rid;

            if (IS(msg, ANIMATION_PLAY)){
                HAL_LOCK(res);
                res->data.hhu4[1] = msg.data[0] != 0;
                HAL_SIGNAL(res);
                HAL_UNLOCK(res);
            }
            else if (IS(msg, ANIMATION_LOOP)){
                HAL_LOCK(res);
                res->data.hhu4[0] = msg.data[0] != 0;
                HAL_SIGNAL(res);
                HAL_UNLOCK(res);
            }
            else if (IS(msg, ANIMATION_DELAY)){
                HAL_LOCK(res);
                res->data.hhu4[2] = msg.data[0];
                if (res->data.hhu4[2] == 0)
                    res->data.hhu4[2] = 1; // Avoid zero-div error
                HAL_SIGNAL(res);
                HAL_UNLOCK(res);
            }
            else if (IS(msg, ANIMATION_FRAMES)){
                HAL_LOCK(res);
                HAL_SIGNAL(res);
                HAL_UNLOCK(res);
            }
        }
    }

    return NULL;
}

int HAL_ask_trigger(HALResource *trigger, bool *res)
{
    HALMsg req = {.cmd=(ASK|TRIGGER), .rid=trigger->id, .len=0};
    HAL_LOCK(trigger);
    HAL_send_msg(trigger->hal, &req);
    if (HAL_WAIT(trigger) != 0)
        return -EAGAIN;
    *res = trigger->data.b;
    HAL_UNLOCK(trigger);
    return 0;
}

int HAL_set_switch(HALResource *sw, bool on)
{
    HALMsg req = {.cmd=(CHANGE|SWITCH), .rid=sw->id, .len=1};
    req.data[0] = on ? 1 : 0;

    HAL_LOCK(sw);
    HAL_send_msg(sw->hal, &req);
    if (HAL_WAIT(sw) != 0)
        return -EAGAIN;
    HAL_UNLOCK(sw);
    return 0;
}

int HAL_ask_switch(HALResource *sw, bool *res)
{
    HALMsg req = {.cmd=(ASK|SWITCH), .rid=sw->id, .len=0};
    HAL_LOCK(sw);
    HAL_send_msg(sw->hal, &req);
    if (HAL_WAIT(sw) != 0)
        return -EAGAIN;
    *res = sw->data.b;
    HAL_UNLOCK(sw);
    return 0;
}

int HAL_upload_anim(
    HALResource *anim, 
    unsigned char len, 
    const unsigned char *frames
){
    HALMsg req = {.cmd=(CHANGE|ANIMATION_FRAMES), .rid=anim->id, .len=len};
    memcpy(req.data, frames, len);

    HAL_LOCK(anim);
    HAL_send_msg(anim->hal, &req);
    if (HAL_WAIT(anim) != 0)
        return -EAGAIN;
    HAL_UNLOCK(anim);
    return 0;
}

int HAL_ask_anim_play(HALResource *anim, bool *res)
{
    HALMsg req = {.cmd=(ASK|ANIMATION_PLAY), .rid=anim->id, .len=0};
    HAL_LOCK(anim);
    HAL_send_msg(anim->hal, &req);
    if (HAL_WAIT(anim) != 0)
        return -EAGAIN;
    *res = anim->data.hhu4[1] ? true : false;
    HAL_UNLOCK(anim);
    return 0;
}

int HAL_set_anim_play(HALResource *anim, bool play)
{
    HALMsg req = {.cmd=(CHANGE|ANIMATION_PLAY), .rid=anim->id, .len=1};
    req.data[0] = play ? 1 : 0;

    HAL_LOCK(anim);
    HAL_send_msg(anim->hal, &req);
    if (HAL_WAIT(anim) != 0)
        return -EAGAIN;
    HAL_UNLOCK(anim);
    return 0;
}

int HAL_ask_anim_loop(HALResource *anim, bool *res)
{
    HALMsg req = {.cmd=(ASK|ANIMATION_LOOP), .rid=anim->id, .len=0};
    HAL_LOCK(anim);
    HAL_send_msg(anim->hal, &req);
    if (HAL_WAIT(anim) != 0)
        return -EAGAIN;
    *res = anim->data.hhu4[0] ? true : false;
    HAL_UNLOCK(anim);
    return 0;
}

int HAL_set_anim_loop(HALResource *anim, bool loop)
{
    HALMsg req = {.cmd=(CHANGE|ANIMATION_LOOP), .rid=anim->id, .len=1};
    req.data[0] = loop ? 1 : 0;

    HAL_LOCK(anim);
    HAL_send_msg(anim->hal, &req);
    if (HAL_WAIT(anim) != 0)
        return -EAGAIN;
    HAL_UNLOCK(anim);
    return 0;
}

int HAL_ask_anim_delay(HALResource *anim, unsigned char *res)
{
    HALMsg req = {.cmd=(ASK|ANIMATION_DELAY), .rid=anim->id, .len=0};
    HAL_LOCK(anim);
    HAL_send_msg(anim->hal, &req);
    if (HAL_WAIT(anim) != 0)
        return -EAGAIN;
    *res = anim->data.hhu4[2];
    HAL_UNLOCK(anim);
    return 0;
}

int HAL_set_anim_delay(HALResource *anim, unsigned char delay)
{
    HALMsg req = {.cmd=(CHANGE|ANIMATION_DELAY), .rid=anim->id, .len=1};
    req.data[0] = delay;

    HAL_LOCK(anim);
    HAL_send_msg(anim->hal, &req);
    if (HAL_WAIT(anim) != 0)
        return -EAGAIN;
    HAL_UNLOCK(anim);
    return 0;
}

int HAL_ask_sensor(HALResource *sensor, float *res)
{
    HALMsg req = {.cmd=(ASK|SENSOR), .rid=sensor->id, .len=0};
    HAL_LOCK(sensor);
    HAL_send_msg(sensor->hal, &req);
    if (HAL_WAIT(sensor) != 0)
        return -EAGAIN;
    *res = sensor->data.f;
    HAL_UNLOCK(sensor);
    return 0;
}

size_t HAL_rx_bytes(struct HAL_t *hal)
{
    size_t res = 0;
    pthread_mutex_lock(&hal->mutex);
    res = hal->rx_bytes;
    pthread_mutex_unlock(&hal->mutex);
    return res;
}

size_t HAL_tx_bytes(struct HAL_t *hal)
{
    size_t res = 0;
    pthread_mutex_lock(&hal->mutex);
    res = hal->tx_bytes;
    pthread_mutex_unlock(&hal->mutex);
    return res;
}

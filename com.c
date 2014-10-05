#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <errno.h>
#include <pthread.h>

#include "arduino-serial-lib.h"
#include "HALResource.h"
#include "HALMsg.h"
#include "utils.h"
#include "logger.h"

#define HAL_LOCK(r) pthread_mutex_lock(&((r)->hal->mutex))
#define HAL_UNLOCK(r) pthread_mutex_unlock(&((r)->hal->mutex))

static const struct timespec HAL_req_timeout = {.tv_sec=0, .tv_nsec=500000000};

static inline int wait_timeout(struct timespec *timeout)
{
    int res = clock_gettime(CLOCK_REALTIME, timeout);
    if (res != 0)
        return res;

    unsigned long int nsecs = timeout->tv_nsec + HAL_req_timeout.tv_nsec;
    if (nsecs > 1000000000l)
        timeout->tv_sec++;
    timeout->tv_nsec = nsecs % 1000000000l;
    return 0;
}

static inline int HAL_SIGNAL(HALResource *r)
{
    DEBUG("SIGNAL %s", (r)->name);
    return pthread_cond_signal(&(r)->cond);
}

static inline int HAL_WAIT(HALResource *r)
{
    struct timespec until;
    wait_timeout(&until);

    DEBUG("WAIT %s (until %ds %dns)", r->name, (int) until.tv_sec, (int) until.tv_nsec);
    int res = pthread_cond_timedwait(&(r->cond), &(r->hal->mutex), &until);
    if (res == ETIMEDOUT)
        DEBUG("WAIT TIMEOUT %s", r->name);
    return res;
}

/* Establish communication with arduino && initialize HAL structure */
bool HAL_init(struct HAL_t *hal, const char *arduino_dev)
{
    memset(hal, 0, sizeof(struct HAL_t));
    hal->serial_fd = serialport_init(arduino_dev, 115200);
    if (hal->serial_fd < 0){
        ERROR("Unable to open serial port %s", arduino_dev);
        return false;
    }

    sleep(1);

    HALMsg msg;
    RESET(&msg);
    HALMsg_read_command(hal, &msg, BOOT);

    has_boot:
        msg.cmd = VERSION;
        HALMsg_write(hal, &msg);

        do {
            HALMsg_read(hal, &msg);
            if (msg.cmd == BOOT)
                goto has_boot;
        } while (! CMD(&msg, VERSION));

        if (msg.len < 40){
            ERROR("Invalid Arduino version");
            goto fail;
        }
        memcpy(hal->version, msg.data, 40);
        INFO("Arduino version: %40s", msg.data);

        RESET(&msg);
        msg.cmd = TREE;
        HALMsg_write(hal, &msg);

        for (int i=0; i<4; i++){
            do HALMsg_read(hal, &msg); while (! CMD(&msg, TREE));
            if (! IS_VALID(&msg)){
                ERROR("Invalid message received (bad checksum)");
                goto fail;
            }

            size_t N = msg.rid;
            HALResource *res = calloc(N, sizeof(HALResource));
            if (! res){
                ERROR("Memory allocation error");
                goto fail;
            }

            if (msg.data[0] == SENSOR){hal->n_sensors = N; hal->sensors = res;}
            else if (msg.data[0] == TRIGGER){hal->n_triggers = N; hal->triggers = res;}
            else if (msg.data[0] == SWITCH){hal->n_switchs = N; hal->switchs = res;}
            else if (msg.data[0] == ANIMATION_FRAMES){hal->n_animations = N; hal->animations = res;}
            else goto fail;
            INFO("Loading %lu resources of type %c",N, msg.data[0]);

            for (size_t j=0; j<N; j++){
                HALMsg_read(hal, &msg);
                if (! IS_VALID(&msg)){
                    ERROR("Invalid message received (bad checksum)");
                    goto fail;
                }
                msg.data[msg.len] = '\0';
                HALResource_init(res+j, (const char *) msg.data, j, hal);
                INFO("    Loaded [%lu] %s", j, res[j].name);
            }
        }

        pthread_mutex_init(&hal->mutex, NULL);
        hal->ready = true;
        INFO("Ready !");
        return true;

    fail:
        close(hal->serial_fd);
        if (hal->n_sensors) HALResource_destroyAll(&hal->n_sensors, hal->sensors);
        if (hal->n_triggers) HALResource_destroyAll(&hal->n_triggers, hal->triggers);
        if (hal->n_switchs) HALResource_destroyAll(&hal->n_switchs, hal->switchs);
        if (hal->n_animations) HALResource_destroyAll(&hal->n_animations, hal->animations);
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

/* Main input routine */
void *HAL_read_thread(void *args)
{
    struct HAL_t *hal = (struct HAL_t *) args;
    HALMsg msg;
    HALResource *res;

    while (true){
        res = NULL;
        HAL_socket_accept(hal);

        HALMsg_read(hal, &msg);
        if (! IS_VALID(&msg)){
            WARN("Invalid message (bad checksum); drop");
            continue;
        }

        pthread_mutex_lock(&hal->mutex);
        hal->rx_bytes += msg.len + 4;
        pthread_mutex_unlock(&hal->mutex);

        if (CMD(&msg, HAL_PING)){
            RESET(&msg);
            msg.cmd = HAL_PING;
            pthread_mutex_lock(&hal->mutex);
            HALMsg_write(hal, &msg);
            pthread_mutex_unlock(&hal->mutex);
        }
        else if (CMD(&msg, SENSOR)){
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
        else if (CMD(&msg, TRIGGER)){
            if ((msg.rid) >= hal->n_triggers)
                continue;
            res = hal->triggers + msg.rid;

            HAL_LOCK(res);
            res->data.b = msg.data[0] != 0;
            HAL_SIGNAL(res);
            HAL_UNLOCK(res);
        }
        else if (CMD(&msg, SWITCH)){
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

            if (CMD(&msg, ANIMATION_PLAY)){
                HAL_LOCK(res);
                res->data.hhu4[1] = msg.data[0] != 0;
                HAL_SIGNAL(res);
                HAL_UNLOCK(res);
            }
            else if (CMD(&msg, ANIMATION_LOOP)){
                HAL_LOCK(res);
                res->data.hhu4[0] = msg.data[0] != 0;
                HAL_SIGNAL(res);
                HAL_UNLOCK(res);
            }
            else if (CMD(&msg, ANIMATION_DELAY)){
                HAL_LOCK(res);
                res->data.hhu4[2] = msg.data[0];
                if (res->data.hhu4[2] == 0)
                    res->data.hhu4[2] = 1; // Avoid zero-div error
                HAL_SIGNAL(res);
                HAL_UNLOCK(res);
            }
            else if (CMD(&msg, ANIMATION_FRAMES)){
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
    int ret = 0;
    HALMsg req = {.cmd=(PARAM_ASK|TRIGGER), .rid=trigger->id, .len=0};

    HAL_LOCK(trigger);
    HALMsg_write(trigger->hal, &req);
    if (HAL_WAIT(trigger) != 0)
        ret = -EAGAIN;
    else
        *res = trigger->data.b;
    HAL_UNLOCK(trigger);

    return ret;
}

int HAL_set_switch(HALResource *sw, bool on)
{
    int ret = 0;
    HALMsg req = {.cmd=(PARAM_CHANGE|SWITCH), .rid=sw->id, .len=1};
    req.data[0] = on ? 1 : 0;

    HAL_LOCK(sw);
    HALMsg_write(sw->hal, &req);
    if (HAL_WAIT(sw) != 0)
        ret = -EAGAIN;
    HAL_UNLOCK(sw);

    return ret;
}

int HAL_ask_switch(HALResource *sw, bool *res)
{
    int ret = 0;
    HALMsg req = {.cmd=(PARAM_ASK|SWITCH), .rid=sw->id, .len=0};

    HAL_LOCK(sw);
    HALMsg_write(sw->hal, &req);
    if (HAL_WAIT(sw) != 0)
        ret = -EAGAIN;
    else
        *res = sw->data.b;
    HAL_UNLOCK(sw);
    
    return ret;
}

int HAL_upload_anim(
    HALResource *anim, 
    unsigned char len, 
    const unsigned char *frames
){
    int ret = 0;
    HALMsg req = {.cmd=(PARAM_CHANGE|ANIMATION_FRAMES), .rid=anim->id, .len=len};
    memcpy(req.data, frames, len);

    HAL_LOCK(anim);
    HALMsg_write(anim->hal, &req);
    if (HAL_WAIT(anim) != 0)
        ret = -EAGAIN;
    HAL_UNLOCK(anim);

    return ret;
}

int HAL_ask_anim_play(HALResource *anim, bool *res)
{
    int ret = 0;
    HALMsg req = {.cmd=(PARAM_ASK|ANIMATION_PLAY), .rid=anim->id, .len=0};

    HAL_LOCK(anim);
    HALMsg_write(anim->hal, &req);
    if (HAL_WAIT(anim) != 0)
        ret = -EAGAIN;
    else
        *res = anim->data.hhu4[1] ? true : false;
    HAL_UNLOCK(anim);

    return ret;
}

int HAL_set_anim_play(HALResource *anim, bool play)
{
    int ret = 0;
    HALMsg req = {.cmd=(PARAM_CHANGE|ANIMATION_PLAY), .rid=anim->id, .len=1};
    req.data[0] = play ? 1 : 0;

    HAL_LOCK(anim);
    HALMsg_write(anim->hal, &req);
    if (HAL_WAIT(anim) != 0)
        ret = -EAGAIN;
    HAL_UNLOCK(anim);

    return ret;
}

int HAL_ask_anim_loop(HALResource *anim, bool *res)
{
    int ret = 0;
    HALMsg req = {.cmd=(PARAM_ASK|ANIMATION_LOOP), .rid=anim->id, .len=0};

    HAL_LOCK(anim);
    HALMsg_write(anim->hal, &req);
    if (HAL_WAIT(anim) != 0)
        ret = -EAGAIN;
    else
        *res = anim->data.hhu4[0] ? true : false;
    HAL_UNLOCK(anim);

    return ret;
}

int HAL_set_anim_loop(HALResource *anim, bool loop)
{
    int ret = 0;
    HALMsg req = {.cmd=(PARAM_CHANGE|ANIMATION_LOOP), .rid=anim->id, .len=1};
    req.data[0] = loop ? 1 : 0;

    HAL_LOCK(anim);
    HALMsg_write(anim->hal, &req);
    if (HAL_WAIT(anim) != 0)
        ret = -EAGAIN;
    HAL_UNLOCK(anim);

    return ret;
}

int HAL_ask_anim_delay(HALResource *anim, unsigned char *res)
{
    int ret = 0;
    HALMsg req = {.cmd=(PARAM_ASK|ANIMATION_DELAY), .rid=anim->id, .len=0};

    HAL_LOCK(anim);
    HALMsg_write(anim->hal, &req);
    if (HAL_WAIT(anim) != 0)
        ret = -EAGAIN;
    else
        *res = anim->data.hhu4[2];
    HAL_UNLOCK(anim);
    
    return ret;
}

int HAL_set_anim_delay(HALResource *anim, unsigned char delay)
{
    int ret = 0;
    HALMsg req = {.cmd=(PARAM_CHANGE|ANIMATION_DELAY), .rid=anim->id, .len=1};
    req.data[0] = delay;

    HAL_LOCK(anim);
    HALMsg_write(anim->hal, &req);
    if (HAL_WAIT(anim) != 0)
        ret = -EAGAIN;
    HAL_UNLOCK(anim);

    return ret;
}

int HAL_ask_sensor(HALResource *sensor, float *res)
{
    int ret = 0;
    HALMsg req = {.cmd=(PARAM_ASK|SENSOR), .rid=sensor->id, .len=0};

    HAL_LOCK(sensor);
    HALMsg_write(sensor->hal, &req);
    if (HAL_WAIT(sensor) != 0)
        ret = -EAGAIN;
    else
        *res = sensor->data.f;
    HAL_UNLOCK(sensor);

    return ret;
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

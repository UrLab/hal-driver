#include "hal.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>
#include <glob.h>
#include <unistd.h>
#include <errno.h>

#define min(A,B) ((A) < (B)) ? (A) : (B)

const char *ARDUINO_DEV_PATH[] = {
    "/dev/tty.usbmodem*",
    "/dev/ttyUSB*",
    "/dev/ttyACM*"
};

/* === Sensors === */
static int sensor_read(HALConnection *conn, unsigned char sensor_id, char *buf, size_t size, off_t offset)
{
    HALMsg msg = {.cmd=(PARAM_ASK|SENSOR), .rid=sensor_id, .len=0};
    HALErr err = HALConn_request(conn, &msg);
    if (err != OK){
        return -EAGAIN;
    }
    float sensor_val = ((msg.data[0]<<8) | msg.data[1]);
    sensor_val /= 1024;
    return snprintf(buf, size, "%f\n", sensor_val);
}

/* === Triggers === */
static int trigger_read(HALConnection *conn, unsigned char trigger_id, char *buf, size_t size, off_t offset)
{
    HALMsg msg = {.cmd=(PARAM_ASK|TRIGGER), .rid=trigger_id, .len=0};
    HALErr err = HALConn_request(conn, &msg);
    if (err != OK){
        return -EAGAIN;
    }
    return snprintf(buf, size, "%c\n", msg.data[0] ? '1' : '0');
}

/* === Switchs === */
static int switch_read(HALConnection *conn, unsigned char switch_id, char *buf, size_t size, off_t offset)
{
    HALMsg msg = {.cmd=(PARAM_ASK|SWITCH), .rid=switch_id, .len=0};
    HALErr err = HALConn_request(conn, &msg);
    if (err != OK){
        return -EAGAIN;
    }
    return snprintf(buf, size, "%c\n", msg.data[0] ? '1' : '0');
}

static int switch_write(HALConnection *conn, unsigned char switch_id, const char *buf, size_t size, off_t offset)
{
    HALMsg msg = {.cmd=(PARAM_CHANGE|SWITCH), .rid=switch_id, .len=1};
    msg.data[0] = (buf[0] == '0') ? 0 : 1;
    HALErr err = HALConn_request(conn, &msg);
    if (err != OK){
        return -EAGAIN;
    }
    return size;
}


/* === Animations FPS === */
static int anim_fps_read(HALConnection *conn, unsigned char anim_id, char *buf, size_t size, off_t offset)
{
    HALMsg msg = {.cmd=(PARAM_ASK|ANIMATION_DELAY), .rid=anim_id, .len=0};
    HALErr err = HALConn_request(conn, &msg);
    if (err != OK){
        return -EAGAIN;
    }
    int delay = msg.data[0];
    if (delay == 0){
        return -EINVAL;
    }
    return snprintf(buf, size, "%d\n", 1000/delay);
}

static int anim_fps_write(HALConnection *conn, unsigned char anim_id, const char *buf, size_t size, off_t offset)
{
    char *endptr;
    int fps = strtol(buf, &endptr, 10);
    if (endptr == buf || fps < 1 || fps > 250){
        return -EINVAL;
    }

    HALMsg msg = {.cmd=(PARAM_CHANGE|ANIMATION_DELAY), .rid=anim_id, .len=1};
    msg.data[0] = (1000/fps);
    HALErr err = HALConn_request(conn, &msg);
    if (err != OK){
        return -EAGAIN;
    }
    return size;
}

/* === Animations loop === */
static int anim_loop_read(HALConnection *conn, unsigned char anim_id, char *buf, size_t size, off_t offset)
{
    HALMsg msg = {.cmd=(PARAM_ASK|ANIMATION_LOOP), .rid=anim_id, .len=0};
    HALErr err = HALConn_request(conn, &msg);
    if (err != OK){
        return -EAGAIN;
    }
    return snprintf(buf, size, "%c\n", msg.data[0] ? '1' : '0');
}

static int anim_loop_write(HALConnection *conn, unsigned char anim_id, const char *buf, size_t size, off_t offset)
{
    HALMsg msg = {.cmd=(PARAM_CHANGE|ANIMATION_LOOP), .rid=anim_id, .len=1};
    msg.data[0] = (buf[0] == '0') ? 0 : 1;
    HALErr err = HALConn_request(conn, &msg);
    if (err != OK){
        return -EAGAIN;
    }
    return size;
}

/* === Animations playing === */
static int anim_play_read(HALConnection *conn, unsigned char anim_id, char *buf, size_t size, off_t offset)
{
    HALMsg msg = {.cmd=(PARAM_ASK|ANIMATION_PLAY), .rid=anim_id, .len=0};
    HALErr err = HALConn_request(conn, &msg);
    if (err != OK){
        return -EAGAIN;
    }
    return snprintf(buf, size, "%c\n", msg.data[0] ? '1' : '0');
}

static int anim_play_write(HALConnection *conn, unsigned char anim_id, const char *buf, size_t size, off_t offset)
{
    HALMsg msg = {.cmd=(PARAM_CHANGE|ANIMATION_PLAY), .rid=anim_id, .len=1};
    msg.data[0] = (buf[0] == '0') ? 0 : 1;
    HALErr err = HALConn_request(conn, &msg);
    if (err != OK){
        return -EAGAIN;
    }
    return size;
}

/* === Animations frames === */
static int anim_frames_read(HALConnection *conn, unsigned char anim_id, char *buf, size_t size, off_t offset)
{
    HALMsg msg = {.cmd=(PARAM_ASK|ANIMATION_FRAMES), .rid=anim_id, .len=0};
    HALErr err = HALConn_request(conn, &msg);
    if (err != OK){
        return -EAGAIN;
    }
    size_t len = min(size, msg.len);
    memcpy(buf, msg.data, len);
    return len;
}

static int anim_frames_write(HALConnection *conn, unsigned char anim_id, const char *buf, size_t size, off_t offset)
{
    if (size == 0 || size > 255){
        return -EINVAL;
    }
    HALMsg msg = {.cmd=(PARAM_CHANGE|ANIMATION_FRAMES), .rid=anim_id, .len=size};
    memcpy(msg.data, buf, msg.len);

    HALErr err = HALConn_request(conn, &msg);
    if (err != OK){
        return -EAGAIN;
    }
    return size;
}

/* === driver === */
static int driver_rx_bytes_read(HALConnection *conn, unsigned char unused_id, char *buf, size_t size, off_t offset)
{
    unsigned long int rx = HALConn_rx_bytes(conn);
    return snprintf(buf, size, "%lu\n",  rx);
}

static int driver_tx_bytes_read(HALConnection *conn, unsigned char unused_id, char *buf, size_t size, off_t offset)
{
    unsigned long int tx = HALConn_tx_bytes(conn);
    return snprintf(buf, size, "%lu\n",  tx);
}

static int driver_loglevel_read(HALConnection *conn, unsigned char unused_id, char *buf, size_t size, off_t offset)
{
    return snprintf(buf, size, "%d\n",  current_log_level);
}

static int driver_loglevel_write(HALConnection *conn, unsigned char unused_id, const char *buf, size_t size, off_t offset)
{
    char *endptr;
    int val = strtol(buf, &endptr, 10);
    if (endptr == buf || val < SILENT || val > DUMP){
        return -EINVAL;
    }
    current_log_level = val;
    return size;
}

static int driver_version_read(HALConnection *conn, unsigned char unused_id, char *buf, size_t size, off_t offset)
{
#ifndef HAL_DRIVER_VERSION
    return snprintf(buf, size, "YOLO\n");
#else
    return snprintf(buf, size, "%s\n", HAL_DRIVER_VERSION);
#endif
}

static int driver_uptime_read(HALConnection *conn, unsigned char unused_id, char *buf, size_t size, off_t offset)
{
    unsigned long int rx = HALConn_uptime(conn);
    return snprintf(buf, size, "%lu\n",  rx);
}


/* === Loading functions === */

static void HAL_insert_animation(HALFS *root, const char *name, unsigned char id)
{
    char path[255];
    HALFS *node = NULL;

    sprintf(path, "/animations/%s/fps", name);
    node = HALFS_insert(root, path);
    node->ops.mode = 0666;
    node->ops.size = 5;
    node->ops.read = anim_fps_read;
    node->ops.write = anim_fps_write;
    node->id = id;

    sprintf(path, "/animations/%s/loop", name);
    node = HALFS_insert(root, path);
    node->ops.mode = 0666;
    node->ops.size = 2;
    node->ops.read = anim_loop_read;
    node->ops.write = anim_loop_write;
    node->id = id;

    sprintf(path, "/animations/%s/play", name);
    node = HALFS_insert(root, path);
    node->ops.mode = 0666;
    node->ops.size = 2;
    node->ops.read = anim_play_read;
    node->ops.write = anim_play_write;
    node->id = id;

    sprintf(path, "/animations/%s/frames", name);
    node = HALFS_insert(root, path);
    node->ops.mode = 0666;
    node->ops.size = 255;
    node->ops.read = anim_frames_read;
    node->ops.write = anim_frames_write;
    node->id = id;
}

static HALErr HAL_load(HAL *hal)
{
    /* Ask Arduino resource tree */
    HALMsg msg;
    msg.cmd = PARAM_ASK | TREE;
    msg.len = 0;
    msg.rid = 0;
    msg.chk = HALMsg_checksum(&msg);
    HALErr err = HALConn_write_message(hal->conn, &msg);
    if (err != OK){
        HAL_ERROR(err, "Unable to ask resource tree");
        return err;
    }

    char path[256];
    char *file;
    unsigned char n;
    HALFS *node = NULL;

    /* Get Tree messages from Arduino */
    for (int i=0; i<4 && err == OK; i++){
        do {
            err = HALConn_read_message(hal->conn, &msg);
            if (err != OK){
                HAL_ERROR(err, "Unable to get resource tree branch");
                return err;
            }
        } while (MSG_TYPE(&msg) != TREE);

        n = msg.rid;
        switch (msg.data[0]){
            case SENSOR:
                HAL_DEBUG("Loading %hhu sensors", n);
                file = path + sprintf(path, "/sensors/");
                for (unsigned char i=0; i<n; i++){
                    err = HALConn_read_message(hal->conn, &msg);
                    if (err != OK){
                        HAL_ERROR(err, "Unable to get sensor %hhu", i);
                        return err;
                    }
                    msg.data[msg.len] = '\0';
                    strcpy(file, (const char *) msg.data);
                    node = HALFS_insert(hal->root, path);
                    node->ops.mode = 0444;
                    node->ops.read = sensor_read;
                    node->ops.size = 13;
                    node->id = i;
                    HAL_DEBUG("  Inserted sensor %s", node->name);
                }
                break;
            case SWITCH:
                HAL_DEBUG("Loading %hhu switchs", n);
                file = path + sprintf(path, "/switchs/");
                for (unsigned char i=0; i<n; i++){
                    err = HALConn_read_message(hal->conn, &msg);
                    if (err != OK){
                        HAL_ERROR(err, "Unable to get switch %hhu", i);
                        return err;
                    }
                    msg.data[msg.len] = '\0';
                    strcpy(file, (const char *) msg.data);
                    node = HALFS_insert(hal->root, path);
                    node->ops.mode = 0666;
                    node->ops.write = switch_write;
                    node->ops.read = switch_read;
                    node->ops.size = 2;
                    node->id = i;
                    HAL_DEBUG("  Inserted switch %s", node->name);
                }
                break;
            case ANIMATION_FRAMES:
                HAL_DEBUG("Loading %hhu animations", n);
                for (unsigned char i=0; i<n; i++){
                    err = HALConn_read_message(hal->conn, &msg);
                    if (err != OK){
                        HAL_ERROR(err, "Unable to get animation %hhu", i);
                        return err;
                    }
                    msg.data[msg.len] = '\0';
                    HAL_insert_animation(hal->root, (const char *) msg.data, msg.rid);
                    HAL_DEBUG("  Inserted animation %s", (const char*) msg.data);
                }
                break;
            case TRIGGER:
                HAL_DEBUG("Loading %hhu triggers", n);
                file = path + sprintf(path, "/triggers/");

                hal->n_triggers = n;
                hal->trigger_names = calloc(n, sizeof(char*));
                for (unsigned char i=0; i<n; i++){
                    err = HALConn_read_message(hal->conn, &msg);
                    if (err != OK){
                        HAL_ERROR(err, "Unable to get trigger %hhu", i);
                        return err;
                    }
                    msg.data[msg.len] = '\0';
                    strcpy(file, (const char *) msg.data);
                    hal->trigger_names[i] = strndup((const char *) msg.data, 255);
                    node = HALFS_insert(hal->root, path);
                    node->ops.mode = 0444;
                    node->ops.read = trigger_read;
                    node->ops.size = 2;
                    node->id = i;
                    HAL_DEBUG("  Inserted trigger %s", node->name);
                }
                break;
        }
    }

    node = HALFS_insert(hal->root, "/driver/rx_bytes");
    node->ops.mode = 0444;
    node->ops.read = driver_rx_bytes_read;
    node->ops.size = 11;

    node = HALFS_insert(hal->root, "/driver/tx_bytes");
    node->ops.mode = 0444;
    node->ops.read = driver_tx_bytes_read;
    node->ops.size = 11;

    node = HALFS_insert(hal->root, "/driver/loglevel");
    node->ops.mode = 0666;
    node->ops.read = driver_loglevel_read;
    node->ops.write = driver_loglevel_write;
    node->ops.size = 2;

    node = HALFS_insert(hal->root, "/driver/version");
    node->ops.mode = 0444;
    node->ops.read = driver_version_read;
    node->ops.size = 41;

    node = HALFS_insert(hal->root, "/driver/uptime");
    node->ops.mode = 0444;
    node->ops.read = driver_uptime_read;
    node->ops.size = 11;

    node = HALFS_insert(hal->root, "/events");
    node->ops.target = HALConn_sock_path(hal->conn);

    HAL_DEBUG("Inserted driver files");

    return OK;
}

HAL *HAL_connect()
{
    HALConnection *conn = NULL;
    HAL *res = NULL;
    glob_t globbuf;
    globbuf.gl_offs = 0;

    int flag = GLOB_DOOFFS;
    for(size_t i = 0; i < sizeof(ARDUINO_DEV_PATH)/sizeof(char *); i++){
        if (i == 1){
            flag = flag | GLOB_APPEND;
        }
        glob(ARDUINO_DEV_PATH[i], flag, NULL, &globbuf);
    }
    HAL_INFO("Found %lu possible arduinos in /dev/", (long unsigned int) globbuf.gl_pathc);
    
    char sock_path[256];
    snprintf(sock_path, sizeof(sock_path), "/tmp/hal-%d.sock", (int) time(NULL));

    for (size_t i = 0; i < globbuf.gl_pathc; i++){
        HAL_DEBUG("Trying %s", globbuf.gl_pathv[i]);
        conn = HALConn_open(globbuf.gl_pathv[i], sock_path);
        sleep(2);

        if (! conn){
            HAL_WARN("Skip %s", globbuf.gl_pathv[i]);
        } else {
            res = calloc(1, sizeof(HAL));
            res->conn = conn;
            res->root = HALFS_create("/");
            if (HAL_load(res) == OK){
                HAL_INFO("Connected to %s !", globbuf.gl_pathv[i]);
                HALConn_run_reader(conn, res->trigger_names, res->n_triggers);
            } else {
                HALFS_destroy(res->root);
                free(res);
                HALConn_close(conn);
                res = NULL;
            }
        }
    }
    globfree(&globbuf);
    return res;
}

void HAL_release(HAL *hal)
{
    HALFS_destroy(hal->root);
    for (size_t i=0; i<hal->n_triggers; i++){
        free((void*) hal->trigger_names[i]);
    }
    free(hal->trigger_names);
    HALConn_close(hal->conn);
    free(hal);
}

#include "hal.h"
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
int sensor_read(HALConnection *conn, unsigned char sensor_id, char *buf, size_t size, off_t offset)
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
int trigger_read(HALConnection *conn, unsigned char trigger_id, char *buf, size_t size, off_t offset)
{
    HALMsg msg = {.cmd=(PARAM_ASK|TRIGGER), .rid=trigger_id, .len=0};
    HALErr err = HALConn_request(conn, &msg);
    if (err != OK){
        return -EAGAIN;
    }
    return snprintf(buf, size, "%c\n", msg.data[0] ? '1' : '0');
}

/* === Switchs === */
int switch_read(HALConnection *conn, unsigned char switch_id, char *buf, size_t size, off_t offset)
{
    HALMsg msg = {.cmd=(PARAM_ASK|SWITCH), .rid=switch_id, .len=0};
    HALErr err = HALConn_request(conn, &msg);
    if (err != OK){
        return -EAGAIN;
    }
    return snprintf(buf, size, "%c\n", msg.data[0] ? '1' : '0');
}

int switch_write(HALConnection *conn, unsigned char switch_id, const char *buf, size_t size, off_t offset)
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
int anim_fps_read(HALConnection *conn, unsigned char anim_id, char *buf, size_t size, off_t offset)
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

int anim_fps_write(HALConnection *conn, unsigned char anim_id, const char *buf, size_t size, off_t offset)
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
int anim_loop_read(HALConnection *conn, unsigned char switch_id, char *buf, size_t size, off_t offset)
{
    HALMsg msg = {.cmd=(PARAM_ASK|ANIMATION_LOOP), .rid=switch_id, .len=0};
    HALErr err = HALConn_request(conn, &msg);
    if (err != OK){
        return -EAGAIN;
    }
    return snprintf(buf, size, "%c\n", msg.data[0] ? '1' : '0');
}

int anim_loop_write(HALConnection *conn, unsigned char switch_id, const char *buf, size_t size, off_t offset)
{
    HALMsg msg = {.cmd=(PARAM_CHANGE|ANIMATION_LOOP), .rid=switch_id, .len=1};
    msg.data[0] = (buf[0] == '0') ? 0 : 1;
    HALErr err = HALConn_request(conn, &msg);
    if (err != OK){
        return -EAGAIN;
    }
    return size;
}

/* === Animations playing === */
int anim_play_read(HALConnection *conn, unsigned char switch_id, char *buf, size_t size, off_t offset)
{
    HALMsg msg = {.cmd=(PARAM_ASK|ANIMATION_PLAY), .rid=switch_id, .len=0};
    HALErr err = HALConn_request(conn, &msg);
    if (err != OK){
        return -EAGAIN;
    }
    return snprintf(buf, size, "%c\n", msg.data[0] ? '1' : '0');
}

int anim_play_write(HALConnection *conn, unsigned char switch_id, const char *buf, size_t size, off_t offset)
{
    HALMsg msg = {.cmd=(PARAM_CHANGE|ANIMATION_PLAY), .rid=switch_id, .len=1};
    msg.data[0] = (buf[0] == '0') ? 0 : 1;
    HALErr err = HALConn_request(conn, &msg);
    if (err != OK){
        return -EAGAIN;
    }
    return size;
}

/* === Animations frames === */
int anim_frames_read(HALConnection *conn, unsigned char switch_id, char *buf, size_t size, off_t offset)
{
    HALMsg msg = {.cmd=(PARAM_ASK|ANIMATION_FRAMES), .rid=switch_id, .len=0};
    HALErr err = HALConn_request(conn, &msg);
    if (err != OK){
        return -EAGAIN;
    }
    size_t len = min(size, msg.len);
    memcpy(buf, msg.data, len);
    return len;
}

int anim_frames_write(HALConnection *conn, unsigned char switch_id, const char *buf, size_t size, off_t offset)
{
    if (size == 0 || size > 255){
        return -EINVAL;
    }
    HALMsg msg = {.cmd=(PARAM_CHANGE|ANIMATION_FRAMES), .rid=switch_id, .len=size};
    memcpy(msg.data, buf, size);

    HALErr err = HALConn_request(conn, &msg);
    if (err != OK){
        return -EAGAIN;
    }
    return size;
}

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

    sprintf(path, "/animations/%s/loop", name);
    node = HALFS_insert(root, path);
    node->ops.mode = 0666;
    node->ops.size = 2;
    node->ops.read = anim_loop_read;
    node->ops.write = anim_loop_write;

    sprintf(path, "/animations/%s/play", name);
    node = HALFS_insert(root, path);
    node->ops.mode = 0666;
    node->ops.size = 2;
    node->ops.read = anim_play_read;
    node->ops.write = anim_play_write;

    sprintf(path, "/animations/%s/frames", name);
    node = HALFS_insert(root, path);
    node->ops.mode = 0666;
    node->ops.size = 255;
    node->ops.read = anim_frames_read;
    node->ops.write = anim_frames_write;
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
        HAL_ERROR(hal->conn, "Cannot write to arduino: %s", HALErr_desc(err));
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
                HAL_ERROR(hal->conn, "Cannot read from arduino: %s", HALErr_desc(err));
                return err;
            }
        } while (MSG_TYPE(&msg) != TREE);

        n = msg.rid;
        switch (msg.data[0]){
            case SENSOR:
                HAL_DEBUG(hal->conn, "Loading %hhu sensors", n);
                file = path + sprintf(path, "/sensors/");
                for (unsigned char i=0; i<n; i++){
                    err = HALConn_read_message(hal->conn, &msg);
                    if (err != OK){
                        HAL_ERROR(hal->conn, "Cannot read from arduino: %s",
                            HALErr_desc(err));
                        return err;
                    }
                    msg.data[msg.len] = '\0';
                    strcpy(file, (const char *) msg.data);
                    printf("INSERTING %s\n", path);
                    node = HALFS_insert(hal->root, path);
                    node->ops.mode = 0444;
                    node->ops.read = sensor_read;
                    node->ops.size = 13;
                }
                break;
            case SWITCH:
                HAL_DEBUG(hal->conn, "Loading %hhu switchs", n);
                file = path + sprintf(path, "/switchs/");
                for (unsigned char i=0; i<n; i++){
                    err = HALConn_read_message(hal->conn, &msg);
                    if (err != OK){
                        HAL_ERROR(hal->conn, "Cannot read from arduino: %s",
                            HALErr_desc(err));
                        return err;
                    }
                    msg.data[msg.len] = '\0';
                    strcpy(file, (const char *) msg.data);
                    printf("INSERTING %s\n", path);
                    node = HALFS_insert(hal->root, path);
                    node->ops.mode = 0666;
                    node->ops.write = switch_write;
                    node->ops.read = switch_read;
                    node->ops.size = 2;
                }
                break;
            case ANIMATION_FRAMES:
                HAL_DEBUG(hal->conn, "Loading %hhu animations", n);
                for (unsigned char i=0; i<n; i++){
                    err = HALConn_read_message(hal->conn, &msg);
                    if (err != OK){
                        HAL_ERROR(hal->conn, "Cannot read from arduino: %s",
                            HALErr_desc(err));
                        return err;
                    }
                    msg.data[msg.len] = '\0';
                    HAL_insert_animation(hal->root, (const char *) msg.data, msg.rid);
                }
                break;
            case TRIGGER:
                HAL_DEBUG(hal->conn, "Loading %hhu triggers", n);
                file = path + sprintf(path, "/triggers/");
                for (unsigned char i=0; i<n; i++){
                    err = HALConn_read_message(hal->conn, &msg);
                    if (err != OK){
                        HAL_ERROR(hal->conn, "Cannot read from arduino: %s",
                            HALErr_desc(err));
                        return err;
                    }
                    msg.data[msg.len] = '\0';
                    strcpy(file, (const char *) msg.data);
                    printf("INSERTING %s\n", path);
                    node = HALFS_insert(hal->root, path);
                    node->ops.mode = 0444;
                    node->ops.read = trigger_read;
                    node->ops.size = 2;
                }
                break;
        }
    }

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
    printf("Found %lu possible arduinos in /dev/\n",
        (long unsigned int) globbuf.gl_pathc);
    for (size_t i = 0; i < globbuf.gl_pathc; i++){
        printf("Trying %s\n", globbuf.gl_pathv[i]);
        conn = HALConn_open(globbuf.gl_pathv[i]);
        HALConn_loglevel(conn, DUMP);
        sleep(2);

        if (! conn){
            printf("Skip %s\n", globbuf.gl_pathv[i]);
        } else {
            res = calloc(1, sizeof(HAL));
            res->conn = conn;
            res->root = HALFS_create("/");
            if (HAL_load(res) == OK){
                printf("Connected !\n");
                HALConn_run_reader(conn);
            } else {
                free(res);
                HALConn_close(conn);
                res = NULL;
            }
        }
    }
    globfree(&globbuf);
    return res;
}

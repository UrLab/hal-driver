#ifndef DEFINE_SHAREDQUEUE_HEADER
#define DEFINE_SHAREDQUEUE_HEADER

#include <stdbool.h>
#include "HALResource.h"

bool HAL_init(struct HAL_t *hal, const char *arduino_dev);

void HAL_socket_open(struct HAL_t *hal, const char *path);

void *HAL_read_thread(void *args);

int HAL_ask_trigger(HALResource *trigger, bool *res);

int HAL_set_switch(HALResource *sw, bool on);
int HAL_ask_switch(HALResource *sw, bool *res);

int HAL_upload_anim(
    HALResource *anim, 
    unsigned char len, 
    const unsigned char *frames
);

int HAL_ask_anim_play(HALResource *anim, bool *res);
int HAL_set_anim_play(HALResource *anim, bool play);

int HAL_ask_anim_loop(HALResource *anim, bool *res);
int HAL_set_anim_loop(HALResource *anim, bool loop);

int HAL_ask_anim_delay(HALResource *anim, unsigned char *res);
int HAL_set_anim_delay(HALResource *anim, unsigned char delay);

int HAL_ask_sensor(HALResource *sensor, float *res);

size_t HAL_rx_bytes(struct HAL_t *hal);
size_t HAL_tx_bytes(struct HAL_t *hal);

#endif
#ifndef DEFINE_HAL_HEADER
#define DEFINE_HAL_HEADER

#include "com.h"
#include "HALFS.h"

typedef struct HAL {
    HALConnection *conn;
    HALFS *root;
    size_t n_triggers;
    const char **trigger_names;
} HAL;

HAL *HAL_connect();

void HAL_release(HAL *hal);

#endif

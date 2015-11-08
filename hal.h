#ifndef DEFINE_HAL_HEADER
#define DEFINE_HAL_HEADER

#include "com.h"
#include "HALFS.h"

typedef struct HAL {
    HALConnection *conn;
    HALFS *root;
} HAL;

HAL *HAL_connect();

#endif

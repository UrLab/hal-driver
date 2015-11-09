#ifndef DEFINE_LOGGER_HEADER
#define DEFINE_LOGGER_HEADER

#include "com.h"

extern int current_log_level;

#ifndef LOG_LEVEL
/* Default log level (could be overwritten with -DLOG_LEVEL=...) */
#define LOG_LEVEL INFO
#endif

typedef enum HALLogLvl {
    SILENT  = 0, //!< No logging at all
    ERROR   = 1, //!< Errors only
    WARNING = 2, //!< Errors and warnings
    INFO    = 3, //!< Errors, warnings and informations
    DEBUG   = 4, //!< Log everything
    DUMP    = 5  //!< Log everything and dump HAL serial traffic 
} HALLogLvl;

void print_log(int lvl, const char *fmt, ... );

const char *HALErr_desc(HALErr err);

void dump_message(const HALMsg *msg, const char *prefix);

#define HAL_ERROR(err,fmt,...) print_log(ERROR, "{ERROR %d: %s} "fmt, err, HALErr_desc(err), ##__VA_ARGS__)
#define HAL_WARN(fmt,...) print_log(WARNING, fmt, ##__VA_ARGS__)
#define HAL_INFO(fmt,...) print_log(INFO, fmt, ##__VA_ARGS__)
#define HAL_DEBUG(fmt,...) print_log(DEBUG, fmt, ##__VA_ARGS__)

#endif

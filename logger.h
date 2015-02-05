#ifndef DEFINE_LOGGER_HEADER
#define DEFINE_LOGGER_HEADER

extern int current_log_level;

#ifndef LOG_LEVEL
/* Default log level (could be overwritten with -DLOG_LEVEL=...) */
#define LOG_LEVEL LOGLVL_INFO
#endif

#define LOGLVL_NONE  0
#define LOGLVL_ERROR 1
#define LOGLVL_WARN  2
#define LOGLVL_INFO  3
#define LOGLVL_DEBUG 4
#define LOGLVL_COM   5 //Print all sent/received messages

void print_log(int lvl, const char *fmt, ... );

#define DEBUG(fmt, ...) print_log(LOGLVL_DEBUG, fmt, ##__VA_ARGS__)
#define INFO(fmt, ...) print_log(LOGLVL_INFO, fmt, ##__VA_ARGS__)
#define WARN(fmt, ...) print_log(LOGLVL_WARN, fmt, ##__VA_ARGS__)
#define ERROR(fmt, ...) print_log(LOGLVL_ERROR, fmt, ##__VA_ARGS__)

#endif
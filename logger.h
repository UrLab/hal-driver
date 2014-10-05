#ifndef DEFINE_LOGGER_HEADER
#define DEFINE_LOGGER_HEADER

#ifndef LOG_LEVEL
/* Default definition of the log level for whole program */
#define LOG_LEVEL LOGLVL_INFO
#endif

#define LOGLVL_ERROR 1
#define LOGLVL_WARN  2
#define LOGLVL_INFO  3
#define LOGLVL_DEBUG 4

#define DEBUG(fmt, ...) if (LOG_LEVEL >= LOGLVL_DEBUG) printf("\033[1;34m (D) " fmt "\033[0m\n", ##__VA_ARGS__)
#define INFO(fmt, ...) if (LOG_LEVEL >= LOGLVL_INFO) printf("\033[1;32m (I) " fmt "\033[0m\n", ##__VA_ARGS__)
#define WARN(fmt, ...) if (LOG_LEVEL >= LOGLVL_WARN) printf("\033[1;33m (W) " fmt "\033[0m\n", ##__VA_ARGS__)
#define ERROR(fmt, ...) if (LOG_LEVEL >= LOGLVL_ERROR) printf("\033[1;31m (E) " fmt "\033[0m\n", ##__VA_ARGS__)

#endif
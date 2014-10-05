#ifndef DEFINE_LOGGER_HEADER
#define DEFINE_LOGGER_HEADER

#define LOG_LEVEL 3

#define DEBUG(fmt, ...) if (LOG_LEVEL >= 4) printf("\033[1;34m (D) " fmt "\033[0m\n", ##__VA_ARGS__)
#define INFO(fmt, ...) if (LOG_LEVEL >= 3) printf("\033[1;32m (I) " fmt "\033[0m\n", ##__VA_ARGS__)
#define WARN(fmt, ...) if (LOG_LEVEL >= 2) printf("\033[1;33m (W) " fmt "\033[0m\n", ##__VA_ARGS__)
#define ERROR(fmt, ...) if (LOG_LEVEL >= 1) printf("\033[1;31m (E) " fmt "\033[0m\n", ##__VA_ARGS__)

#endif

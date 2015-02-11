#include "logger.h"
#include <stdarg.h>
#include <stdio.h>
#include <time.h>

int current_log_level = LOG_LEVEL;

int level_color[] = {0, 1, 3, 2, 4, 5};
char level_prefix[] = " EWIDC";

void print_log(int lvl, const char *fmt, ... )
{
    if (LOGLVL_ERROR <= lvl && lvl <= current_log_level){
        va_list args;
        va_start(args, fmt);

        time_t now_seconds = time(NULL);
        struct tm *now = localtime(&now_seconds);

        printf("\033[1;3%dm(%c) %02d-%02d-%02d %02d:%02d:%02d\033[0m ", 
               level_color[lvl], level_prefix[lvl],
               now->tm_year + 1900, now->tm_mon + 1, now->tm_mday,
               now->tm_hour, now->tm_min, now->tm_sec);
        vprintf(fmt, args);
        printf("\n");
    }
}

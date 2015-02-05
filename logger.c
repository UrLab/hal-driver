#include "logger.h"
#include <stdarg.h>
#include <stdio.h>

int current_log_level = LOG_LEVEL;

int level_color[] = {0, 1, 3, 2, 4, 5};
char level_prefix[] = " EWIDC";

void print_log(int lvl, const char *fmt, ... )
{
    if (LOGLVL_ERROR <= lvl && lvl <= current_log_level){
        va_list args;
        va_start(args, fmt);
        printf("\033[1;3%dm(%c)\033[0m ", level_color[lvl], level_prefix[lvl]);
        vprintf(fmt, args);
        printf("\n");
    }
}

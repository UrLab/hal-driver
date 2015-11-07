#include "logger.h"
#include <stdarg.h>
#include <stdio.h>
#include <time.h>

int current_log_level = LOG_LEVEL;

char level_prefix[] = " EWIDC";
int level_color[] = {0, 1, 3, 2, 4, 5};

const char *HALErr_desc(HALErr err)
{
    switch (err){
        case OK:        return "No error";
        case TIMEOUT:   return "Command timeouted";
        case SEQERR:    return "No more seq no avialableat the moment";
        case LOCKERR:   return "Cannot acquire lock on connection";
        case CHKERR:    return "Checksum error";
        case READERR:   return "Cannot read";
        case WRITEERR:  return "Cannot write";
        case OUTOFSYNC: return "Encountered unexpected SYNC byte; must resync";
        default: return "Unknown error";
    }
}

void print_log(int lvl, const char *fmt, ... )
{
    if (current_log_level >= lvl){
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
        fflush(stdout);
    }
}

void dump_message(const HALMsg *msg, const char *prefix){
    if (current_log_level >= DUMP){
        if (prefix){
            fprintf(stderr, "%s", prefix);
        }

        fprintf(stderr, 
            "#%c%-3hhu: command=%02hhx, type=%c%c, rid=%hhu, len=%hhu chk=%d\n", 
            (IS_ARDUINO_SEQ(msg->seq) ? 'A' : 'D'),
            ABSOLUTE_SEQ(msg->seq),
            msg->cmd,
            MSG_TYPE(msg), MSG_IS_CHANGE(msg) ? '!' : '?',
            msg->rid,
            msg->len,
            msg->chk);
        
        for (int i=0; i<16 && 16*i<msg->len; i++){
            for (int j=0; j<16 && 16*i+j < msg->len; j++){
                fprintf(stderr, "  %02hhx", msg->data[16*i+j]);
            }
            fprintf(stderr, "\n");
        }
    }
}

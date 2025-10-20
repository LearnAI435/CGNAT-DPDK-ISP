/* SPDX-License-Identifier: MIT */
/**
 * @file logger.c
 * @brief High-performance structured logging
 */

#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>

typedef enum {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
    LOG_FATAL
} log_level_t;

static const char *level_strings[] = {
    "DEBUG", "INFO", "WARN", "ERROR", "FATAL"
};

void
cgnat_log(log_level_t level, const char *format, ...)
{
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char timestamp[64];
    
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
    
    printf("[%s] [%s] ", timestamp, level_strings[level]);
    
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    
    printf("\n");
}

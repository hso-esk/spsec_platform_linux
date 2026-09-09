/*
 * Copyright (c) 2026
 *
 * Hochschule Offenburg, University of Applied Sciences
 * Institute for reliable Embedded Systems
 * and Communications Electronic (ivESK)
 *
 * This file is licensed as described in the "LICENSE" file
 * included within the root folder of this work.
 */

/**
 * @file logging.c
 * @brief LPC55S16 baremetal logging via UART/RTT.
 */

#include "logging.h"
#include "spsec_common.h"
#include "fsl_debug_console.h"
#include "fsl_uart.h"

static const char *logger_name = "log_lpc55";
static log_level_t current_level = LOG_LEVEL_DEBUG;
static char time_buf[32];

void logging_init(log_level_t level)
{
    current_level = level;
    /* Debug console init typically done in BOARD_InitDebugConsole() */
    LOG_INFO(logger_name, "Logging initialized at level %d", level);
}

void logging_deinit(void)
{
}

void logging_set_level(log_level_t level)
{
    current_level = level;
}

log_level_t logging_get_level(void)
{
    return current_level;
}

void logging_log(log_level_t level, const char *logger, const char *fmt, ...)
{
    if (level < current_level) return;

    va_list args;
    va_start(args, fmt);

    /* Format timestamp */
    platform_time_format(time_buf, sizeof(time_buf), log_level_str(level), logger);

    /* Print timestamp + formatted message */
    PRINTF("%s", time_buf);
    vprintf(fmt, args);
    PRINTF("\r\n");

    va_end(args);
}

const char *log_level_str(log_level_t level)
{
    switch (level) {
        case LOG_LEVEL_DEBUG:    return "DEBUG";
        case LOG_LEVEL_INFO:     return "INFO";
        case LOG_LEVEL_WARNING:  return "WARNING";
        case LOG_LEVEL_ERROR:    return "ERROR";
        case LOG_LEVEL_CRITICAL: return "CRITICAL";
        default:                 return "UNKNOWN";
    }
}
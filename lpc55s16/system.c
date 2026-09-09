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
 * @file system.c
 * @brief LPC55S16 baremetal system control (stop callback).
 */

#include "system.h"
#include "spsec_common.h"
#include "fsl_gpio.h"

static const char *logger_name = "sys_lpc55";

/* Stop flag set by GPIO interrupt or WDT */
static volatile bool stop_requested = false;

void system_request_stop(void *ctx)
{
    (void)ctx;
    stop_requested = true;
    LOG_INFO(logger_name, "Stop requested");
}

bool system_is_stop_requested(void *ctx)
{
    (void)ctx;
    return stop_requested;
}

void system_clear_stop(void *ctx)
{
    (void)ctx;
    stop_requested = false;
}
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
 * @file timer.c
 * @brief LPC55S16 baremetal FreeRunningTimer using DWT cycle counter.
 */

#include "timer.h"
#include "nvol_storage.h"
#include "spsec_common.h"
#include "fsl_dwt.h"

/* Max allowable backward clock jump (1 second) to prevent rollback attacks. */
#define TIMER_MAX_BACKWARD_NS 1000000000ULL /* 1 second */

/* Forward jump applied to persistent timestamp on reboot to ensure monotonicity. */
#define TIMER_REBOOT_MARGIN_TICKS (1000000000ULL / 12500ULL) /* 80000 */

static uint64_t timer_max_backward_ticks(uint32_t tick_ns)
{
    return TIMER_MAX_BACKWARD_NS / (uint64_t)tick_ns;
}

static const char *logger_name = "timer_lpc55";

/* System core clock frequency (set by SystemCoreClockUpdate) */
extern uint32_t SystemCoreClock;

signed char timer_init(FreeRunningTimer *timer, uint8_t seconds_symbol_index)
{
    if (!timer) return -1;

    timer->seconds_symbol_index = seconds_symbol_index;
    timer->tick_ns = 100000; /* default 100us/tick; see timer_set_tick_ns() */

    /* Initialize DWT cycle counter */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    timer->start_cycles64 = 0;
    timer->cycle_high = 0;
    timer->last_cyccnt = 0;

    /* Load or generate initial timestamp */
    uint64_t persistent_timestamp = 0;
    if (nvol_storage_read_u64("config/timestamp", &persistent_timestamp) == 0) {
        persistent_timestamp += TIMER_REBOOT_MARGIN_TICKS;
        LOG_INFO(logger_name, "Loaded persistent timestamp, incremented");
    } else {
        /* Generate random non-zero timestamp */
        uint8_t rng_bytes[8];
        if (random_generator_get_bytes(NULL, rng_bytes, 8)) {
            for (int i = 0; i < 8; i++) {
                persistent_timestamp |= ((uint64_t)rng_bytes[i]) << (i * 8);
            }
        } else {
            persistent_timestamp = (uint64_t)DWT->CYCCNT;
        }
        if (persistent_timestamp == 0) persistent_timestamp = 1;
        LOG_INFO(logger_name, "Generated new random timestamp");
    }

    timer->tick_base = persistent_timestamp;

    /* Initialize timestamp */
    for (int i = 0; i < 8; i++) {
        timer->timestamp[i] = (uint8_t)((persistent_timestamp >> (i * 8)) & 0xFF);
    }

    return 0;
}

void timer_destroy(FreeRunningTimer *timer)
{
    (void)timer;
    DWT->CTRL &= ~DWT_CTRL_CYCCNTENA_Msk;
}

/* Single-threaded baremetal target: no lock needed, unlike platform/timer.c's
 * pthread-based Linux implementation. */
void timer_set_tick_ns(FreeRunningTimer *timer, uint32_t tick_ns)
{
    if (!timer || tick_ns == 0) return;
    timer->tick_ns = tick_ns;
}

uint32_t timer_get_tick_ns(FreeRunningTimer *timer)
{
    if (!timer) return 100000;
    return timer->tick_ns;
}

void timer_get_timestamp(FreeRunningTimer *timer, uint8_t *timestamp)
{
    if (!timer || !timestamp) return;

    /* Update timestamp from cycle counter */
    uint32_t cyccnt = DWT->CYCCNT;
    if (cyccnt < timer->last_cyccnt) {
        timer->cycle_high += (1ULL << 32);
    }
    timer->last_cyccnt = cyccnt;

    uint64_t total_cycles = timer->cycle_high | cyccnt;
    uint64_t elapsed_cycles = total_cycles - timer->start_cycles64;
    uint64_t elapsed_us = elapsed_cycles / (SystemCoreClock / 1000000ULL);

    /* Apply speed scaling */
    if (timer->seconds_symbol_index != 8) {
        if (timer->seconds_symbol_index < 8) {
            uint8_t steps = 8 - timer->seconds_symbol_index;
            uint64_t scale = 1;
            for (uint8_t i = 0; i < steps; i++) scale *= 16;
            elapsed_us *= scale;
        } else {
            uint8_t steps = timer->seconds_symbol_index - 8;
            for (uint8_t i = 0; i < steps; i++) elapsed_us /= 16;
        }
    }

    uint64_t elapsed_ticks = (elapsed_us * 1000ULL) / (uint64_t)timer->tick_ns;
    uint64_t ticks = timer->tick_base + elapsed_ticks;

    for (int i = 0; i < 8; i++) {
        timestamp[i] = (uint8_t)((ticks >> (i * 8)) & 0xFF);
        timer->timestamp[i] = timestamp[i];
    }
}

signed char timer_set_timestamp(FreeRunningTimer *timer, uint8_t *timestamp)
{
    if (!timer || !timestamp) return -1;

    uint64_t ticks = 0;
    for (int i = 0; i < 8; i++) {
        ticks |= ((uint64_t)timestamp[i]) << (i * 8);
    }

    /* Backward jump protection after first sync */
    static bool synced = false;
    if (synced && timer->tick_base > 0) {
        uint64_t current = 0;
        for (int i = 0; i < 8; i++) {
            current |= ((uint64_t)timer->timestamp[i]) << (i * 8);
        }
        if (ticks + timer_max_backward_ticks(timer->tick_ns) < current) {
            LOG_WARNING(logger_name, "Rejecting backward clock jump");
            return -1;
        }
    }

    /* Update base and restart cycle counting */
    timer->tick_base = ticks;
    timer->start_cycles64 = timer->cycle_high | DWT->CYCCNT;
    timer->last_cyccnt = DWT->CYCCNT;

    memcpy(timer->timestamp, timestamp, 8);
    synced = true;

    /* Persist */
    nvol_storage_write_u64("config/timestamp", ticks);

    return 0;
}

uint64_t timer_get_difference(FreeRunningTimer *timer, uint8_t *timestamp)
{
    uint8_t current[8];
    timer_get_timestamp(timer, current);

    uint64_t t_in = 0, t_cur = 0;
    for (int i = 0; i < 8; i++) {
        t_in  |= ((uint64_t)timestamp[i]) << (i * 8);
        t_cur |= ((uint64_t)current[i]) << (i * 8);
    }
    return (t_cur - t_in) * (uint64_t)timer_get_tick_ns(timer) / 1000ULL;  /* microseconds */
}

uint64_t timer_get_current_time_us(FreeRunningTimer *timer)
{
    uint8_t ts[8];
    timer_get_timestamp(timer, ts);
    uint64_t ticks = 0;
    for (int i = 0; i < 8; i++) {
        ticks |= ((uint64_t)ts[i]) << (i * 8);
    }
    return ticks * (uint64_t)timer_get_tick_ns(timer) / 1000ULL;
}

void platform_time_format(char *buf, size_t buf_size, const char *level,
                          const char *logger)
{
    if (!buf || buf_size == 0) return;
    uint8_t ts[8];
    /* Log prefixes use a standalone 100us reference timer */
    FreeRunningTimer dummy = {0};
    dummy.tick_ns = 100000;
    timer_get_timestamp(&dummy, ts);
    uint64_t ticks = 0;
    for (int i = 0; i < 8; i++) ticks |= ((uint64_t)ts[i]) << (i * 8);
    const uint64_t ticks_per_sec = 1000000000ULL / dummy.tick_ns;
    const uint64_t ticks_per_ms  = ticks_per_sec / 1000ULL;
    uint32_t sec = (uint32_t)(ticks / ticks_per_sec);
    uint32_t ms  = (uint32_t)((ticks % ticks_per_sec) / ticks_per_ms);
    snprintf(buf, buf_size, "%u.%03u - %s - %s - ", sec, ms, level ? level : "", logger ? logger : "");
}
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

#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <time.h>
#include <unistd.h>

#include "timer.h"
#include "nvol_storage.h"
#include "spsec_common.h"
#include <pthread.h>
#include <sys/random.h>
#include <sys/types.h>

// Largest backward clock adjustment accepted from a time-sync broadcast (1 second).
#define TIMER_MAX_BACKWARD_NS 1000000000ULL // 1 second

// Backward-jump rejection bound, in ticks at the configured resolution.
static uint64_t timer_max_backward_ticks(uint32_t tick_ns) {
  return TIMER_MAX_BACKWARD_NS / (uint64_t)tick_ns;
}

// Forward jump applied to persistent timestamp_ptr on reboot to ensure monotonicity.
#define TIMER_REBOOT_MARGIN_TICKS (1000000000ULL / 12500ULL) // 80000

static const char *logger_name_ptr = "timer";

// Timestamp is a 64-bit tick counter, 1 tick = 0.1ms per SPsec302 2.11,
// serialized little-endian into 8 bytes.

// Background thread: converts monotonic time to scaled ticks under a mutex.
static void *timer_thread(void *arg_ptr) {
  FreeRunningTimer *timer_ptr = (FreeRunningTimer *)arg_ptr;

  while (timer_ptr->running) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    pthread_mutex_lock(&timer_ptr->lock);
    uint64_t diff_ns =
        (uint64_t)(now.tv_sec - timer_ptr->start_mono.tv_sec) * 1000000000ULL +
        (uint64_t)(now.tv_nsec - timer_ptr->start_mono.tv_nsec);
    uint64_t diff_us = diff_ns / 1000ULL;

    // Apply speed scaling based on seconds_symbol_index
    // index 8 → 1x (neutral)
    if (timer_ptr->seconds_symbol_index != 8) {
      if (timer_ptr->seconds_symbol_index < 8) {
        // Speed up: each step is ×16
        uint8_t steps = (uint8_t)(8 - timer_ptr->seconds_symbol_index);
        uint64_t scale = 1ULL;
        for (uint8_t i = 0; i < steps; i++)
          scale *= 16ULL;
        diff_us *= scale;
      } else {
        // Slow down: each step is ÷16
        uint8_t steps = (uint8_t)(timer_ptr->seconds_symbol_index - 8);
        for (uint8_t i = 0; i < steps; i++)
          diff_us /= 16ULL;
      }
    }

    // Convert elapsed time to ticks and add to base
    uint64_t elapsed_ticks = (diff_us * 1000ULL) / (uint64_t)timer_ptr->tick_ns;
    uint64_t ticks = timer_ptr->tick_base + elapsed_ticks;

    // Serialize little-endian 64-bit tick counter
    for (int i = 0; i < 8; i++) {
      timer_ptr->timestamp[i] = (uint8_t)((ticks >> (i * 8)) & 0xFF);
    }

    pthread_mutex_unlock(&timer_ptr->lock);
    struct timespec ts = {0, 1000000}; // 1ms
    nanosleep(&ts, NULL);
  }
  return NULL;
}

// Start the free-running timer and its update thread. seconds_symbol_index
// is the SPsec speed-scaling index (8 = 1x).
signed char timer_init(FreeRunningTimer *timer_ptr, uint8_t seconds_symbol_index) {
  timer_ptr->running = 1;
  timer_ptr->synced = 0;
  timer_ptr->thread_started = 0;
  timer_ptr->tick_ns = 100000; // default 100us/tick (SPsec302 2.11)
  pthread_mutex_init(&timer_ptr->lock, NULL);
  clock_gettime(CLOCK_MONOTONIC, &timer_ptr->start_mono);
  timer_ptr->seconds_symbol_index = seconds_symbol_index;

  // SPsec201 V34 2.12.1: timestamp must be unique across reboots.
  uint64_t persistent_timestamp = 0;
  if (nvol_storage_read_u64("config/timestamp_ptr",
                            &persistent_timestamp) == 0) {
    // Bump past the reboot margin so we never reuse a prior timestamp.
    persistent_timestamp += TIMER_REBOOT_MARGIN_TICKS;
    LOG_INFO(logger_name_ptr,
             "Loaded persistent timestamp_ptr from storage, incremented by %llu ticks",
             (unsigned long long)TIMER_REBOOT_MARGIN_TICKS);
  } else {
    // No stored timestamp: seed non-zero via CSPRNG (SPsec302 V40 2.11 bans
    // a zero start value). getrandom() over rand()/time() because this seeds
    // the AEAD nonce's high bytes and must not be predictable from boot time.
    ssize_t rnd =
        getrandom(&persistent_timestamp, sizeof(persistent_timestamp), 0);
    if (rnd != (ssize_t)sizeof(persistent_timestamp)) {
      LOG_WARNING(logger_name_ptr,
                  "getrandom() failed; falling back to monotonic clock seed");
      struct timespec seed_ts;
      clock_gettime(CLOCK_MONOTONIC, &seed_ts);
      persistent_timestamp =
          (uint64_t)seed_ts.tv_nsec ^ ((uint64_t)seed_ts.tv_sec << 20);
    }
    if (persistent_timestamp == 0)
      persistent_timestamp = 1; // ensure non-zero
    LOG_INFO(logger_name_ptr,
             "Initialized new random timestamp_ptr (not from storage)");
  }

  timer_ptr->tick_base = persistent_timestamp;

  // Initialize timestamp_ptr immediately
  pthread_mutex_lock(&timer_ptr->lock);
  uint64_t ticks = timer_ptr->tick_base;
  for (int i = 0; i < 8; i++)
    timer_ptr->timestamp[i] = (uint8_t)((ticks >> (i * 8)) & 0xFF);
  pthread_mutex_unlock(&timer_ptr->lock);

  if (pthread_create(&timer_ptr->thread, NULL, timer_thread, timer_ptr) != 0) {
    LOG_ERROR(logger_name_ptr, "Failed to create timer_ptr thread");
    return -1;
  }
  timer_ptr->thread_started = 1;
  return 0;
}

// Set tick resolution in ns; see timer.h for ordering/rescale caveats.
void timer_set_tick_ns(FreeRunningTimer *timer_ptr, uint32_t tick_ns) {
  if (!timer_ptr || tick_ns == 0)
    return;
  pthread_mutex_lock(&timer_ptr->lock);
  if (timer_ptr->tick_ns != tick_ns) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    uint64_t diff_ns =
        (uint64_t)(now.tv_sec - timer_ptr->start_mono.tv_sec) * 1000000000ULL +
        (uint64_t)(now.tv_nsec - timer_ptr->start_mono.tv_nsec);
    uint64_t diff_us = diff_ns / 1000ULL;

    if (timer_ptr->seconds_symbol_index != 8) {
      if (timer_ptr->seconds_symbol_index < 8) {
        uint8_t steps = (uint8_t)(8 - timer_ptr->seconds_symbol_index);
        uint64_t scale = 1ULL;
        for (uint8_t i = 0; i < steps; i++)
          scale *= 16ULL;
        diff_us *= scale;
      } else {
        uint8_t steps = (uint8_t)(timer_ptr->seconds_symbol_index - 8);
        for (uint8_t i = 0; i < steps; i++)
          diff_us /= 16ULL;
      }
    }

    uint64_t elapsed_ticks = (diff_us * 1000ULL) / (uint64_t)timer_ptr->tick_ns;
    timer_ptr->tick_base += elapsed_ticks;
    timer_ptr->start_mono = now;
    timer_ptr->tick_ns = tick_ns;
  }
  pthread_mutex_unlock(&timer_ptr->lock);
}

// Get the current tick resolution in nanoseconds.
uint32_t timer_get_tick_ns(FreeRunningTimer *timer_ptr) {
  pthread_mutex_lock(&timer_ptr->lock);
  uint32_t tick_ns = timer_ptr->tick_ns;
  pthread_mutex_unlock(&timer_ptr->lock);
  return tick_ns;
}

// Stop the timer thread and release resources.
void timer_destroy(FreeRunningTimer *timer_ptr) {
  timer_ptr->running = 0;
  if (timer_ptr->thread_started)
    pthread_join(timer_ptr->thread, NULL);
  pthread_mutex_destroy(&timer_ptr->lock);
}

// Read the current timestamp (8-byte little-endian tick count).
void timer_get_timestamp(FreeRunningTimer *timer_ptr, uint8_t *timestamp_ptr) {
  pthread_mutex_lock(&timer_ptr->lock);
  memcpy(timestamp_ptr, timer_ptr->timestamp, 8);
  pthread_mutex_unlock(&timer_ptr->lock);
}

// Set the timer to a specific timestamp and reset the monotonic baseline.
signed char timer_set_timestamp(FreeRunningTimer *timer_ptr,
                                uint8_t *timestamp_ptr) {
  if (!timestamp_ptr) {
    LOG_ERROR(logger_name_ptr, "Invalid timestamp_ptr: NULL pointer");
    return -1;
  }

  // Parse little-endian 64-bit ticks
  uint64_t ticks = 0;
  for (int i = 0; i < 8; i++) {
    ticks |= ((uint64_t)timestamp_ptr[i]) << (i * 8);
  }

  pthread_mutex_lock(&timer_ptr->lock);
  // Once synced, reject large backward jumps: replaying an old sync broadcast
  // could re-enter a spent nonce epoch. The first set is always accepted, since
  // it just replaces the arbitrary random init epoch.
  if (timer_ptr->synced) {
    uint64_t current = 0;
    for (int i = 0; i < 8; i++) {
      current |= ((uint64_t)timer_ptr->timestamp[i]) << (i * 8);
    }
    // tick_ns read under the currently held lock.
    uint64_t max_backward = timer_max_backward_ticks(timer_ptr->tick_ns);
    if (ticks + max_backward < current) {
      pthread_mutex_unlock(&timer_ptr->lock);
      LOG_WARNING(logger_name_ptr,
                  "Rejecting backward clock jump (current=%llu new=%llu "
                  "max_backward=%llu ticks)",
                  (unsigned long long)current, (unsigned long long)ticks,
                  (unsigned long long)max_backward);
      return -1;
    }
  }
  memcpy(timer_ptr->timestamp, timestamp_ptr, 8);
  clock_gettime(CLOCK_MONOTONIC, &timer_ptr->start_mono);
  timer_ptr->tick_base = ticks;
  timer_ptr->synced = 1;
  pthread_mutex_unlock(&timer_ptr->lock);

  LOG_INFO_ARRAY(logger_name_ptr, "Setting timestamp_ptr to:", timestamp_ptr, 8);
  return 0;
}

// Microseconds elapsed since timestamp_ptr (wrap-safe).
uint64_t timer_get_difference(FreeRunningTimer *timer_ptr, uint8_t *timestamp_ptr) {
  uint8_t current[8];
  timer_get_timestamp(timer_ptr, current);

  uint64_t t_in = 0, t_cur = 0;
  for (int i = 0; i < 8; i++) {
    t_in |= ((uint64_t)timestamp_ptr[i]) << (i * 8);
    t_cur |= ((uint64_t)current[i]) << (i * 8);
  }

  // Unsigned 64-bit wrap-safe difference in ticks
  uint64_t delta_ticks = t_cur - t_in;
  return delta_ticks * (uint64_t)timer_get_tick_ns(timer_ptr) / 1000ULL; // microseconds
}

// Current timer value in microseconds.
uint64_t timer_get_current_time_us(FreeRunningTimer *timer_ptr) {
  uint8_t timestamp_ptr[8];
  timer_get_timestamp(timer_ptr, timestamp_ptr);

  uint64_t ticks = 0;
  for (int i = 0; i < 8; i++) {
    ticks |= ((uint64_t)timestamp_ptr[i]) << (i * 8);
  }
  return ticks * (uint64_t)timer_get_tick_ns(timer_ptr) / 1000ULL;
}

// Format the current wall-clock time for log prefixes.
void platform_time_format(char *buf_ptr, size_t buf_size, const char *level_ptr,
                          const char *logger_ptr) {
  if (!buf_ptr || buf_size == 0)
    return;
  struct timespec now_ts;
  clock_gettime(CLOCK_REALTIME, &now_ts);
  struct tm t;
  localtime_r(&now_ts.tv_sec, &t);
  char ts[16];
  ts[0] = '\0';
  if (strftime(ts, sizeof(ts), "%H:%M:%S", &t) == 0)
    ts[0] = '\0';
  long us = (long)(now_ts.tv_nsec / 1000L);
  // Build: "HH:MM:SS.uuuuuu - LEVEL - LOGGER - " (microsecond precision)
  // Use snprintf to ensure no overflow and always terminate
  if (!level_ptr)
    level_ptr = "";
  if (!logger_ptr)
    logger_ptr = "";
  snprintf(buf_ptr, buf_size, "%s.%06ld - %s - %s - ", ts, us, level_ptr, logger_ptr);
}
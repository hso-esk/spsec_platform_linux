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
 * Linux/PC log sink: writes formatted lines to stdout or a rotating file.
 * The only place touching filesystem/stdio for logging - common/spsec_common.c
 * just formats and calls platform_log_write(). Port by swapping this file
 * for one that writes to a UART.
 */

#include "logging.h"

#include <stdio.h>
#include <sys/stat.h>

static FILE *s_log_file_ptr = NULL;        // NULL => write to stdout
static const char *s_log_path_ptr = NULL;  // path for rotation (not owned)
static size_t s_max_size = 0;
static int s_max_rotated = 0;
static int s_rotation_enabled = 0;

static FILE *current_stream(void) { return s_log_file_ptr ? s_log_file_ptr : stdout; }

void platform_log_write(const char *data_ptr, size_t len) {
  if (!data_ptr || len == 0)
    return;
  fwrite(data_ptr, 1, len, current_stream());
}

static void rotate_if_needed(void) {
  if (!s_rotation_enabled || !s_log_path_ptr)
    return;

  struct stat st;
  if (stat(s_log_path_ptr, &st) != 0)
    return;
  if ((size_t)st.st_size < s_max_size)
    return;

  if (s_log_file_ptr) {
    fclose(s_log_file_ptr);
    s_log_file_ptr = NULL;
  }

  if (s_max_rotated > 1) {
    unsigned int limit = (unsigned int)s_max_rotated;
    for (unsigned int i = limit - 1; i >= 1; i--) {
      char old_name[512];
      char new_name[512];
      snprintf(old_name, sizeof(old_name), "%s.%u", s_log_path_ptr, i);
      snprintf(new_name, sizeof(new_name), "%s.%u", s_log_path_ptr, i + 1U);
      rename(old_name, new_name);
    }
  }

  char rotated_name[512];
  snprintf(rotated_name, sizeof(rotated_name), "%s.1", s_log_path_ptr);
  rename(s_log_path_ptr, rotated_name);

  s_log_file_ptr = fopen(s_log_path_ptr, "a");
}

void platform_log_flush(void) {
  fflush(current_stream());
  rotate_if_needed();
}

void platform_log_configure_file(const char *path_ptr, size_t max_size,
                                 int max_rotated) {
  if (s_log_file_ptr) {
    fclose(s_log_file_ptr);
    s_log_file_ptr = NULL;
  }
  if (!path_ptr) {
    // Fall back to stdout.
    s_log_path_ptr = NULL;
    s_rotation_enabled = 0;
    return;
  }
  s_log_path_ptr = path_ptr;
  s_max_size = max_size;
  s_max_rotated = max_rotated;
  s_rotation_enabled = (max_size > 0);
  s_log_file_ptr = fopen(path_ptr, "a");
  // On failure s_log_file_ptr stays NULL and output falls back to stdout.
}

void platform_log_cleanup(void) {
  if (s_log_file_ptr) {
    fclose(s_log_file_ptr);
    s_log_file_ptr = NULL;
  }
  s_log_path_ptr = NULL;
  s_rotation_enabled = 0;
}

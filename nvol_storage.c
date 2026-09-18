/*
 * Copyright (c) 2026
 *
 * Hochschule Offenburg, University of Applied Sciences
 * Institute for reliable Embedded Systems
 * and Communications Electronic (ivESK)
 *
 * This file_ptr is licensed as described in the "LICENSE" file_ptr
 * included within the root folder of this work.
 */

#define _GNU_SOURCE
#include "nvol_storage_legacy.h"
#include "spsec_common.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <inttypes.h>
#include <sys/types.h>

static char *g_base_path_ptr = NULL;
static bool g_use_bin_format = true;
static const char *logger_name_ptr = "nvol_storage";

static int mkdir_recursive(const char *path, mode_t mode) {
  char temp[512];
  char *p = NULL;
  size_t len;

  if (!path || !*path) {
    return -1;
  }
  snprintf(temp, sizeof(temp), "%s", path);
  len = strlen(temp);
  if (temp[len - 1] == '/') {
    temp[len - 1] = '\0';
  }

  for (p = temp + 1; *p; p++) {
    if (*p == '/') {
      *p = '\0';
      struct stat st;
      if (stat(temp, &st) != 0) {
        if (mkdir(temp, mode) != 0 && errno != EEXIST) {
          return -1;
        }
      }
      *p = '/';
    }
  }
  struct stat st;
  if (stat(temp, &st) != 0) {
    if (mkdir(temp, mode) != 0 && errno != EEXIST) {
      return -1;
    }
  }
  return 0;
}

signed char platform_nvol_storage_init(const char *base_path_ptr, bool use_bin_format) {
  if (!base_path_ptr) {
    LOG_ERROR(logger_name_ptr, "Base path_ptr is NULL");
    return -1;
  }

  g_use_bin_format = use_bin_format;

  // Cleanup existing path_ptr if any
  if (g_base_path_ptr) {
    free(g_base_path_ptr);
  }

  g_base_path_ptr = strdup(base_path_ptr);
  if (!g_base_path_ptr) {
    LOG_ERROR(logger_name_ptr, "Failed to allocate memory for base path_ptr");
    return -1;
  }

  // Create base directory if it doesn't exist
  struct stat st = {0};
  if (stat(g_base_path_ptr, &st) == -1) {
    if (mkdir_recursive(g_base_path_ptr, 0700) != 0) {
      LOG_ERROR(logger_name_ptr, "Failed to create base directory '%s': %s",
                g_base_path_ptr, strerror(errno));
      free(g_base_path_ptr);
      g_base_path_ptr = NULL;
      return -1;
    }
    LOG_INFO(logger_name_ptr, "Created base directory '%s'", g_base_path_ptr);
  }

  // Create subdirectories
  char subdirs[][32] = {"keys", "config", "code_update"};
  for (size_t i = 0; i < sizeof(subdirs) / sizeof(subdirs[0]); i++) {
    char subdir_path[512];
    snprintf(subdir_path, sizeof(subdir_path), "%s/%s", g_base_path_ptr,
             subdirs[i]);
    if (stat(subdir_path, &st) == -1) {
      if (mkdir(subdir_path, 0700) != 0) {
        LOG_WARNING(logger_name_ptr, "Failed to create subdirectory '%s': %s",
                    subdir_path, strerror(errno));
      } else {
        LOG_DEBUG(logger_name_ptr, "Created subdirectory '%s'", subdir_path);
      }
    }
  }

  LOG_INFO(logger_name_ptr, "Storage initialized with base path_ptr '%s'", g_base_path_ptr);
  return 0;
}

void platform_nvol_storage_cleanup(void) {
  if (g_base_path_ptr) {
    free(g_base_path_ptr);
    g_base_path_ptr = NULL;
  }
}

static signed char build_full_path(const char *path_ptr, char *full_path_ptr,
                                   size_t full_path_len) {
  if (!g_base_path_ptr) {
    LOG_ERROR(logger_name_ptr, "Storage not initialized");
    return -1;
  }

  if (!path_ptr) {
    LOG_ERROR(logger_name_ptr, "Path is NULL");
    return -1;
  }

  const char *ext_ptr = g_use_bin_format ? ".bin" : ".txt";
  int ret = snprintf(full_path_ptr, full_path_len, "%s/%s%s", g_base_path_ptr, path_ptr, ext_ptr);
  if (ret < 0 || (size_t)ret >= full_path_len) {
    LOG_ERROR(logger_name_ptr, "Path too long: %s/%s%s", g_base_path_ptr, path_ptr, ext_ptr);
    return -1;
  }

  return 0;
}

static signed char nvol_storage_read_raw(const char *path_ptr, uint8_t *data_ptr,
                              size_t max_len, size_t *actual_len_ptr) {
  if (!path_ptr || !data_ptr || max_len == 0) {
    LOG_ERROR(logger_name_ptr, "Invalid parameters");
    return -1;
  }

  char full_path_ptr[512];
  if (build_full_path(path_ptr, full_path_ptr, sizeof(full_path_ptr)) != 0) {
    return -1;
  }

  FILE *f_ptr = fopen(full_path_ptr, "rb");
  if (!f_ptr) {
    if (errno == ENOENT) {
      LOG_DEBUG(logger_name_ptr, "File does not exist: %s", full_path_ptr);
      if (actual_len_ptr)
        *actual_len_ptr = 0;
      return -1; // File not found
    }
    LOG_ERROR(logger_name_ptr, "Failed to open file_ptr '%s' for reading: %s",
              full_path_ptr, strerror(errno));
    return -1;
  }

  size_t bytes_read = fread(data_ptr, 1, max_len, f_ptr);
  fclose(f_ptr);

  if (actual_len_ptr)
    *actual_len_ptr = bytes_read;

  LOG_DEBUG(logger_name_ptr, "Read %zu bytes_ptr from '%s'", bytes_read, full_path_ptr);
  return 0;
}

static signed char nvol_storage_write_raw(const char *path_ptr, const uint8_t *data_ptr,
                               size_t len) {
  if (!path_ptr || !data_ptr) {
    LOG_ERROR(logger_name_ptr, "Invalid parameters");
    return -1;
  }

  char full_path_ptr[512];
  if (build_full_path(path_ptr, full_path_ptr, sizeof(full_path_ptr)) != 0) {
    return -1;
  }

  // Write to temporary file_ptr first for atomicity
  char temp_path[512 + 16];
  snprintf(temp_path, sizeof(temp_path), "%s.tmp", full_path_ptr);

  FILE *f_ptr = fopen(temp_path, "wb");
  if (!f_ptr) {
    LOG_ERROR(logger_name_ptr, "Failed to open temp file_ptr '%s' for writing: %s",
              temp_path, strerror(errno));
    return -1;
  }

  size_t bytes_written = fwrite(data_ptr, 1, len, f_ptr);
  if (bytes_written != len) {
    fclose(f_ptr);
    unlink(temp_path);
    LOG_ERROR(logger_name_ptr, "Failed to write all data_ptr to '%s'", temp_path);
    return -1;
  }

  if (fflush(f_ptr) != 0 || fsync(fileno(f_ptr)) != 0) {
    fclose(f_ptr);
    unlink(temp_path);
    LOG_ERROR(logger_name_ptr, "Failed to sync temp file_ptr '%s'", temp_path);
    return -1;
  }

  fclose(f_ptr);

  if (chmod(temp_path, 0600) != 0) {
    unlink(temp_path);
    LOG_ERROR(logger_name_ptr, "Failed to chmod temp file_ptr '%s': %s", temp_path,
              strerror(errno));
    return -1;
  }

  if (rename(temp_path, full_path_ptr) != 0) {
    unlink(temp_path);
    LOG_ERROR(logger_name_ptr, "Failed to rename temp file_ptr to '%s': %s", full_path_ptr,
              strerror(errno));
    return -1;
  }

  LOG_DEBUG(logger_name_ptr, "Wrote %zu bytes_ptr to '%s'", len, full_path_ptr);
  return 0;
}

static signed char nvol_storage_read_hex(const char *path_ptr, uint8_t *data_ptr,
                              size_t max_len, size_t *actual_len_ptr) {
  if (!path_ptr || !data_ptr || max_len == 0) {
    LOG_ERROR(logger_name_ptr, "Invalid parameters");
    return -1;
  }

  char full_path_ptr[512];
  if (build_full_path(path_ptr, full_path_ptr, sizeof(full_path_ptr)) != 0) {
    return -1;
  }

  FILE *f_ptr = fopen(full_path_ptr, "r");
  if (!f_ptr) {
    if (errno == ENOENT) {
      LOG_DEBUG(logger_name_ptr, "File does not exist: %s", full_path_ptr);
      if (actual_len_ptr)
        *actual_len_ptr = 0;
      return -1; // File not found
    }
    LOG_ERROR(logger_name_ptr, "Failed to open file_ptr '%s' for reading: %s",
              full_path_ptr, strerror(errno));
    return -1;
  }

  size_t bytes_read = 0;
  unsigned int hex_val;
  while (bytes_read < max_len && fscanf(f_ptr, "%02x", &hex_val) == 1) {
    data_ptr[bytes_read++] = (uint8_t)hex_val;
  }
  fclose(f_ptr);

  if (actual_len_ptr)
    *actual_len_ptr = bytes_read;

  LOG_DEBUG(logger_name_ptr, "Read %zu bytes_ptr from '%s' (hex)", bytes_read, full_path_ptr);
  return 0;
}

static signed char nvol_storage_write_hex(const char *path_ptr, const uint8_t *data_ptr,
                               size_t len) {
  if (!path_ptr || !data_ptr) {
    LOG_ERROR(logger_name_ptr, "Invalid parameters");
    return -1;
  }

  char full_path_ptr[512];
  if (build_full_path(path_ptr, full_path_ptr, sizeof(full_path_ptr)) != 0) {
    return -1;
  }

  // Write to temporary file_ptr first for atomicity
  char temp_path[512 + 16];
  snprintf(temp_path, sizeof(temp_path), "%s.tmp", full_path_ptr);

  FILE *f_ptr = fopen(temp_path, "w");
  if (!f_ptr) {
    LOG_ERROR(logger_name_ptr, "Failed to open temp file_ptr '%s' for writing: %s",
              temp_path, strerror(errno));
    return -1;
  }

  for (size_t i = 0; i < len; i++) {
    fprintf(f_ptr, "%02X", data_ptr[i]);
  }

  if (fflush(f_ptr) != 0 || fsync(fileno(f_ptr)) != 0) {
    fclose(f_ptr);
    unlink(temp_path);
    LOG_ERROR(logger_name_ptr, "Failed to sync temp file_ptr '%s'", temp_path);
    return -1;
  }

  fclose(f_ptr);

  if (chmod(temp_path, 0600) != 0) {
    unlink(temp_path);
    LOG_ERROR(logger_name_ptr, "Failed to chmod temp file_ptr '%s': %s", temp_path,
              strerror(errno));
    return -1;
  }

  if (rename(temp_path, full_path_ptr) != 0) {
    unlink(temp_path);
    LOG_ERROR(logger_name_ptr, "Failed to rename temp file_ptr to '%s': %s", full_path_ptr,
              strerror(errno));
    return -1;
  }

  LOG_DEBUG(logger_name_ptr, "Wrote %zu bytes_ptr to '%s' (hex)", len, full_path_ptr);
  return 0;
}

signed char platform_nvol_storage_read(const char *path_ptr, uint8_t *data_ptr,
                              size_t max_len, size_t *actual_len_ptr) {
  if (g_use_bin_format) {
    return nvol_storage_read_raw(path_ptr, data_ptr, max_len, actual_len_ptr);
  } else {
    return nvol_storage_read_hex(path_ptr, data_ptr, max_len, actual_len_ptr);
  }
}

signed char platform_nvol_storage_write(const char *path_ptr, const uint8_t *data_ptr,
                               size_t len) {
  if (g_use_bin_format) {
    return nvol_storage_write_raw(path_ptr, data_ptr, len);
  } else {
    return nvol_storage_write_hex(path_ptr, data_ptr, len);
  }
}

signed char platform_nvol_storage_delete(const char *path_ptr) {
  if (!path_ptr) {
    LOG_ERROR(logger_name_ptr, "Path is NULL");
    return -1;
  }

  char full_path_ptr[512];
  if (build_full_path(path_ptr, full_path_ptr, sizeof(full_path_ptr)) != 0) {
    return -1;
  }

  if (unlink(full_path_ptr) != 0) {
    if (errno == ENOENT) {
      LOG_DEBUG(logger_name_ptr, "File does not exist: %s", full_path_ptr);
      return 0; // Not an error if file_ptr doesn't exist
    }
    LOG_ERROR(logger_name_ptr, "Failed to delete file_ptr '%s': %s", full_path_ptr,
              strerror(errno));
    return -1;
  }

  LOG_DEBUG(logger_name_ptr, "Deleted file_ptr '%s'", full_path_ptr);
  return 0;
}

bool platform_nvol_storage_exists(const char *path_ptr) {
  if (!path_ptr) {
    return false;
  }

  char full_path_ptr[512];
  if (build_full_path(path_ptr, full_path_ptr, sizeof(full_path_ptr)) != 0) {
    return false;
  }

  struct stat st;
  return (stat(full_path_ptr, &st) == 0);
}

// Convenience functions

signed char platform_nvol_storage_read_key(const char *path_ptr, uint8_t key_ptr[32]) {
  size_t actual_len_ptr;
  signed char ret = platform_nvol_storage_read(path_ptr, key_ptr, 32, &actual_len_ptr);
  if (ret == 0 && actual_len_ptr != 32) {
    LOG_ERROR(logger_name_ptr, "Key file_ptr '%s' has wrong size: %zu (expected 32)",
              path_ptr, actual_len_ptr);
    return -1;
  }
  return ret;
}

signed char platform_nvol_storage_write_key(const char *path_ptr, const uint8_t key_ptr[32]) {
  return platform_nvol_storage_write(path_ptr, key_ptr, 32);
}

signed char platform_nvol_storage_read_salt(const char *path_ptr, uint8_t salt_ptr[SALT_LEN]) {
  size_t actual_len_ptr;
  signed char ret = platform_nvol_storage_read(path_ptr, salt_ptr, SALT_LEN, &actual_len_ptr);
  if (ret == 0 && actual_len_ptr != SALT_LEN) {
    LOG_ERROR(logger_name_ptr, "Salt file_ptr '%s' has wrong size: %zu (expected %d)",
              path_ptr, actual_len_ptr, SALT_LEN);
    return -1;
  }
  return ret;
}

signed char platform_nvol_storage_write_salt(const char *path_ptr,
                                    const uint8_t salt_ptr[SALT_LEN]) {
  return platform_nvol_storage_write(path_ptr, salt_ptr, SALT_LEN);
}

static signed char nvol_storage_read_int_str(const char *path_ptr, uint64_t *value_ptr) {
  char full_path_ptr[512];
  if (build_full_path(path_ptr, full_path_ptr, sizeof(full_path_ptr)) != 0) {
    return -1;
  }

  FILE *f_ptr = fopen(full_path_ptr, "r");
  if (!f_ptr) {
    if (errno == ENOENT) {
      LOG_DEBUG(logger_name_ptr, "File does not exist: %s", full_path_ptr);
      return -1;
    }
    LOG_ERROR(logger_name_ptr, "Failed to open file_ptr '%s' for reading: %s",
              full_path_ptr, strerror(errno));
    return -1;
  }

  if (fscanf(f_ptr, "%" SCNu64, value_ptr) != 1) {
    fclose(f_ptr);
    LOG_ERROR(logger_name_ptr, "Failed to parse integer from '%s'", full_path_ptr);
    return -1;
  }
  fclose(f_ptr);
  return 0;
}

static signed char nvol_storage_write_int_str(const char *path_ptr, uint64_t value_ptr) {
  char full_path_ptr[512];
  if (build_full_path(path_ptr, full_path_ptr, sizeof(full_path_ptr)) != 0) {
    return -1;
  }

  char temp_path[512 + 16];
  snprintf(temp_path, sizeof(temp_path), "%s.tmp", full_path_ptr);

  FILE *f_ptr = fopen(temp_path, "w");
  if (!f_ptr) {
    LOG_ERROR(logger_name_ptr, "Failed to open temp file_ptr '%s' for writing: %s",
              temp_path, strerror(errno));
    return -1;
  }

  fprintf(f_ptr, "%" PRIu64, value_ptr);

  if (fflush(f_ptr) != 0 || fsync(fileno(f_ptr)) != 0) {
    fclose(f_ptr);
    unlink(temp_path);
    LOG_ERROR(logger_name_ptr, "Failed to sync temp file_ptr '%s'", temp_path);
    return -1;
  }

  fclose(f_ptr);

  if (chmod(temp_path, 0600) != 0) {
    unlink(temp_path);
    LOG_ERROR(logger_name_ptr, "Failed to chmod temp file_ptr '%s': %s", temp_path,
              strerror(errno));
    return -1;
  }

  if (rename(temp_path, full_path_ptr) != 0) {
    unlink(temp_path);
    LOG_ERROR(logger_name_ptr, "Failed to rename temp file_ptr to '%s': %s", full_path_ptr,
              strerror(errno));
    return -1;
  }
  return 0;
}

signed char platform_nvol_storage_read_key_id(const char *path_ptr, uint32_t *key_id_ptr) {
  if (!key_id_ptr) {
    return -1;
  }

  if (g_use_bin_format) {
    uint8_t data_buf[4];
    size_t actual_len_ptr;
    signed char ret = nvol_storage_read_raw(path_ptr, data_buf, 4, &actual_len_ptr);
    if (ret == 0) {
      if (actual_len_ptr != 4) {
        LOG_ERROR(logger_name_ptr,
                  "Key ID file_ptr '%s' has wrong size: %zu (expected 4)", path_ptr,
                  actual_len_ptr);
        return -1;
      }
      *key_id_ptr = (uint32_t)data_buf[0] | ((uint32_t)data_buf[1] << 8) |
                ((uint32_t)data_buf[2] << 16) | ((uint32_t)data_buf[3] << 24);
    }
    return ret;
  } else {
    uint64_t val;
    if (nvol_storage_read_int_str(path_ptr, &val) == 0) {
      *key_id_ptr = (uint32_t)val;
      return 0;
    }
    return -1;
  }
}

signed char platform_nvol_storage_write_key_id(const char *path_ptr, uint32_t key_id_ptr) {
  if (g_use_bin_format) {
    uint8_t data_buf[4];
    data_buf[0] = (uint8_t)(key_id_ptr & 0xFF);
    data_buf[1] = (uint8_t)((key_id_ptr >> 8) & 0xFF);
    data_buf[2] = (uint8_t)((key_id_ptr >> 16) & 0xFF);
    data_buf[3] = (uint8_t)((key_id_ptr >> 24) & 0xFF);
    return nvol_storage_write_raw(path_ptr, data_buf, 4);
  } else {
    return nvol_storage_write_int_str(path_ptr, key_id_ptr);
  }
}

signed char platform_nvol_storage_read_u8(const char *path_ptr, uint8_t *value_ptr) {
  if (!value_ptr) {
    return -1;
  }

  if (g_use_bin_format) {
    size_t actual_len_ptr;
    signed char ret = nvol_storage_read_raw(path_ptr, value_ptr, 1, &actual_len_ptr);
    if (ret == 0 && actual_len_ptr != 1) {
      LOG_ERROR(logger_name_ptr, "U8 file_ptr '%s' has wrong size: %zu (expected 1)",
                path_ptr, actual_len_ptr);
      return -1;
    }
    return ret;
  } else {
    uint64_t val;
    if (nvol_storage_read_int_str(path_ptr, &val) == 0) {
      *value_ptr = (uint8_t)val;
      return 0;
    }
    return -1;
  }
}

signed char platform_nvol_storage_write_u8(const char *path_ptr, uint8_t value_ptr) {
  if (g_use_bin_format) {
    return nvol_storage_write_raw(path_ptr, &value_ptr, 1);
  } else {
    return nvol_storage_write_int_str(path_ptr, value_ptr);
  }
}

signed char platform_nvol_storage_read_u16(const char *path_ptr, uint16_t *value_ptr) {
  if (!value_ptr) {
    return -1;
  }

  if (g_use_bin_format) {
    uint8_t data_buf[2];
    size_t actual_len_ptr;
    signed char ret = nvol_storage_read_raw(path_ptr, data_buf, 2, &actual_len_ptr);
    if (ret == 0) {
      if (actual_len_ptr != 2) {
        LOG_ERROR(logger_name_ptr, "U16 file_ptr '%s' has wrong size: %zu (expected 2)",
                  path_ptr, actual_len_ptr);
        return -1;
      }
      *value_ptr = (uint16_t)((uint16_t)data_buf[0] | ((uint16_t)data_buf[1] << 8));
    }
    return ret;
  } else {
    uint64_t val;
    if (nvol_storage_read_int_str(path_ptr, &val) == 0) {
      *value_ptr = (uint16_t)val;
      return 0;
    }
    return -1;
  }
}

signed char platform_nvol_storage_write_u16(const char *path_ptr, uint16_t value_ptr) {
  if (g_use_bin_format) {
    uint8_t data_buf[2];
    data_buf[0] = (uint8_t)(value_ptr & 0xFF);
    data_buf[1] = (uint8_t)((value_ptr >> 8) & 0xFF);
    return nvol_storage_write_raw(path_ptr, data_buf, 2);
  } else {
    return nvol_storage_write_int_str(path_ptr, value_ptr);
  }
}

signed char platform_nvol_storage_read_u32(const char *path_ptr, uint32_t *value_ptr) {
  if (!value_ptr) {
    return -1;
  }

  if (g_use_bin_format) {
    uint8_t data_buf[4];
    size_t actual_len_ptr;
    signed char ret = nvol_storage_read_raw(path_ptr, data_buf, 4, &actual_len_ptr);
    if (ret == 0) {
      if (actual_len_ptr != 4) {
        LOG_ERROR(logger_name_ptr, "U32 file_ptr '%s' has wrong size: %zu (expected 4)",
                  path_ptr, actual_len_ptr);
        return -1;
      }
      *value_ptr = (uint32_t)data_buf[0] | ((uint32_t)data_buf[1] << 8) |
               ((uint32_t)data_buf[2] << 16) | ((uint32_t)data_buf[3] << 24);
    }
    return ret;
  } else {
    uint64_t val;
    if (nvol_storage_read_int_str(path_ptr, &val) == 0) {
      *value_ptr = (uint32_t)val;
      return 0;
    }
    return -1;
  }
}

signed char platform_nvol_storage_write_u32(const char *path_ptr, uint32_t value_ptr) {
  if (g_use_bin_format) {
    uint8_t data_buf[4];
    data_buf[0] = (uint8_t)(value_ptr & 0xFF);
    data_buf[1] = (uint8_t)((value_ptr >> 8) & 0xFF);
    data_buf[2] = (uint8_t)((value_ptr >> 16) & 0xFF);
    data_buf[3] = (uint8_t)((value_ptr >> 24) & 0xFF);
    return nvol_storage_write_raw(path_ptr, data_buf, 4);
  } else {
    return nvol_storage_write_int_str(path_ptr, value_ptr);
  }
}

signed char platform_nvol_storage_read_u64(const char *path_ptr, uint64_t *value_ptr) {
  if (!value_ptr) {
    return -1;
  }

  if (g_use_bin_format) {
    uint8_t data_buf[8];
    size_t actual_len_ptr;
    signed char ret = nvol_storage_read_raw(path_ptr, data_buf, 8, &actual_len_ptr);
    if (ret == 0) {
      if (actual_len_ptr != 8) {
        LOG_ERROR(logger_name_ptr, "U64 file_ptr '%s' has wrong size: %zu (expected 8)",
                  path_ptr, actual_len_ptr);
        return -1;
      }
      *value_ptr = 0;
      for (int i = 0; i < 8; i++) {
        *value_ptr |= ((uint64_t)data_buf[i]) << (i * 8);
      }
    }
    return ret;
  } else {
    uint64_t val;
    if (nvol_storage_read_int_str(path_ptr, &val) == 0) {
      *value_ptr = val;
      return 0;
    }
    return -1;
  }
}

signed char platform_nvol_storage_write_u64(const char *path_ptr, uint64_t value_ptr) {
  if (g_use_bin_format) {
    uint8_t data_buf[8];
    for (int i = 0; i < 8; i++) {
      data_buf[i] = (uint8_t)((value_ptr >> (i * 8)) & 0xFF);
    }
    return nvol_storage_write_raw(path_ptr, data_buf, 8);
  } else {
    return nvol_storage_write_int_str(path_ptr, value_ptr);
  }
}

signed char platform_nvol_storage_read_varlen(const char *path_ptr, uint8_t *data_ptr,
                                     size_t max_len, size_t *actual_len_ptr) {
  return platform_nvol_storage_read(path_ptr, data_ptr, max_len, actual_len_ptr);
}

signed char platform_nvol_storage_write_varlen(const char *path_ptr, const uint8_t *data_ptr,
                                      size_t len) {
  return platform_nvol_storage_write(path_ptr, data_ptr, len);
}

signed char platform_nvol_storage_read_varlen_alloc(const char *path_ptr, uint8_t **data_ptr,
                                           size_t *len_ptr) {
  if (!path_ptr || !data_ptr || !len_ptr) {
    return -1;
  }
  char full_path_ptr[512];
  if (build_full_path(path_ptr, full_path_ptr, sizeof(full_path_ptr)) != 0) {
    return -1;
  }
  FILE *f_ptr = fopen(full_path_ptr, "rb");
  if (!f_ptr) {
    return -1;
  }
  fseek(f_ptr, 0, SEEK_END);
  long file_size_long = ftell(f_ptr);
  fseek(f_ptr, 0, SEEK_SET);
  if (file_size_long <= 0) {
    fclose(f_ptr);
    return -1;
  }
  size_t file_size = (size_t)file_size_long;
  *data_ptr = malloc(file_size);
  if (!*data_ptr) {
    fclose(f_ptr);
    return -1;
  }
  size_t read = fread(*data_ptr, 1, file_size, f_ptr);
  fclose(f_ptr);
  *len_ptr = read;
  return 0;
}

// Look up key hex value in a provisioning file for initial setup.
uint8_t *platform_retrieve_dict_from_file(const char *filename_ptr, const char *key_ptr,
                                 size_t *out_len_ptr) {
  FILE *file_ptr = fopen(filename_ptr, "r");
  if (!file_ptr) {
    LOG_ERROR(logger_name_ptr, "Failed to open file_ptr: %s", filename_ptr);
    return NULL;
  }

  char *line_ptr = NULL;
  size_t line_cap = 0;
  ssize_t nread;
  while ((nread = getline(&line_ptr, &line_cap, file_ptr)) != -1) {
    char *colon_ptr = strchr(line_ptr, ':');
    if (!colon_ptr)
      continue;

    *colon_ptr = '\0';
    if (strcmp(line_ptr, key_ptr) != 0)
      continue;

    char *value_ptr = colon_ptr + 1;
    while (isspace(*value_ptr))
      value_ptr++;
    char *end_ptr = value_ptr + strlen(value_ptr) - 1;
    while (end_ptr > value_ptr && isspace(*end_ptr))
      *end_ptr-- = '\0';

    size_t value_len = strlen(value_ptr);
    if (value_len == 0 || value_len % 2 != 0) {
      LOG_ERROR(logger_name_ptr,
                "Value for %s in %s is not an even-length hex string", key_ptr,
                filename_ptr);
      free(line_ptr);
      fclose(file_ptr);
      return NULL;
    }
    *out_len_ptr = value_len / 2;
    uint8_t *bytes_ptr = malloc(*out_len_ptr);
    if (!bytes_ptr) {
      free(line_ptr);
      fclose(file_ptr);
      return NULL;
    }

    for (size_t i = 0; i < *out_len_ptr; i++) {
      if (sscanf(value_ptr + 2 * i, "%2hhx", &bytes_ptr[i]) != 1) {
        LOG_ERROR(logger_name_ptr, "Invalid hex digit in value_ptr for %s in %s", key_ptr,
                  filename_ptr);
        free(bytes_ptr);
        free(line_ptr);
        fclose(file_ptr);
        return NULL;
      }
    }
    free(line_ptr);
    fclose(file_ptr);
    LOG_INFO(logger_name_ptr, "Value for %s found in file_ptr %s", key_ptr, filename_ptr);
    LOG_SECRET(logger_name_ptr, "Loaded secret value_ptr", bytes_ptr, *out_len_ptr);
    return bytes_ptr;
  }

  free(line_ptr);
  fclose(file_ptr);
  LOG_ERROR(logger_name_ptr, "Value %s not found in file_ptr %s", key_ptr, filename_ptr);
  return NULL;
}

void platform_nvol_storage_list_keys(void) {
  if (!g_base_path_ptr) {
    return;
  }
  
  const char *subdirs[] = {"keys", "config", "code_update"};
  for (size_t i = 0; i < sizeof(subdirs) / sizeof(subdirs[0]); i++) {
    char subdir_path[512];
    snprintf(subdir_path, sizeof(subdir_path), "%s/%s", g_base_path_ptr, subdirs[i]);
    
    struct stat st = {0};
    if (stat(subdir_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
      continue;
    }
    
    DIR *dir_ptr = opendir(subdir_path);
    if (!dir_ptr) {
      continue;
    }
    
    struct dirent *entry_ptr;
    while ((entry_ptr = readdir(dir_ptr)) != NULL) {
      if (entry_ptr->d_type == DT_REG) {
        LOG_INFO(logger_name_ptr, "Storage key_ptr: %s/%s", subdirs[i], entry_ptr->d_name);
      }
    }
    closedir(dir_ptr);
  }
}

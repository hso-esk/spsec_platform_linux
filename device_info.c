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

#define _GNU_SOURCE
#include "device_info.h"
#include "spsec_common.h"
#include "keys.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <sys/types.h>
#include <strings.h>

static const char *logger_name_ptr = "device_info";

signed char device_info_get_identification(char *buffer_ptr, size_t buffer_size) {
  if (!buffer_ptr || buffer_size == 0) {
    return -1;
  }

  // Register 81h - Device Identification (mock for Linux).

  struct utsname sys_info;
  if (uname(&sys_info) == 0) {
    // Format: "Manufacturer/Product/Serial" or similar
    // Using system information as placeholder
    int ret = snprintf(buffer_ptr, buffer_size, "SPsec-Linux/%s/%s",
                       sys_info.nodename, sys_info.machine);
    if (ret < 0 || (size_t)ret >= buffer_size) {
      LOG_WARNING(logger_name_ptr, "Device identification string truncated");
      buffer_ptr[buffer_size - 1] = '\0';
    }
    LOG_INFO(logger_name_ptr, "Device identification: %s", buffer_ptr);
  } else {
    // Fallback to default
    strncpy(buffer_ptr, "SPsec-Participant/Unknown/00000000", buffer_size - 1);
    buffer_ptr[buffer_size - 1] = '\0';
    LOG_WARNING(
        logger_name_ptr,
        "Failed to get system info, using default device identification");
  }

  // TODO(embedded): read device identification from EEPROM/Flash or the
  // MCU's unique ID instead.

  return 0;
}

signed char device_info_get_mcu_serial(uint8_t *serial_ptr) {
  if (!serial_ptr) {
    return -1;
  }

  // Register 82h - 128-bit MCU Serial Number (pseudo-unique ID for Linux).

  // Try to read from /etc/machine-id or generate from hostname
  FILE *f_ptr = fopen("/etc/machine-id", "r");
  if (f_ptr) {
    char machine_id[64];
    if (fgets(machine_id, sizeof(machine_id), f_ptr)) {
      // Convert machine-id (hex_ptr string) to bytes
      // Machine ID is typically 32 hex_ptr chars = 16 bytes
      size_t len = strlen(machine_id);
      if (len > 0 && machine_id[len - 1] == '\n')
        machine_id[len - 1] = '\0';

      // Parse hex_ptr string to bytes
      memset(serial_ptr, 0, 16);
      // Convert 32-character hex machine-id to 16-byte serial.
      size_t id_len = strlen(machine_id);
      for (size_t i = 0; i + 1 < id_len && i < 32; i += 2) {
        char hex_byte[3] = {machine_id[i], machine_id[i + 1], '\0'};
        serial_ptr[i / 2] = (uint8_t)strtoul(hex_byte, NULL, 16);
      }
      fclose(f_ptr);
      LOG_INFO(logger_name_ptr, "MCU serial_ptr number loaded from /etc/machine-id");
      return 0;
    }
    fclose(f_ptr);
  }

  // Fallback: generate from hostname hash
  struct utsname sys_info;
  if (uname(&sys_info) == 0) {
    // Simple hash of hostname to generate pseudo-unique serial_ptr
    uint32_t hash = 0;
    for (const char *p_ptr = sys_info.nodename; *p_ptr; p_ptr++) {
      hash = hash * 31 + (uint32_t)(unsigned char)*p_ptr;
    }
    memset(serial_ptr, 0, 16);
    // Put hash in first 4 bytes, repeat pattern
    for (int i = 0; i < 4; i++) {
      serial_ptr[i] = (uint8_t)((hash >> (i * 8)) & 0xFF);
      serial_ptr[i + 4] = serial_ptr[i];
      serial_ptr[i + 8] = serial_ptr[i];
      serial_ptr[i + 12] = serial_ptr[i];
    }
    LOG_INFO(logger_name_ptr,
             "MCU serial_ptr number generated from hostname (fallback)");
  } else {
    // Last resort: zero-filled
    memset(serial_ptr, 0, 16);
    LOG_WARNING(logger_name_ptr,
                "Failed to get system info, using zero MCU serial_ptr number");
  }

  // TODO(embedded): read the real MCU unique-ID register (e.g. STM32 UID)
  // or a secure element instead.

  return 0;
}

signed char device_info_get_public_auth_key(uint8_t *key_ptr, size_t *key_size_ptr) {
  if (!key_ptr || !key_size_ptr || *key_size_ptr < KEY_LEN) {
    return -1;
  }

  // Read public authentication key_ptr from environment or key_ptr file (register 0x91)
  const char *hex_ptr = getenv("SPSEC_PUBLIC_AUTH_KEY");
  char file_buf[2 * KEY_LEN + 2];

  if (!hex_ptr) {
    const char *path_ptr = getenv("SPSEC_PUBLIC_AUTH_KEY_FILE");
    if (path_ptr) {
      FILE *f_ptr = fopen(path_ptr, "r");
      if (!f_ptr) {
        LOG_ERROR(logger_name_ptr,
                  "Public Authentication Key (91h): cannot open '%s'", path_ptr);
        return -1;
      }
      size_t n = fread(file_buf, 1, sizeof(file_buf) - 1, f_ptr);
      fclose(f_ptr);
      file_buf[n] = '\0';
      // Trim trailing whitespace/newline so a normal text file works.
      while (n > 0 && (file_buf[n - 1] == '\n' || file_buf[n - 1] == '\r' ||
                       file_buf[n - 1] == ' ' || file_buf[n - 1] == '\t')) {
        file_buf[--n] = '\0';
      }
      hex_ptr = file_buf;
    }
  }

  if (!hex_ptr) {
    LOG_WARNING(logger_name_ptr,
                "Public Authentication Key (91h) not provisioned - code "
                "updates will be refused (set SPSEC_PUBLIC_AUTH_KEY or "
                "SPSEC_PUBLIC_AUTH_KEY_FILE)");
    return -1;
  }

  if (strlen(hex_ptr) != 2 * KEY_LEN) {
    LOG_ERROR(logger_name_ptr,
              "Public Authentication Key (91h) must be %d hex_ptr characters, got "
              "%zu",
              2 * KEY_LEN, strlen(hex_ptr));
    return -1;
  }

  for (size_t i = 0; i < KEY_LEN; ++i) {
    unsigned int byte = 0;
    if (sscanf(hex_ptr + 2 * i, "%2x", &byte) != 1) {
      LOG_ERROR(logger_name_ptr,
                "Public Authentication Key (91h) is not valid hex_ptr at offset %zu",
                2 * i);
      return -1;
    }
    key_ptr[i] = (uint8_t)byte;
  }
  *key_size_ptr = KEY_LEN;

  LOG_INFO(logger_name_ptr, "Public Authentication Key (91h) loaded");
  return 0;
}

uint32_t device_info_get_code_update_capabilities(void) {
  // SPsec302 §2.3.7.1, register 90h: bit 0 = update capable,
  // bits 24-31 = manufacturer code.

  // Check if code update is enabled via environment variable or platform
  // detection
  const char *env_ptr = getenv("SPSEC_CODE_UPDATE_ENABLED");
  bool update_capable =
      (env_ptr && (strcmp(env_ptr, "1") == 0 || strcasecmp(env_ptr, "true") == 0));

  uint32_t capabilities = 0;
  if (update_capable) {
    capabilities |= 0x01; // Bit 0: update capable
  }
  // Bits 24-31: manufacturer_ptr specific (can be set via environment)
  const char *manufacturer_ptr = getenv("SPSEC_MANUFACTURER_CODE");
  if (manufacturer_ptr) {
    uint32_t mfg_code = (uint32_t)strtoul(manufacturer_ptr, NULL, 0) & 0xFF;
    capabilities |= (mfg_code << 24);
  }

  LOG_INFO(logger_name_ptr, "Code update capabilities: 0x%08X (capable=%s)",
           capabilities, update_capable ? "YES" : "NO");

  // TODO(embedded): detect real bootloader/flash update capability instead.

  return capabilities;
}

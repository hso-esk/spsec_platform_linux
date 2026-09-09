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

#include "can_bitrate.h"
#include "spsec_common.h"
#include "spsec_registers.h"

static const char *logger_name_ptr = "can_bitrate";

uint32_t can_bitrate_nominal_to_bps(uint8_t nominal_enum) {
  switch (nominal_enum) {
  case SPSEC_CAN_NOMINAL_1000KBPS:
    return 1000000;
  case SPSEC_CAN_NOMINAL_800KBPS:
    return 800000;
  case SPSEC_CAN_NOMINAL_500KBPS:
    return 500000;
  case SPSEC_CAN_NOMINAL_250KBPS:
    return 250000;
  default:
    if (nominal_enum >= SPSEC_CAN_NOMINAL_MANUFACTURER_MIN) {
      // Manufacturer specific - return as-is (caller should handle)
      return nominal_enum * 1000; // Placeholder conversion
    }
    return 0;
  }
}

uint32_t can_bitrate_data_to_bps(uint8_t data_enum) {
  switch (data_enum) {
  case SPSEC_CAN_DATA_1MBPS:
    return 1000000;
  case SPSEC_CAN_DATA_2MBPS:
    return 2000000;
  case SPSEC_CAN_DATA_4MBPS:
    return 4000000;
  case SPSEC_CAN_DATA_5MBPS:
    return 5000000;
  case SPSEC_CAN_DATA_8MBPS:
    return 8000000;
  case SPSEC_CAN_DATA_10MBPS:
    return 10000000;
  default:
    if (data_enum >= SPSEC_CAN_DATA_MANUFACTURER_MIN) {
      // Manufacturer specific - return as-is (caller should handle)
      return data_enum * 1000000; // Placeholder conversion
    }
    return 0;
  }
}

signed char can_bitrate_apply(const char *interface_name_ptr,
                              spsec_can_fd_bitrate_t bitrate_config) {
  if (!interface_name_ptr) {
    LOG_ERROR(logger_name_ptr, "Invalid interface name");
    return -1;
  }

  uint32_t nominal_bps =
      can_bitrate_nominal_to_bps(bitrate_config.rates.nominal_bitrate);
  uint32_t data_bps =
      can_bitrate_data_to_bps(bitrate_config.rates.data_bitrate);

  if (nominal_bps == 0 || data_bps == 0) {
    LOG_ERROR(logger_name_ptr,
              "Invalid bitrate configuration: nominal=0x%02X, data_ptr=0x%02X",
              bitrate_config.rates.nominal_bitrate,
              bitrate_config.rates.data_bitrate);
    return -1;
  }

  // SPsec302 §2.3.5.5: bitrate activates on power cycle. Linux can't change
  // it at runtime; use `ip link set <if> type can bitrate ... fd on`.

  LOG_WARNING(
      logger_name_ptr,
      "CAN FD bitrate configuration loaded: nominal=%u bps, data_ptr=%u bps",
      nominal_bps, data_bps);
  LOG_WARNING(logger_name_ptr,
              "To apply bitrate on Linux, run: ip link set %s type can bitrate "
              "%u dbitrate %u fd on",
              interface_name_ptr, nominal_bps, data_bps);
  LOG_WARNING(logger_name_ptr,
              "Bitrate change requires interface to be down. Current "
              "session will continue with existing bitrate.");

  // TODO(embedded): reconfigure CAN controller registers/timing for the
  // new bitrates and verify it took effect.

  return 0;
}

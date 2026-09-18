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

#include "dll_events.h"
#include "spsec_common.h"
#include "spsec_registers.h"

static const char *logger_name_ptr = "dll_events";

// Duplicate frame detection window (in microseconds)
// Frames received within this window with same CAN ID are considered duplicates
#define DUPLICATE_DETECTION_WINDOW_US 1000ULL // 1ms window

signed char dll_events_init(DLLEventContext *ctx_ptr,
                            uint8_t own_participant_id) {
  if (!ctx_ptr) {
    return -1;
  }

  memset(ctx_ptr, 0, sizeof(DLLEventContext));

  // Register all control-plane CAN IDs for this participant.
  // Format (SPsec302): 0x1E000000 | (CPMT << 8) | participant_id.
  for (uint8_t cpmt = 0; cpmt <= 12; cpmt++) {
    uint32_t can_id =
        (uint32_t)(0x1E000000 | (cpmt << 8) | (own_participant_id & 0x7F));
    dll_events_register_own_can_id(ctx_ptr, can_id);
    // Also register destination addressing version (bit 7 set)
    can_id = (uint32_t)(0x1E000000 | (cpmt << 8) |
                         ((own_participant_id & 0x7F) | 0x80));
    dll_events_register_own_can_id(ctx_ptr, can_id);
  }

  LOG_INFO(logger_name_ptr, "DLL event detection initialized for participant %u",
           own_participant_id);
  return 0;
}

void dll_events_cleanup(DLLEventContext *ctx_ptr) {
  if (ctx_ptr) {
    memset(ctx_ptr, 0, sizeof(DLLEventContext));
  }
}

uint16_t dll_events_check_rx_overrun(DLLEventContext *ctx_ptr) {
  if (!ctx_ptr) {
    return 0;
  }

  // SPsec302 §8.2 DLL_RX_OVERRUN (0xDE01). Rare on Linux's socket API.

  if (ctx_ptr->rx_overrun_detected) {
    ctx_ptr->rx_overrun_detected = false; // Reset after reporting
    LOG_WARNING(logger_name_ptr, "DLL RX overrun detected");
    return SPSEC_DLL_RX_OVERRUN;
  }

  // TODO(embedded): monitor the CAN controller's RX buffer status directly.

  return 0;
}

uint16_t dll_events_check_tx_overrun(DLLEventContext *ctx_ptr) {
  if (!ctx_ptr) {
    return 0;
  }

  // DLL_TX_OVERRUN (0xDE02): transmit buffer overrun detection.

  if (ctx_ptr->tx_overrun_detected) {
    ctx_ptr->tx_overrun_detected = false; // Reset after reporting
    LOG_WARNING(logger_name_ptr, "DLL TX overrun detected");
    return SPSEC_DLL_TX_OVERRUN;
  }

  // TODO(embedded): monitor the CAN controller's TX buffer status directly.

  return 0;
}

uint16_t dll_events_check_address_guard(DLLEventContext *ctx_ptr,
                                        uint32_t can_id) {
  if (!ctx_ptr) {
    return 0;
  }

  // DLL_ADRID_GUARD (0xDE03): injection of own address ID detection.

  for (uint8_t i = 0; i < ctx_ptr->own_can_id_count; i++) {
    if (ctx_ptr->own_can_ids[i] == can_id) {
      LOG_WARNING(logger_name_ptr,
                  "DLL Address ID guard violation: received own CAN ID 0x%08X",
                  can_id);
      return SPSEC_DLL_ADRID_GUARD;
    }
  }

  return 0;
}

uint16_t dll_events_check_duplicate_frame(DLLEventContext *ctx_ptr,
                                          uint32_t can_id,
                                          uint64_t current_timestamp) {
  if (!ctx_ptr) {
    return 0;
  }

  // SPsec302 §8.2 DLL_DUP_FRAME_IGNORED: same CAN ID seen twice within
  // DUPLICATE_DETECTION_WINDOW_US.

  if (ctx_ptr->last_received_can_id == can_id &&
      ctx_ptr->last_received_timestamp != 0) {
    uint64_t time_diff = current_timestamp - ctx_ptr->last_received_timestamp;
    if (time_diff < DUPLICATE_DETECTION_WINDOW_US) {
      LOG_WARNING(
          logger_name_ptr,
          "DLL duplicate frame detected: CAN ID 0x%08X (time diff: %llu us)",
          can_id, (unsigned long long)time_diff);
      return SPSEC_DLL_DUP_FRAME_IGNORED;
    }
  }

  // Update last received frame info
  ctx_ptr->last_received_can_id = can_id;
  ctx_ptr->last_received_timestamp = current_timestamp;

  return 0;
}

void dll_events_register_own_can_id(DLLEventContext *ctx_ptr, uint32_t can_id) {
  if (!ctx_ptr) {
    return;
  }

  // Check if already registered
  for (uint8_t i = 0; i < ctx_ptr->own_can_id_count; i++) {
    if (ctx_ptr->own_can_ids[i] == can_id) {
      return; // Already registered
    }
  }

  // Add to list if space available
  if (ctx_ptr->own_can_id_count <
      sizeof(ctx_ptr->own_can_ids) / sizeof(ctx_ptr->own_can_ids[0])) {
    ctx_ptr->own_can_ids[ctx_ptr->own_can_id_count++] = can_id;
    LOG_DEBUG(logger_name_ptr, "Registered own CAN ID: 0x%08X", can_id);
  } else {
    LOG_WARNING(logger_name_ptr, "Cannot register own CAN ID 0x%08X: buffer full",
                can_id);
  }
}

uint16_t dll_events_process_received_frame(DLLEventContext *ctx_ptr,
                                           uint32_t can_id,
                                           uint64_t current_timestamp) {
  if (!ctx_ptr) {
    return 0;
  }

  // Check all DLL events
  uint16_t event = dll_events_check_address_guard(ctx_ptr, can_id);
  if (event != 0) {
    return event;
  }

  event = dll_events_check_duplicate_frame(ctx_ptr, can_id, current_timestamp);
  if (event != 0) {
    return event;
  }

  // RX overrun is checked separately (not per-frame)
  // TX overrun is checked on send operations

  return 0;
}

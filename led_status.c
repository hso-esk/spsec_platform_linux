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

#include "led_status.h"
#include "spsec_common.h"

static const char *logger_name_ptr = "led_status";
static spsec_state_t last_state = SPSEC_STATE_NOT_SET;
static bool last_alert_flag = false;

signed char led_status_init(void) {
  LOG_INFO(logger_name_ptr, "LED status system initialized (mock implementation - "
                        "debug logging only)");
  return 0;
}

void led_status_cleanup(void) {
  LOG_INFO(logger_name_ptr, "LED status system cleaned up");
}

void led_status_update(spsec_state_t state, bool alert_flag) {
  // Only log on state change or alert flag change to avoid spam
  if (state == last_state && alert_flag == last_alert_flag) {
    return;
  }

  last_state = state;
  last_alert_flag = alert_flag;

  // SPsec302 V40 Section 8.1: LED Security Status Indication
  switch (state) {
  case SPSEC_STATE_NOT_SET:
  case SPSEC_STATE_SHUTDOWN:
    // Red on, green off
    LOG_INFO(logger_name_ptr, "LED Status: RED ON, GREEN OFF (State: %s)",
             state == SPSEC_STATE_NOT_SET ? "NOT_SET" : "SHUTDOWN");
    break;

  case SPSEC_STATE_WAITING:
    // Red off, green blinking (200ms on, 800ms off)
    LOG_INFO(logger_name_ptr, "LED Status: RED OFF, GREEN BLINKING (200ms on, "
                          "800ms off) - State: WAITING");
    break;

  case SPSEC_STATE_SECURE:
    // Red off, green on
    if (alert_flag) {
      LOG_INFO(logger_name_ptr,
               "LED Status: RED OFF, GREEN ON (State: SECURE, Alert: SET)");
    } else {
      LOG_INFO(logger_name_ptr, "LED Status: RED OFF, GREEN ON (State: SECURE)");
    }
    break;

  case SPSEC_STATE_WARNING:
    // Red and green blinking alternately (400ms)
    LOG_INFO(logger_name_ptr, "LED Status: RED and GREEN BLINKING ALTERNATELY "
                          "(400ms) - State: WARNING");
    break;

  case SPSEC_STATE_CONFIGURATION:
    // Red off, green blinking (800ms on, 200ms off)
    LOG_INFO(logger_name_ptr, "LED Status: RED OFF, GREEN BLINKING (800ms on, "
                          "200ms off) - State: CONFIGURATION");
    break;

  default:
    LOG_WARNING(logger_name_ptr, "LED Status: Unknown state %d", state);
    break;
  }

  // TODO: For embedded systems, implement actual LED hardware control here:
  // - Set GPIO pins for red/green LEDs
  // - Implement blinking patterns using timers
}

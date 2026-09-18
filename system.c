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

/* Linux process lifecycle: wires SIGINT/SIGTERM to stop callback. */

#include "platform_system.h"

#include <signal.h>
#include <string.h>

static void (*s_on_stop_ptr)(void) = NULL;

static void signal_forwarder(int sig) {
  (void)sig;
  if (s_on_stop_ptr)
    s_on_stop_ptr();
}

void platform_request_stop_on_signal(void (*on_stop_ptr)(void)) {
  s_on_stop_ptr = on_stop_ptr;

  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = signal_forwarder;
  // No SA_RESTART: a signal interrupts the blocking select() in the receive
  // loops so they re-check the stop flag and unwind.
  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);
}

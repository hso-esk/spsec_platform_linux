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

#include "randomgen.h"
#include "spsec_common.h"

static const char *logger_name_ptr = "random";

signed char random_generator_init(RandomGenerator *rg_ptr) {
  if (!rg_ptr)
    return -1;

  if (crypto_rng_init(&rg_ptr->core) != 0) {
    LOG_ERROR(logger_name_ptr, "Failed to initialise crypto RNG");
    rg_ptr->initialized = false;
    return -1;
  }

  rg_ptr->initialized = true;
  LOG_INFO(logger_name_ptr, "Random generator initialised");
  return 0;
}

void random_generator_free(RandomGenerator *rg_ptr) {
  if (!rg_ptr || !rg_ptr->initialized)
    return;
  crypto_rng_free(&rg_ptr->core);
  rg_ptr->initialized = false;
}

// Allocate num_bytes of random data; caller frees. NULL if uninit or mbedTLS fails.
uint8_t *random_generator_get_bytes(RandomGenerator *rg_ptr, size_t num_bytes) {
  if (!rg_ptr->initialized) {
    LOG_ERROR(logger_name_ptr, "Random generator not initialized");
    return NULL;
  }

  uint8_t *bytes_ptr = malloc(num_bytes);
  if (!bytes_ptr) {
    LOG_ERROR(logger_name_ptr, "Failed to allocate memory for random bytes");
    return NULL;
  }

  if (crypto_rng_get_bytes(&rg_ptr->core, bytes_ptr, num_bytes) != 0) {
    LOG_ERROR(logger_name_ptr, "Failed to generate random bytes");
    free(bytes_ptr);
    return NULL;
  }

  return bytes_ptr;
}
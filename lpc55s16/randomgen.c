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
 * @file randomgen.c
 * @brief LPC55S16 baremetal CSPRNG using TRNG.
 */

#include "randomgen.h"
#include "spsec_common.h"
#include "fsl_trng.h"

static const char *logger_name = "rng_lpc55";
static trng_config_t trng_config;
static bool trng_initialized = false;

signed char random_generator_init(RandomGenerator *rg)
{
    (void)rg;
    if (trng_initialized) return 0;

    TRNG_GetDefaultConfig(&trng_config);
    status_t status = TRNG_Init(TRNG, &trng_config);
    if (status != kStatus_Success) {
        LOG_ERROR(logger_name, "TRNG init failed: %d", status);
        return -1;
    }
    trng_initialized = true;
    LOG_INFO(logger_name, "TRNG initialized");
    return 0;
}

void random_generator_deinit(RandomGenerator *rg)
{
    (void)rg;
    if (trng_initialized) {
        TRNG_Deinit(TRNG);
        trng_initialized = false;
    }
}

signed char random_generator_get_bytes(void *ctx, uint8_t *out, size_t len)
{
    (void)ctx;
    if (!out || len == 0) return -1;

    if (!trng_initialized) {
        if (random_generator_init(NULL) != 0) return -1;
    }

    status_t status = TRNG_GetRandomData(TRNG, out, len);
    if (status != kStatus_Success) {
        LOG_ERROR(logger_name, "TRNG get data failed: %d", status);
        return -1;
    }
    return 0;
}
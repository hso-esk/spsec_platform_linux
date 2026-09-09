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
 * @file can_bitrate.c
 * @brief LPC55S16 CAN FD bitrate configuration.
 */

#include "can_bitrate.h"
#include "spsec_common.h"
#include "fsl_flexcan.h"

static const char *logger_name = "can_br_lpc55";

static const struct {
    const char *name;
    uint32_t nominal_bps;
    uint32_t data_bps;
} bitrate_table[] = {
    { "125k/500k",    125000,  500000 },
    { "250k/1M",      250000,  1000000 },
    { "500k/2M",      500000,  2000000 },
    { "1M/5M",        1000000, 5000000 },
    { "2M/8M",        2000000, 8000000 },
};

const CanBitrateInfo *can_bitrate_by_name(const char *name)
{
    static CanBitrateInfo info;
    for (size_t i = 0; i < sizeof(bitrate_table)/sizeof(bitrate_table[0]); i++) {
        if (strcmp(name, bitrate_table[i].name) == 0) {
            info.nominal_bps = bitrate_table[i].nominal_bps;
            info.data_bps = bitrate_table[i].data_bps;
            return &info;
        }
    }
    return NULL;
}

const CanBitrateInfo *can_bitrate_by_value(uint32_t nominal_bps, uint32_t data_bps)
{
    static CanBitrateInfo info;
    for (size_t i = 0; i < sizeof(bitrate_table)/sizeof(bitrate_table[0]); i++) {
        if (bitrate_table[i].nominal_bps == nominal_bps &&
            bitrate_table[i].data_bps == data_bps) {
            info.nominal_bps = nominal_bps;
            info.data_bps = data_bps;
            return &info;
        }
    }
    return NULL;
}

const char *can_bitrate_name(uint32_t nominal_bps, uint32_t data_bps)
{
    for (size_t i = 0; i < sizeof(bitrate_table)/sizeof(bitrate_table[0]); i++) {
        if (bitrate_table[i].nominal_bps == nominal_bps &&
            bitrate_table[i].data_bps == data_bps) {
            return bitrate_table[i].name;
        }
    }
    return "unknown";
}

signed char can_bitrate_configure(void *can_handle, uint32_t nominal_bps, uint32_t data_bps)
{
    CAN_Type *base = (CAN_Type *)can_handle;
    if (!base) return -1;

    status_t status = FLEXCAN_SetFDBaudRate(base, nominal_bps, data_bps,
                                            CLOCK_GetFreq(kCLOCK_Can0));
    if (status != kStatus_Success) {
        LOG_ERROR(logger_name, "Failed to set FD baud rate: %d", status);
        return -1;
    }
    return 0;
}
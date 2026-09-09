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
 * @file device_info.c
 * @brief LPC55S16 baremetal device info (UID, serial, versions).
 */

#include "device_info.h"
#include "spsec_common.h"
#include "fsl_gpc.h"
#include "fsl_ocotp.h"

static const char *logger_name = "devinfo_lpc55";

static const char *hw_version = "LPC55S16-EVK Rev A";
static const char *fw_version = "SPsec 1.0.0";

signed char device_info_init(DeviceInfo *info)
{
    if (!info) return -1;

    /* Read MCU unique ID from OCOTP */
    ocotp_config_t ocotp_config;
    OCOTP_GetDefaultConfig(&ocotp_config);
    OCOTP_Init(OCOTP, &ocotp_config);

    uint32_t uuid[4];
    status_t status = OCOTP_ReadUniqueID(OCOTP, uuid);
    if (status != kStatus_Success) {
        LOG_ERROR(logger_name, "Failed to read unique ID");
        return -1;
    }

    memcpy(info->uid, uuid, 16);
    info->serial_len = 16;
    memcpy(info->serial_number, uuid, 16);

    info->hw_version = hw_version;
    info->fw_version = fw_version;

    OCOTP_Deinit(OCOTP);
    LOG_INFO(logger_name, "Device info initialized");
    return 0;
}

void device_info_deinit(DeviceInfo *info)
{
    (void)info;
}

const char *device_info_get_hw_version(const DeviceInfo *info)
{
    return info ? info->hw_version : hw_version;
}

const char *device_info_get_fw_version(const DeviceInfo *info)
{
    return info ? info->fw_version : fw_version;
}

const uint8_t *device_info_get_uid(const DeviceInfo *info, size_t *len)
{
    if (info) {
        if (len) *len = 16;
        return info->uid;
    }
    if (len) *len = 0;
    return NULL;
}

const uint8_t *device_info_get_serial(const DeviceInfo *info, size_t *len)
{
    if (info) {
        if (len) *len = info->serial_len;
        return info->serial_number;
    }
    if (len) *len = 0;
    return NULL;
}
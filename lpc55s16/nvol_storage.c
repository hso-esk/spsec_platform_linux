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
 * @file nvol_storage.c
 * @brief LPC55S16 baremetal non-volatile storage using internal flash.
 */

#include "nvol_storage.h"
#include "spsec_common.h"
#include "fsl_iap.h"

#define FLASH_CONFIG_AREA_START 0x0007C000  /* Last 16KB of 512KB flash */
#define FLASH_CONFIG_AREA_SIZE  0x4000      /* 16KB */
#define FLASH_PAGE_SIZE         256
#define FLASH_SECTOR_SIZE       4096

static const char *logger_name = "nvol_lpc55";

typedef struct {
    char key[64];
    uint32_t len;
    uint8_t data[256];
} nv_entry_t;

static nv_entry_t nv_store[32];
static int nv_count = 0;
static bool nv_initialized = false;

static void nvol_flash_read(void *dst, const void *src, size_t len)
{
    memcpy(dst, src, len);
}

static signed char nvol_flash_write(void *dst, const void *src, size_t len)
{
    status_t status = FLASH_Program(FLASH, (uint32_t)dst, src, len);
    return (status == kStatus_Success) ? 0 : -1;
}

static signed char nvol_flash_erase(void *dst, size_t len)
{
    status_t status = FLASH_Erase(FLASH, (uint32_t)dst, len, kFLASH_ApiEraseKey);
    return (status == kStatus_Success) ? 0 : -1;
}

signed char nvol_storage_init(void)
{
    if (nv_initialized) return 0;

    /* Read existing entries from flash */
    nv_entry_t *flash_entries = (nv_entry_t *)FLASH_CONFIG_AREA_START;
    for (int i = 0; i < 32; i++) {
        if (flash_entries[i].key[0] == '\0') break;
        nv_store[nv_count++] = flash_entries[i];
    }
    nv_initialized = true;
    LOG_INFO(logger_name, "NVOL storage initialized, %d entries loaded", nv_count);
    return 0;
}

void nvol_storage_deinit(void)
{
    nv_initialized = false;
    nv_count = 0;
}

signed char nvol_storage_write(const char *path, const uint8_t *data, size_t len)
{
    if (!nv_initialized) return -1;
    if (!path || !data || len > 256) return -1;

    /* Find or create entry */
    nv_entry_t *entry = NULL;
    for (int i = 0; i < nv_count; i++) {
        if (strcmp(nv_store[i].key, path) == 0) {
            entry = &nv_store[i];
            break;
        }
    }
    if (!entry && nv_count < 32) {
        entry = &nv_store[nv_count++];
        strncpy(entry->key, path, 63);
        entry->key[63] = '\0';
    }
    if (!entry) return -1;  /* No space */

    entry->len = (uint32_t)len;
    memcpy(entry->data, data, len);

    /* Erase and rewrite entire config area */
    if (nvol_flash_erase((void *)FLASH_CONFIG_AREA_START, FLASH_CONFIG_AREA_SIZE) != 0) {
        LOG_ERROR(logger_name, "Flash erase failed");
        return -1;
    }

    if (nvol_flash_write((void *)FLASH_CONFIG_AREA_START, nv_store, sizeof(nv_store)) != 0) {
        LOG_ERROR(logger_name, "Flash write failed");
        return -1;
    }
    return 0;
}

signed char nvol_storage_read(const char *path, uint8_t *data, size_t max_len, size_t *actual_len)
{
    if (!nv_initialized) return -1;
    if (!path || !data) return -1;

    for (int i = 0; i < nv_count; i++) {
        if (strcmp(nv_store[i].key, path) == 0) {
            size_t len = nv_store[i].len;
            if (len > max_len) len = max_len;
            memcpy(data, nv_store[i].data, len);
            if (actual_len) *actual_len = len;
            return 0;
        }
    }
    return -1;  /* Not found */
}

signed char nvol_storage_delete(const char *path)
{
    if (!nv_initialized) return -1;
    if (!path) return -1;

    for (int i = 0; i < nv_count; i++) {
        if (strcmp(nv_store[i].key, path) == 0) {
            memmove(&nv_store[i], &nv_store[i + 1], (nv_count - i - 1) * sizeof(nv_entry_t));
            nv_count--;
            memset(&nv_store[nv_count], 0, sizeof(nv_entry_t));

            /* Rewrite */
            if (nvol_flash_erase((void *)FLASH_CONFIG_AREA_START, FLASH_CONFIG_AREA_SIZE) != 0) return -1;
            if (nvol_flash_write((void *)FLASH_CONFIG_AREA_START, nv_store, sizeof(nv_store)) != 0) return -1;
            return 0;
        }
    }
    return -1;
}

signed char nvol_storage_write_u64(const char *path, uint64_t value)
{
    uint8_t buf[8];
    for (int i = 0; i < 8; i++) buf[i] = (uint8_t)(value >> (i * 8));
    return nvol_storage_write(path, buf, 8);
}

signed char nvol_storage_read_u64(const char *path, uint64_t *value)
{
    uint8_t buf[8];
    size_t len;
    signed char ret = nvol_storage_read(path, buf, 8, &len);
    if (ret != 0 || len != 8) return -1;
    *value = 0;
    for (int i = 0; i < 8; i++) *value |= ((uint64_t)buf[i]) << (i * 8);
    return 0;
}
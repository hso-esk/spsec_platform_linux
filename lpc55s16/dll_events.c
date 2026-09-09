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
 * @file dll_events.c
 * @brief LPC55S16 baremetal CAN DLL events (error counters, overrun).
 */

#include "dll_events.h"
#include "spsec_common.h"
#include "fsl_flexcan.h"

static const char *logger_name = "dll_lpc55";

typedef struct {
    uint32_t rx_overrun_count;
    uint32_t tx_overflow_count;
    uint32_t rx_error_count;
    uint32_t tx_error_count;
    uint32_t bus_off_count;
    uint32_t address_guard_count;
} DllCounters;

static DllCounters dll_counters = {0};

signed char dll_events_init(void *can_handle)
{
    (void)can_handle;
    memset(&dll_counters, 0, sizeof(dll_counters));
    LOG_INFO(logger_name, "DLL events initialized");
    return 0;
}

void dll_events_deinit(void)
{
}

void dll_events_update(void *can_handle)
{
    CAN_Type *base = (CAN_Type *)can_handle;
    if (!base) return;

    /* Read FlexCAN error counters */
    uint32_t esr1 = base->ESR1;
    dll_counters.rx_error_count = (esr1 & CAN_ESR1_RXERRCNT_MASK) >> CAN_ESR1_RXERRCNT_SHIFT;
    dll_counters.tx_error_count = (esr1 & CAN_ESR1_TXERRCNT_MASK) >> CAN_ESR1_TXERRCNT_SHIFT;

    /* Check for bus off */
    if (esr1 & CAN_ESR1_BOFFINT_MASK) {
        dll_counters.bus_off_count++;
        LOG_WARNING(logger_name, "Bus off detected");
        base->ESR1 = CAN_ESR1_BOFFINT_MASK;
    }

    /* Check for RX warning */
    if (esr1 & CAN_ESR1_RXWRN_MASK) {
        LOG_DEBUG(logger_name, "RX warning");
    }

    /* Check for TX warning */
    if (esr1 & CAN_ESR1_TXWRN_MASK) {
        LOG_DEBUG(logger_name, "TX warning");
    }
}

DllCounters *dll_events_get_counters(void)
{
    return &dll_counters;
}

void dll_events_reset_counters(void)
{
    memset(&dll_counters, 0, sizeof(dll_counters));
}

bool dll_events_is_bus_off(void *can_handle)
{
    CAN_Type *base = (CAN_Type *)can_handle;
    if (!base) return true;
    return (base->ESR1 & CAN_ESR1_BOFFINT_MASK) != 0;
}
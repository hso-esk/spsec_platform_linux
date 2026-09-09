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
 * @file can_interface.c
 * @brief LPC55S16-EVK FlexCAN FD implementation of the CAN HAL.
 *
 * Uses MCUXpresso SDK FlexCAN driver. Requires MCUX_SDK_PATH to be set.
 * Reference: LPC55S16 FlexCAN chapter, MCUXpresso SDK flexcan_fd_driver.
 */

#include "communication_interface.h"
#include "spsec_common.h"
#include "spsec_protocol_can.h"
#include "fsl_flexcan.h"
#include "fsl_clock.h"
#include "pin_mux.h"
#include "clock_config.h"

static const char *logger_name = "can_interface_lpc55";

/* FlexCAN base address and clock for LPC55S16 */
#define EXAMPLE_CAN       CAN0
#define EXAMPLE_CAN_CLKSRC kCLOCK_Can0
#define EXAMPLE_CAN_CLK_FREQ CLOCK_GetFreq(kCLOCK_Can0)

/* Message buffer indices */
#define RX_MB_IDX         0
#define TX_MB_IDX         1

static flexcan_handle_t flexcan_handle;
static volatile bool tx_complete = false;
static volatile bool rx_complete = false;
static flexcan_fd_frame_t rx_frame;

static void flexcan_callback(CAN_Type *base, flexcan_handle_t *handle,
                             status_t status, uint32_t result, void *userData)
{
    (void)base;
    (void)handle;
    (void)userData;

    switch (status) {
        case kStatus_FLEXCAN_RxIdle:
            if (result == RX_MB_IDX) {
                rx_complete = true;
            }
            break;
        case kStatus_FLEXCAN_TxIdle:
            if (result == TX_MB_IDX) {
                tx_complete = true;
            }
            break;
        default:
            break;
    }
}

signed char can_channel_init(CommChannel *channel, const char *interface_name,
                             int bitrate, int data_bitrate)
{
    (void)interface_name;  /* Not used on baremetal; single CAN instance */

    channel->bitrate = bitrate;
    channel->data_bitrate = data_bitrate;
    channel->socket = 0;   /* Opaque handle: 0 = uninitialized, 1 = initialized */

    /* Initialize FlexCAN */
    flexcan_config_t config;
    FLEXCAN_GetDefaultConfig(&config);
    config.enableLoopBack = false;
    config.enableSelfWakeup = false;
    config.enableIndividMask = true;
    config.clkSrc = kFLEXCAN_ClkSrcOsc;

    FLEXCAN_Init(EXAMPLE_CAN, &config, EXAMPLE_CAN_CLK_FREQ);

    /* Configure FD bitrate */
    flexcan_fd_bitrate_t fd_bitrate;
    fd_bitrate.preDivider = 0;
    fd_bitrate.divider = 0;
    /* TODO: Calculate timing parameters from bitrate/data_bitrate */
    /* For now, use hardcoded 1M/5M */
    if (FLEXCAN_SetFDBaudRate(EXAMPLE_CAN, 1000000, 5000000, EXAMPLE_CAN_CLK_FREQ) != kStatus_Success) {
        LOG_ERROR(logger_name, "Failed to set FD baud rate");
        return -1;
    }

    /* Enable FD and BRS */
    EXAMPLE_CAN->MCR |= CAN_MCR_FDEN_MASK;
    EXAMPLE_CAN->FDCTRL |= CAN_FDCTRL_BRS_MASK;

    /* Create handle */
    FLEXCAN_TransferCreateHandle(EXAMPLE_CAN, &flexcan_handle, flexcan_callback, NULL);

    /* Setup RX message buffer */
    flexcan_fd_frame_t frame;
    FLEXCAN_SetRxMbConfig(EXAMPLE_CAN, RX_MB_IDX, &frame, true);

    /* Start receiving */
    flexcan_fd_frame_t rx_mb;
    FLEXCAN_TransferReceiveFdNonBlocking(EXAMPLE_CAN, &flexcan_handle, &rx_mb, RX_MB_IDX);

    channel->socket = 1;  /* Mark initialized */
    LOG_INFO(logger_name, "FlexCAN FD initialized: nominal=%d, data=%d", bitrate, data_bitrate);
    return 0;
}

void can_channel_destroy(CommChannel *channel)
{
    if (channel->socket) {
        FLEXCAN_Deinit(EXAMPLE_CAN);
        channel->socket = 0;
    }
}

SPsecMessage *can_channel_receive(CommChannel *channel, int timeout)
{
    (void)channel;
    (void)timeout;

    /* Non-blocking check for received frame */
    if (rx_complete) {
        rx_complete = false;

        AppData *ad = appdata_new(rx_frame.id & CAN_EFF_MASK, rx_frame.data, rx_frame.length);
        if (!ad) return NULL;

        SPsecMessage *msg = spsecmessage_new(MSGTYPE_APP_DATA, ad);
        if (!msg) {
            appdata_free(ad);
            return NULL;
        }
        return msg;
    }
    return NULL;
}

signed char can_channel_receive_frame(CommChannel *channel, CanFrame *out_frame,
                                      int timeout_ms)
{
    (void)channel;
    (void)timeout_ms;

    if (rx_complete) {
        rx_complete = false;
        out_frame->can_id = rx_frame.id & CAN_EFF_MASK;
        out_frame->len = rx_frame.length;
        memset(out_frame->data_ptr, 0, sizeof(out_frame->data_ptr));
        memcpy(out_frame->data_ptr, rx_frame.data, rx_frame.length);
        return 1;
    }
    return 0;  /* Timeout / no frame */
}

signed char channel_send_appdata(CommChannel *channel, AppData *message)
{
    if (!channel || channel->socket == 0 || !message) return -1;
    if (message->data_len > 64) return -1;  /* CAN FD max payload */

    flexcan_fd_frame_t tx_frame = {0};
    tx_frame.id = message->address | CAN_EFF_FLAG;
    tx_frame.length = (uint8_t)message->data_len;
    tx_frame.flags = FLEXCAN_FD_FRAME_BRS;  /* Bit rate switch */
    memcpy(tx_frame.data, message->data_ptr, message->data_len);

    tx_complete = false;
    status_t status = FLEXCAN_TransferSendFdNonBlocking(EXAMPLE_CAN, &flexcan_handle,
                                                         &tx_frame, TX_MB_IDX);
    if (status != kStatus_Success) {
        LOG_ERROR(logger_name, "FlexCAN TX failed: %d", status);
        return -1;
    }

    /* Wait for TX complete (with timeout) */
    uint32_t timeout = 100000;
    while (!tx_complete && timeout--) {
        __NOP();
    }
    if (!tx_complete) {
        LOG_WARNING(logger_name, "TX timeout");
        return -1;
    }
    return 0;
}
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
 * @file led_status.c
 * @brief LPC55S16 baremetal status LED control.
 */

#include "led_status.h"
#include "spsec_common.h"
#include "fsl_gpio.h"
#include "pin_mux.h"

static const char *logger_name = "led_lpc55";

/* LED pins for LPC55S16-EVK */
#define LED_RED_GPIO      GPIO
#define LED_RED_PORT      0
#define LED_RED_PIN       29
#define LED_GREEN_GPIO    GPIO
#define LED_GREEN_PORT    0
#define LED_GREEN_PIN     30
#define LED_BLUE_GPIO     GPIO
#define LED_BLUE_PORT     0
#define LED_BLUE_PIN      14

static gpio_pin_config_t led_config = {
    kGPIO_DigitalOutput, 0,
};

static bool led_initialized = false;

signed char led_status_init(void)
{
    if (led_initialized) return 0;

    /* Initialize GPIO clock */
    CLOCK_EnableClock(kCLOCK_Gpio0);

    /* Init LEDs */
    GPIO_PinInit(LED_RED_GPIO, LED_RED_PORT, LED_RED_PIN, &led_config);
    GPIO_PinInit(LED_GREEN_GPIO, LED_GREEN_PORT, LED_GREEN_PIN, &led_config);
    GPIO_PinInit(LED_BLUE_GPIO, LED_BLUE_PORT, LED_BLUE_PIN, &led_config);

    /* All off initially */
    led_status_set(LED_STATUS_OFF);
    led_initialized = true;
    LOG_INFO(logger_name, "Status LEDs initialized");
    return 0;
}

void led_status_deinit(void)
{
    if (led_initialized) {
        led_status_set(LED_STATUS_OFF);
        led_initialized = false;
    }
}

void led_status_set(LedStatus state)
{
    if (!led_initialized) return;

    switch (state) {
        case LED_STATUS_OFF:
            GPIO_PinWrite(LED_RED_GPIO, LED_RED_PORT, LED_RED_PIN, 1);
            GPIO_PinWrite(LED_GREEN_GPIO, LED_GREEN_PORT, LED_GREEN_PIN, 1);
            GPIO_PinWrite(LED_BLUE_GPIO, LED_BLUE_PORT, LED_BLUE_PIN, 1);
            break;
        case LED_STATUS_WAITING:      /* Red solid */
            GPIO_PinWrite(LED_RED_GPIO, LED_RED_PORT, LED_RED_PIN, 0);
            GPIO_PinWrite(LED_GREEN_GPIO, LED_GREEN_PORT, LED_GREEN_PIN, 1);
            GPIO_PinWrite(LED_BLUE_GPIO, LED_BLUE_PORT, LED_BLUE_PIN, 1);
            break;
        case LED_STATUS_CONFIGURATION: /* Blue solid */
            GPIO_PinWrite(LED_RED_GPIO, LED_RED_PORT, LED_RED_PIN, 1);
            GPIO_PinWrite(LED_GREEN_GPIO, LED_GREEN_PORT, LED_GREEN_PIN, 1);
            GPIO_PinWrite(LED_BLUE_GPIO, LED_BLUE_PORT, LED_BLUE_PIN, 0);
            break;
        case LED_STATUS_SECURE:       /* Green solid */
            GPIO_PinWrite(LED_RED_GPIO, LED_RED_PORT, LED_RED_PIN, 1);
            GPIO_PinWrite(LED_GREEN_GPIO, LED_GREEN_PORT, LED_GREEN_PIN, 0);
            GPIO_PinWrite(LED_BLUE_GPIO, LED_BLUE_PORT, LED_BLUE_PIN, 1);
            break;
        case LED_STATUS_WARNING:      /* Yellow solid (red+green) */
            GPIO_PinWrite(LED_RED_GPIO, LED_RED_PORT, LED_RED_PIN, 0);
            GPIO_PinWrite(LED_GREEN_GPIO, LED_GREEN_PORT, LED_GREEN_PIN, 0);
            GPIO_PinWrite(LED_BLUE_GPIO, LED_BLUE_PORT, LED_BLUE_PIN, 1);
            break;
        case LED_STATUS_ERROR:        /* Red blink */
            GPIO_PinWrite(LED_RED_GPIO, LED_RED_PORT, LED_RED_PIN, 0);
            GPIO_PinWrite(LED_GREEN_GPIO, LED_GREEN_PORT, LED_GREEN_PIN, 1);
            GPIO_PinWrite(LED_BLUE_GPIO, LED_BLUE_PORT, LED_BLUE_PIN, 1);
            break;
        case LED_STATUS_TIMESYNC:     /* Blue blink */
            GPIO_PinWrite(LED_RED_GPIO, LED_RED_PORT, LED_RED_PIN, 1);
            GPIO_PinWrite(LED_GREEN_GPIO, LED_GREEN_PORT, LED_GREEN_PIN, 1);
            GPIO_PinWrite(LED_BLUE_GPIO, LED_BLUE_PORT, LED_BLUE_PIN, 0);
            break;
        case LED_STATUS_ACTIVITY:     /* Green blink */
            GPIO_PinWrite(LED_RED_GPIO, LED_RED_PORT, LED_RED_PIN, 1);
            GPIO_PinWrite(LED_GREEN_GPIO, LED_GREEN_PORT, LED_GREEN_PIN, 0);
            GPIO_PinWrite(LED_BLUE_GPIO, LED_BLUE_PORT, LED_BLUE_PIN, 1);
            break;
    }
}

void led_status_blink(LedStatus state, uint32_t duration_ms)
{
    led_status_set(state);
    /* Simple busy-wait blink - in production use timer/RTOS */
    for (uint32_t i = 0; i < duration_ms * 1000; i++) {
        __NOP();
    }
    led_status_set(LED_STATUS_OFF);
}
/* Five-button front-panel UI for ESP32 NAT Router.
 *
 * Drives Up / Down / Left / Right / Select buttons polled from a dedicated
 * low-priority task.  Actions are dispatched to the OLED page controller and
 * to a small set of router operations (show status, restart, factory reset).
 *
 * All five GPIOs come from NVS ("btn_up", "btn_down", "btn_left", "btn_right",
 * "btn_sel") and the feature is disabled unless btn_sel is configured, so
 * boards without buttons allocate nothing.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Button events delivered to the UI handler. */
typedef enum {
    BTN_NONE = 0,
    BTN_UP,
    BTN_DOWN,
    BTN_LEFT,
    BTN_RIGHT,
    BTN_SELECT,
    BTN_SELECT_LONG,   /* Select held past BUTTON_LONG_PRESS_MS */
} button_event_t;

/* Logical screen the UI is currently showing. */
typedef enum {
    UI_SCREEN_STATUS = 0,   /* default: SSID / IP / clients */
    UI_SCREEN_CLIENTS,      /* connected client list */
    UI_SCREEN_ANTENNA,      /* antenna selector */
    UI_SCREEN_SYSTEM,       /* heap / uptime / temperature */
    UI_SCREEN_COUNT
} ui_screen_t;

#define BUTTON_LONG_PRESS_MS 1500
#define BUTTON_POLL_MS       20
#define BUTTON_DEBOUNCE_MS   40

/**
 * @brief Initialise buttons from NVS and start the polling task.
 *
 * Does nothing (and allocates nothing) when btn_sel is not configured.
 */
void button_ui_init(void);

/**
 * @brief Configure the five button GPIOs. -1 disables the feature.
 * Persisted to NVS; takes effect after restart.
 */
void button_ui_set_gpio(int up, int down, int left, int right, int sel);

/**
 * @brief Read the configured button GPIOs.
 * @return true if buttons are enabled.
 */
bool button_ui_get_gpio(int *up, int *down, int *left, int *right, int *sel);

/**
 * @brief Current UI screen, for the display renderer.
 */
ui_screen_t button_ui_screen(void);

/**
 * @brief Index selected within the current screen (e.g. antenna number).
 */
int button_ui_cursor(void);

/**
 * @brief Ask the UI to redraw now instead of waiting for the next tick.
 */
void button_ui_request_redraw(void);

#ifdef __cplusplus
}
#endif

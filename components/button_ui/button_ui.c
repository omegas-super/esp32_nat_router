/* Five-button front-panel UI for ESP32 NAT Router.
 *
 * Buttons are polled at BUTTON_POLL_MS and debounced by requiring a stable
 * level for BUTTON_DEBOUNCE_MS.  Nothing here allocates per poll, and the
 * whole task is not created at all when btn_sel is unconfigured.
 *
 * SPDX-License-Identifier: MIT
 */

#include "button_ui.h"
#include "router_config.h"

#include <string.h>
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "button_ui";

#define PARAM_NAMESPACE "esp32_nat"

static int btn_gpio[5] = { -1, -1, -1, -1, -1 };  /* up, down, left, right, sel */
static bool buttons_enabled = false;

/* Debounce state: last raw level and how long it has been stable. */
typedef struct {
    int last_level;
    int stable_ms;
} btn_state_t;

static btn_state_t btn_state[5];

static volatile ui_screen_t current_screen = UI_SCREEN_STATUS;
static volatile int current_cursor = 0;
static volatile bool redraw_requested = true;

static TaskHandle_t ui_task_handle = NULL;

/* ---- NVS ---- */

void button_ui_set_gpio(int up, int down, int left, int right, int sel)
{
    nvs_handle_t nvs;
    if (nvs_open(PARAM_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK)
        return;

    nvs_set_i32(nvs, "btn_up", up);
    nvs_set_i32(nvs, "btn_down", down);
    nvs_set_i32(nvs, "btn_left", left);
    nvs_set_i32(nvs, "btn_right", right);
    nvs_set_i32(nvs, "btn_sel", sel);
    nvs_commit(nvs);
    nvs_close(nvs);
}

bool button_ui_get_gpio(int *up, int *down, int *left, int *right, int *sel)
{
    nvs_handle_t nvs;
    int32_t v;

    if (up) *up = -1;
    if (down) *down = -1;
    if (left) *left = -1;
    if (right) *right = -1;
    if (sel) *sel = -1;

    if (nvs_open(PARAM_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK)
        return false;

    if (up && nvs_get_i32(nvs, "btn_up", &v) == ESP_OK) *up = (int)v;
    if (down && nvs_get_i32(nvs, "btn_down", &v) == ESP_OK) *down = (int)v;
    if (left && nvs_get_i32(nvs, "btn_left", &v) == ESP_OK) *left = (int)v;
    if (right && nvs_get_i32(nvs, "btn_right", &v) == ESP_OK) *right = (int)v;
    if (sel && nvs_get_i32(nvs, "btn_sel", &v) == ESP_OK) *sel = (int)v;

    nvs_close(nvs);

    /* The Select pin is the sentinel: no Select, no UI. */
    return (sel != NULL && *sel >= 0);
}

/* ---- UI state accessors ---- */

ui_screen_t button_ui_screen(void) { return current_screen; }
int button_ui_cursor(void) { return current_cursor; }
void button_ui_request_redraw(void) { redraw_requested = true; }

/* ---- Event handling ---- */

static const char *screen_name(ui_screen_t s);

static void handle_event(button_event_t ev)
{
    switch (ev) {
    case BTN_UP:
        if (current_screen == UI_SCREEN_ANTENNA) {
            if (current_cursor > 0) current_cursor--;
        } else {
            current_screen = (ui_screen_t)((current_screen + UI_SCREEN_COUNT - 1) % UI_SCREEN_COUNT);
        }
        redraw_requested = true;
        break;

    case BTN_DOWN:
        if (current_screen == UI_SCREEN_ANTENNA) {
            if (current_cursor + 1 < (int)antenna_count) current_cursor++;
        } else {
            current_screen = (ui_screen_t)((current_screen + 1) % UI_SCREEN_COUNT);
        }
        redraw_requested = true;
        break;

    case BTN_LEFT:
        current_screen = (ui_screen_t)((current_screen + UI_SCREEN_COUNT - 1) % UI_SCREEN_COUNT);
        redraw_requested = true;
        break;

    case BTN_RIGHT:
        current_screen = (ui_screen_t)((current_screen + 1) % UI_SCREEN_COUNT);
        redraw_requested = true;
        break;

    case BTN_SELECT:
        if (current_screen == UI_SCREEN_ANTENNA && antenna_gpio >= 0) {
            esp_err_t err = antenna_select((uint8_t)current_cursor);
            ESP_LOGI(TAG, "Antenna select %d -> %s", current_cursor, esp_err_to_name(err));
            redraw_requested = true;
        }
        break;

    case BTN_SELECT_LONG:
        /* Long-press Select on the System screen restarts the router. */
        if (current_screen == UI_SCREEN_SYSTEM) {
            ESP_LOGW(TAG, "Long press: restarting");
            restart_after(2);
        }
        break;

    default:
        break;
    }

    /* Log after the state change so the screen/cursor numbers are current.
     * The console is the only way to verify the pin map on a board whose
     * buttons cannot be traced by eye. */
    static const char *ev_name[] = {
        "none", "up", "down", "left", "right", "select", "select-long"
    };
    if (ev > BTN_NONE && ev <= BTN_SELECT_LONG) {
        ESP_LOGI(TAG, "press %s -> screen %s cursor %d",
                 ev_name[ev], screen_name(current_screen), current_cursor);
    }
}

/* ---- Polling task ---- */

static const char *screen_name(ui_screen_t s)
{
    switch (s) {
    case UI_SCREEN_STATUS:  return "status";
    case UI_SCREEN_CLIENTS: return "clients";
    case UI_SCREEN_ANTENNA: return "antenna";
    case UI_SCREEN_SYSTEM:  return "system";
    default:                return "?";
    }
}

static void button_ui_task(void *arg)
{
    (void)arg;

    for (int i = 0; i < 5; i++) {
        gpio_reset_pin((gpio_num_t)btn_gpio[i]);
        gpio_set_direction((gpio_num_t)btn_gpio[i], GPIO_MODE_INPUT);
        gpio_set_pull_mode((gpio_num_t)btn_gpio[i], GPIO_PULLUP_ONLY);
        btn_state[i].last_level = 1;   /* pulled up = released */
        btn_state[i].stable_ms = 0;
    }

    ESP_LOGI(TAG, "UI buttons: up=%d down=%d left=%d right=%d sel=%d",
             btn_gpio[0], btn_gpio[1], btn_gpio[2], btn_gpio[3], btn_gpio[4]);

    int sel_held_ms = 0;
    bool sel_long_fired = false;

    while (true) {
        for (int i = 0; i < 5; i++) {
            int level = gpio_get_level((gpio_num_t)btn_gpio[i]);

            if (level != btn_state[i].last_level) {
                btn_state[i].stable_ms += BUTTON_POLL_MS;
                if (btn_state[i].stable_ms >= BUTTON_DEBOUNCE_MS) {
                    btn_state[i].last_level = level;
                    btn_state[i].stable_ms = 0;

                    /* Active-low buttons: a press is level 0. */
                    if (level == 0) {
                        static const button_event_t press_ev[5] = {
                            BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_SELECT
                        };
                        handle_event(press_ev[i]);
                    }
                }
            } else {
                btn_state[i].stable_ms = 0;
            }
        }

        /* Select long-press detection (index 4).  Only meaningful on the
         * System screen, where it is the restart gesture. */
        if (btn_state[4].last_level == 0 && current_screen == UI_SCREEN_SYSTEM) {
            sel_held_ms += BUTTON_POLL_MS;
            if (!sel_long_fired && sel_held_ms >= BUTTON_LONG_PRESS_MS) {
                sel_long_fired = true;
                handle_event(BTN_SELECT_LONG);
            }
        } else {
            sel_held_ms = 0;
            sel_long_fired = false;
        }

        vTaskDelay(pdMS_TO_TICKS(BUTTON_POLL_MS));
    }
}

/* ---- Public init ---- */

void button_ui_init(void)
{
    int up, down, left, right, sel;

    if (!button_ui_get_gpio(&up, &down, &left, &right, &sel)) {
        ESP_LOGI(TAG, "Button UI disabled (btn_sel not configured)");
        return;
    }

    /* Reject input-only pins (34-39) and the flash bus (6-11) early, with a
     * clear message instead of a silent non-working button. */
    const int pins[5] = { up, down, left, right, sel };
    for (int i = 0; i < 5; i++) {
        if (pins[i] < 0) continue;
        if (pins[i] >= 6 && pins[i] <= 11) {
            ESP_LOGE(TAG, "Button GPIO%d is on the SPI flash bus - refusing", pins[i]);
            return;
        }
        if (pins[i] >= 34 && pins[i] <= 39) {
            ESP_LOGE(TAG, "Button GPIO%d is input-only with no pull-up - refusing", pins[i]);
            return;
        }
    }

    btn_gpio[0] = up;
    btn_gpio[1] = down;
    btn_gpio[2] = left;
    btn_gpio[3] = right;
    btn_gpio[4] = sel;
    buttons_enabled = true;

    xTaskCreate(button_ui_task, "button_ui", 3072, NULL, 2, &ui_task_handle);
}

/* Keep the compiler quiet when a target has no antenna code linked. */
static void __attribute__((unused)) ui_touch(void)
{
    (void)buttons_enabled;
    (void)screen_name;
}

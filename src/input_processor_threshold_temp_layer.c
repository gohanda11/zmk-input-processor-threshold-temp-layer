/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_input_processor_threshold_temp_layer

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <math.h>

#include <zephyr/dt-bindings/input/input-event-codes.h>

#include <zmk/keymap.h>
#include <zmk/input_processors.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/events/layer_state_changed.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

// Configuration for each processor instance
struct threshold_temp_layer_config {
    uint8_t layer;
    int16_t timeout;
    int activation_threshold;  // NEW: activation threshold in pixels
    int16_t require_prior_idle_ms;
    uint8_t excluded_positions_len;
    uint8_t excluded_positions[];
};

// State data for each processor instance
struct threshold_temp_layer_data {
    int32_t accumulated_distance;  // NEW: accumulated movement distance
    bool layer_active;
    int64_t last_tap_time;
    struct k_work_delayable layer_disable_work;
};

// NEW: Calculate Euclidean distance from dx, dy
static int calculate_distance(int dx, int dy) {
    // Using integer approximation: sqrt(dx^2 + dy^2)
    // For better performance, we can use: max(|dx|, |dy|) + min(|dx|, |dy|)/2
    int abs_dx = abs(dx);
    int abs_dy = abs(dy);
    int max_val = (abs_dx > abs_dy) ? abs_dx : abs_dy;
    int min_val = (abs_dx > abs_dy) ? abs_dy : abs_dx;
    return max_val + (min_val >> 1);  // Approximation
}

// Work handler to disable the layer after timeout
static void layer_disable_work_handler(struct k_work *work) {
    struct k_work_delayable *d_work = k_work_delayable_from_work(work);
    struct threshold_temp_layer_data *data =
        CONTAINER_OF(d_work, struct threshold_temp_layer_data, layer_disable_work);

    const struct device *dev = DEVICE_DT_INST_GET(0);
    const struct threshold_temp_layer_config *cfg = dev->config;

    if (data->layer_active) {
        data->layer_active = false;
        data->accumulated_distance = 0;  // NEW: Reset accumulated distance
        zmk_keymap_layer_deactivate(cfg->layer);
    }
}

// Process input events
static int threshold_temp_layer_handle_event(const struct device *dev,
                                            struct input_event *event,
                                            uint32_t param1, uint32_t param2,
                                            struct zmk_input_processor_state *state) {
    const struct threshold_temp_layer_config *cfg = dev->config;
    struct threshold_temp_layer_data *data = dev->data;

    // Check if we should ignore activation due to recent key press
    if (!data->layer_active && cfg->require_prior_idle_ms > 0) {
        int64_t now = k_uptime_get();
        if ((now - data->last_tap_time) < cfg->require_prior_idle_ms) {
            return 0;  // Don't activate yet
        }
    }

    // Process relative movement events (REL_X, REL_Y)
    if (event->type == INPUT_EV_REL) {
        if (event->code == INPUT_REL_X || event->code == INPUT_REL_Y) {
            static int pending_dx = 0;
            static int pending_dy = 0;

            if (event->code == INPUT_REL_X) {
                pending_dx = event->value;
            } else if (event->code == INPUT_REL_Y) {
                pending_dy = event->value;
            }

            // NEW: Accumulate distance when layer is not active
            if (!data->layer_active) {
                int distance = calculate_distance(pending_dx, pending_dy);
                data->accumulated_distance += distance;

                // Check if accumulated distance exceeds threshold
                if (data->accumulated_distance >= cfg->activation_threshold) {
                    data->layer_active = true;
                    zmk_keymap_layer_activate(cfg->layer);
                }
            }

            // Reset pending values
            pending_dx = 0;
            pending_dy = 0;

            // Reschedule deactivation timer if layer is active
            if (data->layer_active && cfg->timeout > 0) {
                k_work_reschedule(&data->layer_disable_work, K_MSEC(cfg->timeout));
            }
        }
    }

    return 0;
}

// Event listener for key presses (to track last_tap_time)
static int handle_position_state_changed(const struct zmk_event_header *eh) {
    struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
    if (ev == NULL) {
        return 0;
    }

    const struct device *dev = DEVICE_DT_INST_GET(0);
    const struct threshold_temp_layer_config *cfg = dev->config;
    struct threshold_temp_layer_data *data = dev->data;

    // Check if this position is excluded
    for (int i = 0; i < cfg->excluded_positions_len; i++) {
        if (cfg->excluded_positions[i] == ev->position) {
            return 0;  // Excluded position, don't update timer
        }
    }

    // Update last tap time
    if (ev->state) {  // Key pressed
        data->last_tap_time = k_uptime_get();

        // If layer is active and this is not an excluded position, deactivate
        if (data->layer_active) {
            data->layer_active = false;
            data->accumulated_distance = 0;  // NEW: Reset on key press
            k_work_cancel_delayable(&data->layer_disable_work);
            zmk_keymap_layer_deactivate(cfg->layer);
        }
    }

    return 0;
}

ZMK_LISTENER(threshold_temp_layer, handle_position_state_changed);
ZMK_SUBSCRIPTION(threshold_temp_layer, zmk_position_state_changed);

// Initialization
static int threshold_temp_layer_init(const struct device *dev) {
    struct threshold_temp_layer_data *data = dev->data;

    data->accumulated_distance = 0;
    data->layer_active = false;
    data->last_tap_time = 0;

    k_work_init_delayable(&data->layer_disable_work, layer_disable_work_handler);

    return 0;
}

// Device instantiation macro
#define THRESHOLD_TEMP_LAYER_INST(n)                                                      \
    static uint8_t excluded_positions_##n[] = DT_INST_PROP(n, excluded_positions);       \
    static const struct threshold_temp_layer_config threshold_temp_layer_config_##n = {  \
        .layer = DT_INST_PROP_BY_PHANDLE_IDX(n, DT_DRV_COMPAT, 0, layer),               \
        .timeout = DT_INST_PROP_BY_PHANDLE_IDX(n, DT_DRV_COMPAT, 1, timeout),           \
        .activation_threshold = DT_INST_PROP(n, activation_threshold),                   \
        .require_prior_idle_ms = DT_INST_PROP(n, require_prior_idle_ms),                \
        .excluded_positions_len = DT_INST_PROP_LEN(n, excluded_positions),              \
        .excluded_positions = excluded_positions_##n,                                    \
    };                                                                                    \
    static struct threshold_temp_layer_data threshold_temp_layer_data_##n = {};          \
    DEVICE_DT_INST_DEFINE(n, threshold_temp_layer_init, NULL,                           \
                          &threshold_temp_layer_data_##n,                                \
                          &threshold_temp_layer_config_##n,                              \
                          POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,             \
                          NULL);                                                         \
    ZMK_INPUT_PROCESSOR_DEFINE(n, threshold_temp_layer_handle_event);

DT_INST_FOREACH_STATUS_OKAY(THRESHOLD_TEMP_LAYER_INST)

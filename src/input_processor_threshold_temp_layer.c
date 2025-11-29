/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_input_processor_threshold_temp_layer

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zephyr/dt-bindings/input/input-event-codes.h>

#include <zmk/keymap.h>
#include <zmk/input_processors.h>
#include <zmk/events/position_state_changed.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define MAX_LAYERS 16

// Configuration for each processor instance
struct threshold_temp_layer_config {
    int16_t require_prior_idle_ms;
    uint8_t excluded_positions_len;
    uint8_t excluded_positions[];
};

// State data for each layer
struct threshold_temp_layer_layer_data {
    int32_t accumulated_distance;
    bool active;
    struct k_work_delayable disable_work;
};

// State data for each processor instance
struct threshold_temp_layer_data {
    int64_t last_tap_time;
    struct threshold_temp_layer_layer_data layers[MAX_LAYERS];
};

// Calculate distance from dx, dy using integer approximation
static int calculate_distance(int dx, int dy) {
    int abs_dx = abs(dx);
    int abs_dy = abs(dy);
    int max_val = (abs_dx > abs_dy) ? abs_dx : abs_dy;
    int min_val = (abs_dx > abs_dy) ? abs_dy : abs_dx;
    return max_val + (min_val >> 1);
}

// Work handler to disable layer after timeout
static void layer_disable_work_handler(struct k_work *work) {
    struct k_work_delayable *d_work = k_work_delayable_from_work(work);
    struct threshold_temp_layer_layer_data *layer_data =
        CONTAINER_OF(d_work, struct threshold_temp_layer_layer_data, disable_work);

    // Find which layer this is
    for (int i = 0; i < MAX_LAYERS; i++) {
        const struct device *dev = DEVICE_DT_INST_GET(0);
        struct threshold_temp_layer_data *data = dev->data;

        if (&data->layers[i] == layer_data && layer_data->active) {
            layer_data->active = false;
            layer_data->accumulated_distance = 0;
            zmk_keymap_layer_deactivate(i);
            break;
        }
    }
}

// Process input events
static int threshold_temp_layer_handle_event(const struct device *dev,
                                            struct input_event *event,
                                            uint32_t param1, uint32_t param2, uint32_t param3,
                                            struct zmk_input_processor_state *state) {
    struct threshold_temp_layer_data *data = dev->data;
    const struct threshold_temp_layer_config *cfg = dev->config;

    uint8_t layer = (uint8_t)param1;
    int16_t timeout = (int16_t)param2;
    int activation_threshold = (int)param3;

    if (layer >= MAX_LAYERS) {
        return 0;
    }

    struct threshold_temp_layer_layer_data *layer_data = &data->layers[layer];

    // Check if we should ignore activation due to recent key press
    if (!layer_data->active && cfg->require_prior_idle_ms > 0) {
        int64_t now = k_uptime_get();
        if ((now - data->last_tap_time) < cfg->require_prior_idle_ms) {
            return 0;
        }
    }

    // Process relative movement events
    if (event->type == INPUT_EV_REL) {
        if (event->code == INPUT_REL_X || event->code == INPUT_REL_Y) {
            static int pending_dx = 0;
            static int pending_dy = 0;

            if (event->code == INPUT_REL_X) {
                pending_dx = event->value;
            } else if (event->code == INPUT_REL_Y) {
                pending_dy = event->value;
            }

            // Accumulate distance when layer is not active
            if (!layer_data->active) {
                int distance = calculate_distance(pending_dx, pending_dy);
                layer_data->accumulated_distance += distance;

                // Check if threshold is reached
                if (layer_data->accumulated_distance >= activation_threshold) {
                    layer_data->active = true;
                    zmk_keymap_layer_activate(layer);
                }
            }

            // Reset pending values
            pending_dx = 0;
            pending_dy = 0;

            // Reschedule timeout if layer is active
            if (layer_data->active && timeout > 0) {
                k_work_reschedule(&layer_data->disable_work, K_MSEC(timeout));
            }
        }
    }

    return 0;
}

// Event listener for key presses
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
            return 0;
        }
    }

    // Update last tap time
    if (ev->state) {
        data->last_tap_time = k_uptime_get();

        // Deactivate all active layers
        for (int i = 0; i < MAX_LAYERS; i++) {
            if (data->layers[i].active) {
                data->layers[i].active = false;
                data->layers[i].accumulated_distance = 0;
                k_work_cancel_delayable(&data->layers[i].disable_work);
                zmk_keymap_layer_deactivate(i);
            }
        }
    }

    return 0;
}

ZMK_LISTENER(threshold_temp_layer, handle_position_state_changed);
ZMK_SUBSCRIPTION(threshold_temp_layer, zmk_position_state_changed);

// Initialization
static int threshold_temp_layer_init(const struct device *dev) {
    struct threshold_temp_layer_data *data = dev->data;

    data->last_tap_time = 0;

    for (int i = 0; i < MAX_LAYERS; i++) {
        data->layers[i].accumulated_distance = 0;
        data->layers[i].active = false;
        k_work_init_delayable(&data->layers[i].disable_work, layer_disable_work_handler);
    }

    return 0;
}

// Device instantiation
#define THRESHOLD_TEMP_LAYER_INST(n)                                                         \
    static uint8_t excluded_positions_##n[] = DT_INST_PROP(n, excluded_positions);          \
    static const struct threshold_temp_layer_config threshold_temp_layer_config_##n = {     \
        .require_prior_idle_ms = DT_INST_PROP(n, require_prior_idle_ms),                   \
        .excluded_positions_len = DT_INST_PROP_LEN(n, excluded_positions),                 \
        .excluded_positions = {[0 ...(DT_INST_PROP_LEN(n, excluded_positions) - 1)] = 0},  \
    };                                                                                       \
    static struct threshold_temp_layer_data threshold_temp_layer_data_##n = {};             \
    DEVICE_DT_INST_DEFINE(n, threshold_temp_layer_init, NULL,                              \
                          &threshold_temp_layer_data_##n,                                   \
                          &threshold_temp_layer_config_##n,                                 \
                          POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, NULL);         \
    ZMK_INPUT_PROCESSOR_DEFINE(n, threshold_temp_layer_handle_event);

DT_INST_FOREACH_STATUS_OKAY(THRESHOLD_TEMP_LAYER_INST)

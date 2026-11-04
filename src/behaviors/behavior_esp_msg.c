/*
 * SPDX-License-Identifier: MIT
 *
 * &esp_msg N: broadcasts message ID N (0-255) in a short burst of BLE advertisements.
 * Uses its own non-connectable advertising set (legacy PDUs, so a BT 4.2 ESP32 can see it)
 * so it doesn't touch ZMK's own advertising, the USB output or the split connections.
 *
 * Manufacturer data: FF FF 'Z' 'K' <id> <seq>
 */

#define DT_DRV_COMPAT zmk_behavior_esp_msg

#include <zephyr/device.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <drivers/behavior.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/behavior.h>

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static struct bt_le_ext_adv *esp_adv;
static uint8_t seq;

// 0xFFFF = company ID reserved for testing
static uint8_t mfg_data[] = {0xFF, 0xFF, 'Z', 'K', 0x00, 0x00};

static const struct bt_data ad[] = {
    BT_DATA(BT_DATA_MANUFACTURER_DATA, mfg_data, sizeof(mfg_data)),
};

#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)

static const struct behavior_parameter_value_metadata param_values[] = {
    {
        .display_name = "Message",
        .type = BEHAVIOR_PARAMETER_VALUE_TYPE_RANGE,
        .range = {.min = 0, .max = 255},
    },
};

static const struct behavior_parameter_metadata_set param_metadata_set[] = {{
    .param1_values = param_values,
    .param1_values_len = ARRAY_SIZE(param_values),
}};

static const struct behavior_parameter_metadata metadata = {
    .sets_len = ARRAY_SIZE(param_metadata_set),
    .sets = param_metadata_set,
};

#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)

static int esp_adv_create(void) {
    // Non-connectable, non-scannable, legacy advertising at 20-30ms
    struct bt_le_adv_param param =
        BT_LE_ADV_PARAM_INIT(BT_LE_ADV_OPT_NONE, 0x20, 0x30, NULL);

    return bt_le_ext_adv_create(&param, NULL, &esp_adv);
}

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    int err;

    if (esp_adv == NULL) {
        err = esp_adv_create();
        if (err) {
            LOG_ERR("Failed to create ESP adv set: %d", err);
            return ZMK_BEHAVIOR_OPAQUE;
        }
    }

    // Restart the burst if the previous one is still running
    bt_le_ext_adv_stop(esp_adv);

    mfg_data[4] = (uint8_t)binding->param1;
    mfg_data[5] = ++seq;

    err = bt_le_ext_adv_set_data(esp_adv, ad, ARRAY_SIZE(ad), NULL, 0);
    if (err) {
        LOG_ERR("Failed to set ESP adv data: %d", err);
        return ZMK_BEHAVIOR_OPAQUE;
    }

    // Timeout is in units of 10ms
    err = bt_le_ext_adv_start(
        esp_adv, BT_LE_EXT_ADV_START_PARAM(CONFIG_ZMK_BEHAVIOR_ESP_MSG_DURATION_MS / 10, 0));
    if (err) {
        LOG_ERR("Failed to start ESP adv: %d", err);
    } else {
        LOG_DBG("ESP msg %d (seq %d)", mfg_data[4], mfg_data[5]);
    }

    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_esp_msg_driver_api = {
    .binding_pressed = on_keymap_binding_pressed,
    .binding_released = on_keymap_binding_released,
    .locality = BEHAVIOR_LOCALITY_CENTRAL,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .parameter_metadata = &metadata,
#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
};

BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_esp_msg_driver_api);

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */

// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include "storage_component.h"

#include <cstdint>

#include "esp_spiffs.h"
#include "esp_log.h"
#include "nvs.h"
#include "esp_err.h"
#include "led_component.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <nvs_flash.h>
#include <cstring>

#define NVS_VALUE_MAX_LEN 4000

static const char *TAG = "storage_component";

//  ____  ____ ___ _____ _____ ____
// / ___||  _ \_ _|  ___|  ___/ ___|
// \___ \| |_) | || |_  | |_  \___ \
//  ___) |  __/| ||  _| |  _|  ___) |
// |____/|_|  |___|_|   |_|   |____/
//
// (https://www.asciiart.eu/text-to-ascii-art --> Standard)

static bool spiffs_storage_init(void) {
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = nullptr,
        .max_files = 5,
        .format_if_mount_failed = true};

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(ret));
        return false;
    }

    return true;
}

bool storage_load_s2_template(const char *path, char *output, size_t max_len) {
    if (output == nullptr || max_len == 0) {
        return false;
    }

    FILE *f = fopen(path, "r");
    if (!f) {
        return false;
    }

    size_t len = fread(output, 1, max_len - 1, f);
    output[len] = '\0';

    fclose(f);
    return true;
}

bool storage_save_s2_template(const char *path, const char *template_data) {
    if (template_data == nullptr) {
        return false;
    }

    FILE *f = fopen(path, "w");
    if (!f) {
        return false;
    }

    fprintf(f, "%s", template_data);
    fclose(f);
    return true;
}

//  _   ___     ______
// | \ | \ \   / / ___|
// |  \| |\ \ / /\___ \
// | |\  | \ V /  ___) |
// |_| \_|  \_/  |____/
//

// The name of the NVS namespace in which we store our variables.
// Note: ESP-IDF imposes a maximum of 15 characters for this name.
static const char *NVS_NAMESPACE = "config";

static bool nvs_storage_init() {
    esp_err_t err = nvs_flash_init();

    // If the NVS partition has no free pages (e.g. after an OTA update or if it is full),
    // or if a new version has been found, we erase the partition and try again.
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        err = nvs_flash_erase();
        if (err != ESP_OK) {
            return false;
        }
        err = nvs_flash_init();
    }

    return (err == ESP_OK);
}

bool storage_set_configuration(const char *key, uint16_t value) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &my_handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS error %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_set_u16(my_handle, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(my_handle);
    }
    else {
        ESP_LOGE(TAG, "NVS set u16 failed! %s", esp_err_to_name(err));
    }

    nvs_close(my_handle);
    return (err == ESP_OK);
}

bool storage_set_configuration(const char *key, uint32_t value) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &my_handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS error %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_set_u32(my_handle, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(my_handle);
    }
    else {
        ESP_LOGE(TAG, "NVS set u32 failed! %s", esp_err_to_name(err));
    }

    nvs_close(my_handle);
    return (err == ESP_OK);
}

bool storage_set_configuration(const char *key, const char *value) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &my_handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS error %s", esp_err_to_name(err));
        return false;
    }

    size_t existing_size = 0;
    err = nvs_get_str(my_handle, key, nullptr, &existing_size);
    if (err == ESP_OK && existing_size == strnlen(value, NVS_VALUE_MAX_LEN) + 1) {
        char *existing_value = (char *)malloc(existing_size);
        if (existing_value != nullptr) {
            size_t read_size = existing_size;
            err = nvs_get_str(my_handle, key, existing_value, &read_size);
            if (err == ESP_OK && strcmp(existing_value, value) == 0) {
                free(existing_value);
                nvs_close(my_handle);
                ESP_LOGI(TAG, "NVS key '%s' already contains the requested value, skipping write", key);
                return true;
            }
            free(existing_value);
        }
        else {
            ESP_LOGW(TAG, "Unable to allocate buffer to compare existing NVS value for key '%s'", key);
        }
    }
    else if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Unable to read existing NVS value for key '%s': %s", key, esp_err_to_name(err));
    }

    err = nvs_set_str(my_handle, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(my_handle);
    }
    else {
        ESP_LOGE(TAG, "NVS set str failed! %s", esp_err_to_name(err));
    }

    nvs_close(my_handle);
    return (err == ESP_OK);
}

bool storage_get_configuration(const char *key, uint16_t &value, uint16_t default_value) {
    nvs_handle_t my_handle;

    // We open NVS in READWRITE, because we might have to write the default values
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS error %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_get_u16(my_handle, key, &value);

    // If the key is not present yet, write the default
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        value = default_value;

        // Store key-value pair
        err = nvs_set_u16(my_handle, key, default_value);
        if (err == ESP_OK) {
            err = nvs_commit(my_handle);
        }
    }

    nvs_close(my_handle);
    return (err == ESP_OK);
}

bool storage_get_configuration(const char *key, uint32_t &value, uint32_t default_value) {
    nvs_handle_t my_handle;

    // We open NVS in READWRITE, because we might have to write the default values
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS error %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_get_u32(my_handle, key, &value);

    // If the key is not present yet, write the default
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        value = default_value;

        // Store key-value pair
        err = nvs_set_u32(my_handle, key, default_value);
        if (err == ESP_OK) {
            err = nvs_commit(my_handle);
        }
    }

    nvs_close(my_handle);
    return (err == ESP_OK);
}

bool storage_get_configuration(const char *key, char *value, size_t max_len, const char *default_value) {
    nvs_handle_t my_handle;

    // We open NVS in READWRITE, because we might have to write the default values
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS error %s", esp_err_to_name(err));
        return false;
    }

    size_t required_size = max_len;
    err = nvs_get_str(my_handle, key, value, &required_size);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        // Copy default string to buffer with max_len
        if (default_value != nullptr) {
            snprintf(value, max_len, "%s", default_value);
            value[max_len - 1] = '\0'; // Force null termination

            // Store key-value pair
            err = nvs_set_str(my_handle, key, default_value);
            if (err == ESP_OK) {
                err = nvs_commit(my_handle);
            }
        }
        else {
            // Empty string if default is empty
            value[0] = '\0';
            err = ESP_OK; // Treat this as successfully handled
        }
    }

    // Check for buffer overrun
    if (err == ESP_ERR_NVS_INVALID_LENGTH) {
        ESP_LOGE(TAG, "The JSON-template in NVS is larger (%d bytes) than the buffer of %d bytes can handle!", required_size, max_len);
    }

    nvs_close(my_handle);
    return (err == ESP_OK);
}

bool init_storage_component() {
    ESP_LOGW(TAG, "Starting component...");

    // ------------------------------------------------
    // Init storage
    // ------------------------------------------------
    if (!spiffs_storage_init()) {
        ESP_LOGE(TAG, "SPIFFS Storage init failed!");
        set_LED_state(LED_STATE_FATAL_ERROR);
        return false;
    }

    if (!nvs_storage_init()) {
        ESP_LOGE(TAG, "NVS Storage init failed!");
        set_LED_state(LED_STATE_FATAL_ERROR);
        return false;
    }

    return true;
}

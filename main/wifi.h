#pragma once

#include <stdbool.h>

#include "esp_err.h"

/**
 * Initialize Wi-Fi station mode and start connecting.
 *
 * Call this once from app_main().
 */
esp_err_t wifi_connect_start(const char *ssid, const char *password);
esp_err_t wifi_connect_stop(void);

/**
 * Returns true after the device has received an IP address.
 */
bool wifi_connect_is_connected(void);

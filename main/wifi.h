#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

/**
 * Initialize Wi-Fi station mode and start connecting with credentials from NVS.
 *
 * A local build containing config.h seeds NVS when credentials are absent.
 */
esp_err_t wifi_connect_start(void);
esp_err_t wifi_connect_wait(TickType_t timeout_ticks);
esp_err_t wifi_connect_stop(void);

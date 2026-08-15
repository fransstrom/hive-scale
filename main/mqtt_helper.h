#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "hive.h"

esp_err_t mqtt_connect(TickType_t timeout_ticks);
esp_err_t mqtt_publish(const hive_measurement_t *measurement,
                       TickType_t timeout_ticks);
esp_err_t mqtt_disconnect(void);

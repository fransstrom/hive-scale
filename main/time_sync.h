#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

esp_err_t time_sync(TickType_t timeout_ticks);

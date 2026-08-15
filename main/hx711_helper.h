#pragma once

#include <stdint.h>

#include "esp_err.h"

esp_err_t hx711_measure(float *weight_grams, int32_t *raw_value);

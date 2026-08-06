#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "hive.h"

esp_err_t measurement_queue_init(void);
esp_err_t measurement_queue_enqueue(float weight_grams, int32_t raw_value,
                                    int64_t timestamp, bool timestamp_valid);
esp_err_t measurement_queue_peek(hive_measurement_t *measurement);
esp_err_t measurement_queue_pop(void);
size_t measurement_queue_count(void);
size_t measurement_queue_capacity(void);

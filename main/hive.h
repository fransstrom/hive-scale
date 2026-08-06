#pragma once

#include <stdbool.h>
#include <stdint.h>

#define HIVE_MEASUREMENT_ID_SIZE 16

typedef struct {
  uint8_t id[HIVE_MEASUREMENT_ID_SIZE];
  float weight_grams;
  int32_t raw_value;
  int64_t timestamp;
  bool timestamp_valid;
} hive_measurement_t;

// The caller must release the returned string with cJSON_free().
char *hive_measurement_to_json(const hive_measurement_t *measurement,
                               const char *device_id);

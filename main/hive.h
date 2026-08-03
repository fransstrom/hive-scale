#pragma once

#include <limits.h>
#include <stdint.h>
#include <sys/types.h>

struct Hive_Measurement {
  const char *deviceId;
  float weight;
  int64_t timestamp;
};

// The caller must release the returned string with cJSON_free().
char *hive_measurement_to_json(const struct Hive_Measurement *measurement);

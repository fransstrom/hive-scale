#include "hive.h"

#include "cJSON.h"
#include <stdio.h>

char *hive_measurement_to_json(const hive_measurement_t *measurement,
                               const char *device_id) {
  if (measurement == NULL || device_id == NULL) {
    return NULL;
  }

  char measurement_id[HIVE_MEASUREMENT_ID_SIZE * 2 + 1];
  for (size_t i = 0; i < HIVE_MEASUREMENT_ID_SIZE; i++) {
    snprintf(&measurement_id[i * 2], 3, "%02x", measurement->id[i]);
  }

  cJSON *json = cJSON_CreateObject();
  if (json == NULL ||
      cJSON_AddStringToObject(json, "deviceId", device_id) == NULL ||
      cJSON_AddStringToObject(json, "measurementId", measurement_id) == NULL ||
      cJSON_AddNumberToObject(json, "weight_grams",
                              measurement->weight_grams) == NULL ||
      cJSON_AddNumberToObject(json, "raw_value", measurement->raw_value) ==
          NULL ||
      cJSON_AddBoolToObject(json, "timestampValid",
                            measurement->timestamp_valid) == NULL) {
    cJSON_Delete(json);
    return NULL;
  }

  cJSON *timestamp =
      measurement->timestamp_valid
          ? cJSON_AddNumberToObject(json, "timestamp",
                                    (double)measurement->timestamp)
          : cJSON_AddNullToObject(json, "timestamp");
  if (timestamp == NULL) {
    cJSON_Delete(json);
    return NULL;
  }

  char *serialized = cJSON_PrintUnformatted(json);
  cJSON_Delete(json);
  return serialized;
}

#include "hive.h"

#include "cJSON.h"

char *hive_measurement_to_json(const struct Hive_Measurement *measurement) {
  if (measurement == NULL || measurement->deviceId == NULL) {
    return NULL;
  }

  cJSON *json = cJSON_CreateObject();
  if (json == NULL ||
      cJSON_AddStringToObject(json, "deviceId", measurement->deviceId) ==
          NULL ||
      cJSON_AddNumberToObject(json, "weight_grams", measurement->weight) ==
          NULL) {
    cJSON_Delete(json);
    return NULL;
  }

  char *serialized = cJSON_PrintUnformatted(json);
  cJSON_Delete(json);
  return serialized;
}

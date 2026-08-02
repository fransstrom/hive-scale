#include "mqtt_helper.h"
#include "cJSON.h"
#include "config.h"
#include "esp_crt_bundle.h"
#include "esp_err.h"
#include "esp_log.h"
#include "hive.h"
#include "mqtt_client.h"
#include <stdint.h>
static const char *MQTT_TAG = "MQTT";

static esp_mqtt_client_handle_t mqtt_client;
void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                        int32_t event_id, void *event_data) {
  esp_mqtt_event_handle_t event = event_data;

  switch ((esp_mqtt_event_id_t)event_id) {
  case MQTT_EVENT_CONNECTED:
    ESP_LOGI(MQTT_TAG, "MQTT_EVENT_CONNECTED");
    break;
  case MQTT_EVENT_DISCONNECTED:
    ESP_LOGI(MQTT_TAG, "MQTT_EVENT_DISCONNECTED");
    break;

  case MQTT_EVENT_SUBSCRIBED:
    ESP_LOGI(MQTT_TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
    break;

  case MQTT_EVENT_PUBLISHED:
    ESP_LOGI(MQTT_TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
    break;

  case MQTT_EVENT_DATA:
    ESP_LOGI(MQTT_TAG, "MQTT_EVENT_DATA");
    // handle_mqtt_data(event_data);
    break;

  case MQTT_EVENT_ERROR:
    ESP_LOGI(MQTT_TAG, "MQTT_EVENT_ERROR");
    break;

  default:
    break;
  }
}
esp_err_t mqtt_init() {
  // Following config is required for MQTT over TLS
  const esp_mqtt_client_config_t mqtt_cfg = {
      .broker.address.hostname = MQTT_BROKER_URI,
      .broker.address.port = 8883,
      .credentials.username = MQTT_BROKER_USER,
      .credentials.authentication.password = MQTT_BROKER_PASS,
      .broker.address.transport = MQTT_TRANSPORT_OVER_SSL,
      .broker.verification.crt_bundle_attach = esp_crt_bundle_attach,
  };

  mqtt_client = esp_mqtt_client_init(&mqtt_cfg);

  esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID,
                                 mqtt_event_handler, NULL);
  return esp_mqtt_client_start(mqtt_client);
}

int mqtt_publish() {
  const struct Hive_Measurement payload = {.weight = 90000, .deviceId = "1"};
  char *json = hive_measurement_to_json(&payload);
  if (json == NULL) {
    ESP_LOGE(MQTT_TAG, "Could not serialize measurement");
    return -1;
  }

  const int message_id =
      esp_mqtt_client_publish(mqtt_client, "hive-scale/weight", json, 0, 1, 0);
  cJSON_free(json);
  return message_id;
}

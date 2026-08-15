#include "mqtt_helper.h"

#include "cJSON.h"
#include "config.h"
#include "esp_log.h"
#include "freertos/event_groups.h"
#include "hive.h"
#include "mqtt_client.h"

#define MQTT_CONNECTED_BIT BIT0
#define MQTT_PUBLISHED_BIT BIT1

static const char *TAG = "MQTT";
static esp_mqtt_client_handle_t s_client;
static EventGroupHandle_t s_events;
static int s_published_message_id = -1;

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data) {
  esp_mqtt_event_handle_t event = event_data;

  switch ((esp_mqtt_event_id_t)event_id) {
  case MQTT_EVENT_CONNECTED:
    ESP_LOGI(TAG, "Connected to broker");
    xEventGroupSetBits(s_events, MQTT_CONNECTED_BIT);
    break;
  case MQTT_EVENT_DISCONNECTED:
    ESP_LOGW(TAG, "Disconnected from broker");
    xEventGroupClearBits(s_events, MQTT_CONNECTED_BIT);
    break;
  case MQTT_EVENT_PUBLISHED:
    s_published_message_id = event->msg_id;
    xEventGroupSetBits(s_events, MQTT_PUBLISHED_BIT);
    break;
  case MQTT_EVENT_ERROR:
    ESP_LOGE(TAG, "Broker connection error");
    break;
  default:
    break;
  }
}

esp_err_t mqtt_connect(TickType_t timeout_ticks) {
  if (s_client != NULL) {
    return ESP_ERR_INVALID_STATE;
  }

  s_events = xEventGroupCreate();
  if (s_events == NULL) {
    return ESP_ERR_NO_MEM;
  }

  const esp_mqtt_client_config_t mqtt_config = {
      .broker.address.hostname = MQTT_BROKER_URI,
      .broker.address.port = 1883,
      .broker.address.transport = MQTT_TRANSPORT_OVER_TCP,
      .credentials.username = MQTT_BROKER_USER,
      .credentials.authentication.password = MQTT_BROKER_PASS,
  };

  ESP_LOGI(TAG, "Connecting to %s:%d over TCP", MQTT_BROKER_URI, 1883);

  s_client = esp_mqtt_client_init(&mqtt_config);
  if (s_client == NULL) {
    vEventGroupDelete(s_events);
    s_events = NULL;
    return ESP_ERR_NO_MEM;
  }

  esp_err_t err = esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID,
                                                 mqtt_event_handler, NULL);
  if (err == ESP_OK) {
    err = esp_mqtt_client_start(s_client);
  }
  if (err != ESP_OK) {
    esp_mqtt_client_destroy(s_client);
    s_client = NULL;
    vEventGroupDelete(s_events);
    s_events = NULL;
    return err;
  }

  const EventBits_t bits = xEventGroupWaitBits(s_events, MQTT_CONNECTED_BIT,
                                               pdFALSE, pdTRUE, timeout_ticks);
  if ((bits & MQTT_CONNECTED_BIT) == 0) {
    mqtt_disconnect();
    return ESP_ERR_TIMEOUT;
  }
  return ESP_OK;
}

esp_err_t mqtt_publish(const hive_measurement_t *measurement,
                       TickType_t timeout_ticks) {
  if (s_client == NULL || measurement == NULL ||
      (xEventGroupGetBits(s_events) & MQTT_CONNECTED_BIT) == 0) {
    return ESP_ERR_INVALID_STATE;
  }

  char *json = hive_measurement_to_json(measurement, "1");
  if (json == NULL) {
    return ESP_ERR_NO_MEM;
  }

  s_published_message_id = -1;
  xEventGroupClearBits(s_events, MQTT_PUBLISHED_BIT);
  ESP_LOGI(TAG, "Publishing measurement to %s", MQTT_TOPIC_WEIGHT_MEASUREMENT);
  const int message_id = esp_mqtt_client_publish(
      s_client, MQTT_TOPIC_WEIGHT_MEASUREMENT, json, 0, 1, 0);
  cJSON_free(json);
  if (message_id < 0) {
    return ESP_FAIL;
  }

  const EventBits_t bits = xEventGroupWaitBits(s_events, MQTT_PUBLISHED_BIT,
                                               pdTRUE, pdTRUE, timeout_ticks);
  if ((bits & MQTT_PUBLISHED_BIT) == 0) {
    return ESP_ERR_TIMEOUT;
  }
  if (s_published_message_id != message_id) {
    ESP_LOGE(TAG, "Unexpected PUBACK %d while waiting for %d",
             s_published_message_id, message_id);
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Broker acknowledged measurement (message ID %d)", message_id);
  return ESP_OK;
}

esp_err_t mqtt_disconnect(void) {
  if (s_client == NULL) {
    return ESP_OK;
  }

  esp_err_t err = esp_mqtt_client_stop(s_client);
  esp_err_t destroy_err = esp_mqtt_client_destroy(s_client);
  s_client = NULL;
  if (s_events != NULL) {
    vEventGroupDelete(s_events);
    s_events = NULL;
  }

  return err != ESP_OK ? err : destroy_err;
}

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hx711_helper.h"
#include "measurement_queue.h"
#include "mqtt_helper.h"
#include "ota.h"
#include "sdkconfig.h"
#include "time_sync.h"
#include "wifi.h"

#define CYCLE_TAG "CYCLE"
#define LED_GPIO GPIO_NUM_8
#define EARLIEST_VALID_TIMESTAMP 1704067200LL

static TickType_t communication_time_remaining(TickType_t started_at) {
  const TickType_t retry_window =
      pdMS_TO_TICKS(CONFIG_BEESCALE_COMMUNICATION_RETRY_SECONDS * 1000);
  const TickType_t elapsed = xTaskGetTickCount() - started_at;
  return elapsed < retry_window ? retry_window - elapsed : 0;
}

static esp_err_t save_measurement(void) {
  float weight_grams;
  int32_t raw_value;
  esp_err_t err = hx711_measure(&weight_grams, &raw_value);
  if (err != ESP_OK) {
    return err;
  }

  const int64_t timestamp = (int64_t)time(NULL);
  return measurement_queue_enqueue(weight_grams, raw_value, timestamp,
                                   timestamp >= EARLIEST_VALID_TIMESTAMP);
}

static bool upload_measurements(TickType_t communication_started_at) {
  while (measurement_queue_count() > 0) {
    hive_measurement_t measurement;
    esp_err_t err = measurement_queue_peek(&measurement);
    if (err != ESP_OK) {
      ESP_LOGE(CYCLE_TAG, "Could not read queued measurement: %s",
               esp_err_to_name(err));
      return false;
    }

    const TickType_t remaining =
        communication_time_remaining(communication_started_at);
    if (remaining == 0) {
      ESP_LOGW(CYCLE_TAG, "Communication retry window expired");
      return false;
    }

    err = mqtt_publish(&measurement, remaining);
    if (err != ESP_OK) {
      ESP_LOGW(CYCLE_TAG, "Measurement remains queued: %s",
               esp_err_to_name(err));
      return false;
    }

    err = measurement_queue_pop();
    if (err != ESP_OK) {
      ESP_LOGE(CYCLE_TAG, "Could not remove acknowledged measurement: %s",
               esp_err_to_name(err));
      return false;
    }
  }

  return true;
}

void app_main(void) {
  gpio_reset_pin(LED_GPIO);
  gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
  gpio_set_level(LED_GPIO, 0);

  esp_err_t err = measurement_queue_init();
  if (err != ESP_OK) {
    ESP_LOGE(CYCLE_TAG, "Could not initialize measurement queue: %s",
             esp_err_to_name(err));
    goto sleep;
  }

  const bool measurement_deferred =
      measurement_queue_count() == measurement_queue_capacity();
  if (!measurement_deferred) {
    err = save_measurement();
    if (err != ESP_OK) {
      ESP_LOGE(CYCLE_TAG, "Could not take and persist measurement: %s",
               esp_err_to_name(err));
    }
  } else {
    ESP_LOGW(CYCLE_TAG, "Queue is full; measurement deferred until upload");
  }

  const TickType_t communication_started_at = xTaskGetTickCount();
  ESP_LOGI(CYCLE_TAG, "Starting %d-second communication retry window",
           CONFIG_BEESCALE_COMMUNICATION_RETRY_SECONDS);

  err = wifi_connect_start();
  if (err != ESP_OK) {
    ESP_LOGE(CYCLE_TAG, "Could not start Wi-Fi: %s", esp_err_to_name(err));
    goto sleep;
  }

  err =
      wifi_connect_wait(communication_time_remaining(communication_started_at));
  if (err != ESP_OK) {
    ESP_LOGW(CYCLE_TAG, "Wi-Fi unavailable; measurements remain queued");
    goto sleep;
  }

  err = ota_confirm_running_image();
  if (err != ESP_OK) {
    ESP_LOGE(CYCLE_TAG, "Could not confirm running firmware: %s",
             esp_err_to_name(err));
    goto sleep;
  }

  TickType_t remaining = communication_time_remaining(communication_started_at);
  if (remaining > 0) {
    const TickType_t time_sync_limit = pdMS_TO_TICKS(15000);
    err = time_sync(remaining < time_sync_limit ? remaining : time_sync_limit);
    if (err != ESP_OK) {
      ESP_LOGW(CYCLE_TAG, "Could not synchronize time: %s",
               esp_err_to_name(err));
    }
  }

  bool queue_drained = measurement_queue_count() == 0;
  if (!queue_drained) {
    remaining = communication_time_remaining(communication_started_at);
    err = remaining > 0 ? mqtt_connect(remaining) : ESP_ERR_TIMEOUT;
    if (err == ESP_OK) {
      queue_drained = upload_measurements(communication_started_at);
      if (measurement_deferred &&
          measurement_queue_count() < measurement_queue_capacity()) {
        err = save_measurement();
        if (err == ESP_OK && queue_drained) {
          queue_drained = upload_measurements(communication_started_at);
        } else if (err != ESP_OK) {
          ESP_LOGE(CYCLE_TAG, "Could not take deferred measurement: %s",
                   esp_err_to_name(err));
        }
      }
    } else {
      ESP_LOGW(CYCLE_TAG, "MQTT unavailable; measurements remain queued: %s",
               esp_err_to_name(err));
    }
  }

  err = mqtt_disconnect();
  if (err != ESP_OK) {
    ESP_LOGW(CYCLE_TAG, "Could not stop MQTT cleanly: %s",
             esp_err_to_name(err));
  }

  if (queue_drained && ota_check_is_due()) {
    err = ota_check_for_update();
    if (err != ESP_OK) {
      ESP_LOGW(CYCLE_TAG, "OTA check failed: %s", esp_err_to_name(err));
    }
  }

sleep:
  err = mqtt_disconnect();
  if (err != ESP_OK) {
    ESP_LOGW(CYCLE_TAG, "Could not stop MQTT cleanly: %s",
             esp_err_to_name(err));
  }

  err = wifi_connect_stop();
  if (err != ESP_OK) {
    ESP_LOGW(CYCLE_TAG, "Could not stop Wi-Fi cleanly: %s",
             esp_err_to_name(err));
  }

  ESP_LOGI(CYCLE_TAG, "Sleeping for %d seconds with %u queued measurements",
           CONFIG_BEESCALE_DEEP_SLEEP_SECONDS,
           (unsigned int)measurement_queue_count());
  gpio_set_level(LED_GPIO, 1);
  ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(
      (uint64_t)CONFIG_BEESCALE_DEEP_SLEEP_SECONDS * 1000000ULL));
  esp_deep_sleep_start();
}

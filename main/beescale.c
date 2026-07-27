
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ota.h"
#include "sdkconfig.h"
#include "wifi.h"

#define CYCLE_TAG "CYCLE"
#define LED_GPIO GPIO_NUM_8

void app_main(void) {
  // XIAO ESP32-C3 does not have built in LED.
  gpio_reset_pin(LED_GPIO);
  gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
  gpio_set_level(LED_GPIO, 0);

  esp_err_t err = wifi_connect_start();
  if (err != ESP_OK) {
    ESP_LOGE(CYCLE_TAG, "Could not start Wi-Fi: %s", esp_err_to_name(err));
    goto sleep;
  }

  err = wifi_connect_wait(
      pdMS_TO_TICKS(CONFIG_BEESCALE_WIFI_CONNECT_TIMEOUT_SECONDS * 1000));
  if (err != ESP_OK) {
    ESP_LOGE(CYCLE_TAG, "Wi-Fi connection timed out");
    goto sleep;
  }
  ESP_LOGI(CYCLE_TAG, "WIFI CONNECTED");

  err = ota_confirm_running_image();
  if (err != ESP_OK) {
    ESP_LOGE(CYCLE_TAG, "Could not confirm running firmware: %s",
             esp_err_to_name(err));
    goto sleep;
  }

  // Do the measurements here - send to mqtt and all START

  // Do the measurements here - send to mqtt and all END

  if (ota_check_is_due()) {
    err = ota_check_for_update();
    if (err != ESP_OK) {
      ESP_LOGW(CYCLE_TAG, "OTA check failed: %s", esp_err_to_name(err));
    }
  }

sleep:
  ESP_LOGI(CYCLE_TAG, "PREPARING DEEPSLEEP");
  err = wifi_connect_stop();
  if (err != ESP_OK) {
    ESP_LOGW(CYCLE_TAG, "Could not stop Wi-Fi cleanly: %s",
             esp_err_to_name(err));
  }
  ESP_LOGI(CYCLE_TAG, "WIFI OFF");
  ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(1ULL * 60ULL * 1000000ULL));
  vTaskDelay(pdMS_TO_TICKS(20000));
  gpio_set_level(LED_GPIO, 1);
  esp_deep_sleep_start();
}


#include "config.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/projdefs.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "wifi.h"
#include <stdbool.h>

#define CYCLE_TAG "CYCLE"
#define LED_GPIO GPIO_NUM_8

void app_main(void) {
  // XIAO ESP32-C3 does not have built in LED.
  gpio_reset_pin(LED_GPIO);
  gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
  gpio_set_level(LED_GPIO, 0);

  ESP_ERROR_CHECK(wifi_connect_start(WIFI_SSID, WIFI_PASS));
  ESP_LOGI(CYCLE_TAG, "WIFI CONNECTED");

  // Do the measurements here - send to mqtt and all START

  // Do the measurements here - send to mqtt and all END

  ESP_LOGI(CYCLE_TAG, "PREPARING DEEPSLEEP");
  ESP_ERROR_CHECK(wifi_connect_stop());
  ESP_LOGI(CYCLE_TAG, "WIFI OFF");
  ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(1ULL * 60ULL * 1000000ULL));
  vTaskDelay(pdMS_TO_TICKS(20000));
  gpio_set_level(LED_GPIO, 1);
  esp_deep_sleep_start();
}

#include "hx711.h"
#include "lwip/err.h"
#include "mqtt_helper.h"
#include "sdkconfig.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <hx711.h>
#include <inttypes.h>

static const char *TAG = "HX711";
esp_err_t hx711_test() {

  hx711_t dev = {.dout = CONFIG_GPIO_HX711_DOUT,
                 .pd_sck = CONFIG_GPIO_HX711_SCK,
                 .gain = HX711_GAIN_A_64};

  // initialize device
  ESP_ERROR_CHECK(hx711_init(&dev));

  // read from device
  while (1) {
    esp_err_t r = hx711_wait(&dev, 500);
    if (r != ESP_OK) {
      ESP_LOGE(TAG, "Device not found: %d (%s)\n", r, esp_err_to_name(r));
      continue;
    }

    int32_t data;
    r = hx711_read_average(&dev, 5U, &data);
    if (r != ESP_OK) {
      ESP_LOGE(TAG, "Could not read data: %d (%s)\n", r, esp_err_to_name(r));
      continue;
    }

    ESP_LOGI(TAG, "Raw data: %" PRIi32, data);
    mqtt_publish();
    vTaskDelay(pdMS_TO_TICKS(500));
  }
  return ERR_OK;
}

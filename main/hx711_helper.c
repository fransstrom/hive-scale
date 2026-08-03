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
    r = hx711_read_average(&dev, 20U, &data);
    if (r != ESP_OK) {
      ESP_LOGE(TAG, "Could not read data: %d (%s)\n", r, esp_err_to_name(r));
      continue;
    }
    // counts_per_kg = (loaded_raw - zero_offset) / known_weight_kg
    float weight_grams = (float)(data - CONFIG_HX711_ZERO_OFFSET) * 1000.0f /
                         CONFIG_HX711_COUNTS_PER_KG;
    ESP_LOGI(TAG, "Raw data: %" PRIi32, data);
    ESP_LOGI(TAG, "Weight: %.2f g", weight_grams);
    mqtt_publish(weight_grams);
    vTaskDelay(pdMS_TO_TICKS(3000));
  }
  return ERR_OK;
}

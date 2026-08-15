#include "hx711.h"
#include "sdkconfig.h"
#include <esp_log.h>
#include <inttypes.h>

static const char *TAG = "HX711";
esp_err_t hx711_measure(float *weight_grams, int32_t *raw_value) {
  if (weight_grams == NULL || raw_value == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  hx711_t dev = {.dout = CONFIG_GPIO_HX711_DOUT,
                 .pd_sck = CONFIG_GPIO_HX711_SCK,
                 .gain = HX711_GAIN_A_64};

  esp_err_t err = hx711_init(&dev);
  if (err == ESP_OK) {
    err = hx711_wait(&dev, 500);
  }
  if (err == ESP_OK) {
    err = hx711_read_average(&dev, 20U, raw_value);
  }

  esp_err_t power_err = hx711_power_down(&dev, true);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Could not read scale: %s", esp_err_to_name(err));
    return err;
  }
  if (power_err != ESP_OK) {
    ESP_LOGW(TAG, "Could not power down HX711: %s", esp_err_to_name(power_err));
  }

  // counts_per_kg = (loaded_raw - zero_offset) / known_weight_kg
  *weight_grams = (float)(*raw_value - CONFIG_HX711_ZERO_OFFSET) * 1000.0f /
                  CONFIG_HX711_COUNTS_PER_KG;
  ESP_LOGI(TAG, "Raw data: %" PRIi32, *raw_value);
  ESP_LOGI(TAG, "Weight: %.2f g", *weight_grams);
  return ESP_OK;
}

#include "ota.h"

#include <stdint.h>
#include <stdlib.h>

#include "esp_app_desc.h"
#include "esp_attr.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define OTA_SCHEDULE_MAGIC 0x42534f54U

static const char *TAG = "ota";

typedef struct {
  uint32_t magic;
  uint32_t wakes_since_check;
} ota_schedule_t;

typedef struct {
  unsigned long major;
  unsigned long minor;
  unsigned long patch;
} ota_version_t;

static RTC_DATA_ATTR ota_schedule_t s_schedule;

static bool parse_version(const char *text, ota_version_t *version) {
  if (*text == 'v') {
    text++;
  }

  char *end;
  version->major = strtoul(text, &end, 10);
  if (end == text || *end != '.') {
    return false;
  }

  text = end + 1;
  version->minor = strtoul(text, &end, 10);
  if (end == text || *end != '.') {
    return false;
  }

  text = end + 1;
  version->patch = strtoul(text, &end, 10);
  return end != text && (*end == '\0' || *end == '-' || *end == '+');
}

static int compare_versions(const ota_version_t *left,
                            const ota_version_t *right) {
  if (left->major != right->major) {
    return left->major > right->major ? 1 : -1;
  }
  if (left->minor != right->minor) {
    return left->minor > right->minor ? 1 : -1;
  }
  if (left->patch != right->patch) {
    return left->patch > right->patch ? 1 : -1;
  }
  return 0;
}

bool ota_check_is_due(void) {
  const uint32_t interval = CONFIG_BEESCALE_OTA_CHECK_INTERVAL_WAKEUPS;

  if (s_schedule.magic != OTA_SCHEDULE_MAGIC) {
    s_schedule.magic = OTA_SCHEDULE_MAGIC;
    s_schedule.wakes_since_check = interval - 1;
  }

  s_schedule.wakes_since_check++;
  if (s_schedule.wakes_since_check < interval) {
    return false;
  }

  s_schedule.wakes_since_check = 0;
  return true;
}

esp_err_t ota_confirm_running_image(void) {
  const esp_partition_t *running = esp_ota_get_running_partition();
  esp_ota_img_states_t state;
  esp_err_t err = esp_ota_get_state_partition(running, &state);

  if (err == ESP_ERR_NOT_SUPPORTED || err == ESP_ERR_NOT_FOUND) {
    return ESP_OK;
  }
  if (err != ESP_OK) {
    return err;
  }
  if (state != ESP_OTA_IMG_PENDING_VERIFY) {
    return ESP_OK;
  }

  err = esp_ota_mark_app_valid_cancel_rollback();
  if (err == ESP_OK) {
    ESP_LOGI(TAG, "Running firmware passed its health check");
  }
  return err;
}

esp_err_t ota_check_for_update(void) {
  const esp_app_desc_t *current_app = esp_app_get_description();
  ESP_LOGI(TAG, "Checking %s (running version: %s)", CONFIG_BEESCALE_OTA_URL,
           current_app->version);

  esp_http_client_config_t http_config = {
      .url = CONFIG_BEESCALE_OTA_URL,
      .buffer_size_tx = 2048,
      .crt_bundle_attach = esp_crt_bundle_attach,
      .timeout_ms = 30000,
      .keep_alive_enable = true,
  };
  esp_https_ota_config_t ota_config = {
      .http_config = &http_config,
  };
  esp_https_ota_handle_t handle = NULL;

  esp_err_t err = esp_https_ota_begin(&ota_config, &handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Could not start update check: %s", esp_err_to_name(err));
    return err;
  }

  esp_app_desc_t new_app = {0};
  err = esp_https_ota_get_img_desc(handle, &new_app);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Could not read update metadata: %s", esp_err_to_name(err));
    esp_https_ota_abort(handle);
    return err;
  }

  ota_version_t current_version;
  ota_version_t new_version;
  if (!parse_version(new_app.version, &new_version)) {
    ESP_LOGE(TAG, "Release has an invalid version: %s", new_app.version);
    esp_https_ota_abort(handle);
    return ESP_ERR_INVALID_VERSION;
  }

  if (parse_version(current_app->version, &current_version) &&
      compare_versions(&new_version, &current_version) <= 0) {
    ESP_LOGI(TAG, "Skipping version %s; running version %s is not older",
             new_app.version, current_app->version);
    esp_https_ota_abort(handle);
    return ESP_OK;
  }

  ESP_LOGI(TAG, "Downloading firmware version %s", new_app.version);
  do {
    err = esp_https_ota_perform(handle);
  } while (err == ESP_ERR_HTTPS_OTA_IN_PROGRESS);

  if (err != ESP_OK || !esp_https_ota_is_complete_data_received(handle)) {
    if (err == ESP_OK) {
      err = ESP_ERR_INVALID_SIZE;
    }
    ESP_LOGE(TAG, "Firmware download failed: %s", esp_err_to_name(err));
    esp_https_ota_abort(handle);
    return err;
  }

  err = esp_https_ota_finish(handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Firmware validation failed: %s", esp_err_to_name(err));
    return err;
  }

  ESP_LOGI(TAG, "Firmware update installed; restarting");
  vTaskDelay(pdMS_TO_TICKS(1000));
  esp_restart();
  return ESP_OK;
}

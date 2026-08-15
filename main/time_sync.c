#include "time_sync.h"

#include <inttypes.h>
#include <time.h>

#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "time_sync";

esp_err_t time_sync(TickType_t timeout_ticks) {
  const esp_sntp_config_t config =
      ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");

  esp_err_t err = esp_netif_sntp_init(&config);
  if (err != ESP_OK) {
    return err;
  }

  err = esp_netif_sntp_sync_wait(timeout_ticks);
  esp_netif_sntp_deinit();
  if (err != ESP_OK) {
    return err;
  }

  const time_t now = time(NULL);
  ESP_LOGI(TAG, "Time synchronized: %" PRId64, (int64_t)now);
  return ESP_OK;
}

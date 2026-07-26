#include "wifi.h"

#include <stddef.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

static const char *TAG = "wifi_connect";

static bool s_started;
static bool s_connected;
static bool s_stopping;
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {

    ESP_LOGI(TAG, "Wi-Fi started; connecting");

    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(err));
    }

    return;
  }
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {

    s_connected = false;

    if (!s_stopping) {
      ESP_LOGW(TAG, "Disconnected; reconnecting");

      esp_err_t err = esp_wifi_connect();
      if (err != ESP_OK) {
        ESP_LOGE(TAG, "Reconnect failed: %s", esp_err_to_name(err));
      }
    }

    return;
  }

  if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {

    const ip_event_got_ip_t *event = event_data;

    s_connected = true;

    ESP_LOGI(TAG, "Connected, IP address: " IPSTR, IP2STR(&event->ip_info.ip));
  }
}

static esp_err_t initialize_nvs(void) {
  esp_err_t err = nvs_flash_init();

  if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
      err == ESP_ERR_NVS_NEW_VERSION_FOUND) {

    err = nvs_flash_erase();
    if (err != ESP_OK) {
      return err;
    }

    err = nvs_flash_init();
  }

  return err;
}

esp_err_t wifi_connect_start(const char *ssid, const char *password) {
  if (ssid == NULL || password == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  if (s_started) {
    return ESP_ERR_INVALID_STATE;
  }

  const size_t ssid_length = strlen(ssid);
  const size_t password_length = strlen(password);

  wifi_config_t wifi_config = {0};

  if (ssid_length > sizeof(wifi_config.sta.ssid) ||
      password_length > sizeof(wifi_config.sta.password)) {
    return ESP_ERR_INVALID_ARG;
  }

  esp_err_t err = initialize_nvs();
  if (err != ESP_OK) {
    return err;
  }

  err = esp_netif_init();
  if (err != ESP_OK) {
    return err;
  }

  err = esp_event_loop_create_default();
  if (err != ESP_OK) {
    return err;
  }

  esp_netif_t *station = esp_netif_create_default_wifi_sta();

  if (station == NULL) {
    return ESP_FAIL;
  }

  wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();

  err = esp_wifi_init(&init_config);
  if (err != ESP_OK) {
    return err;
  }

  err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                   wifi_event_handler, NULL);

  if (err != ESP_OK) {
    return err;
  }

  err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                   wifi_event_handler, NULL);

  if (err != ESP_OK) {
    return err;
  }

  memcpy(wifi_config.sta.ssid, ssid, ssid_length);

  memcpy(wifi_config.sta.password, password, password_length);

  wifi_config.sta.pmf_cfg.capable = true;
  wifi_config.sta.pmf_cfg.required = false;

  err = esp_wifi_set_mode(WIFI_MODE_STA);
  if (err != ESP_OK) {
    return err;
  }

  err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);

  if (err != ESP_OK) {
    return err;
  }

  err = esp_wifi_start();
  if (err != ESP_OK) {
    return err;
  }

  s_started = true;

  return ESP_OK;
}
esp_err_t wifi_connect_stop(void) {
  if (!s_started) {
    return ESP_OK;
  }

  s_stopping = true;
  s_connected = false;

  esp_err_t err = esp_wifi_stop();

  if (err == ESP_OK) {
    s_started = false;
  }

  return err;
}

bool wifi_connect_is_connected(void) { return s_connected; }

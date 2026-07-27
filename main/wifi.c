#include "wifi.h"

#include <stddef.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs.h"
#include "nvs_flash.h"

#if __has_include("config.h")
#include "config.h"
#define BEESCALE_HAS_BOOTSTRAP_CREDENTIALS 1
#endif

static const char *TAG = "wifi_connect";

#define WIFI_CONNECTED_BIT BIT0

static bool s_started;
static bool s_stopping;
static EventGroupHandle_t s_wifi_events;

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

    xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT);

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

    xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);

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

static esp_err_t save_bootstrap_credentials(nvs_handle_t nvs) {
#ifdef BEESCALE_HAS_BOOTSTRAP_CREDENTIALS
  esp_err_t err = nvs_set_str(nvs, "ssid", WIFI_SSID);
  if (err == ESP_OK) {
    err = nvs_set_str(nvs, "password", WIFI_PASS);
  }
  if (err == ESP_OK) {
    err = nvs_commit(nvs);
  }
  if (err == ESP_OK) {
    ESP_LOGI(TAG, "Saved bootstrap Wi-Fi credentials to NVS");
  }
  return err;
#else
  (void)nvs;
  ESP_LOGE(TAG, "Wi-Fi credentials are missing; flash a local build with "
                "main/config.h to seed NVS");
  return ESP_ERR_NVS_NOT_FOUND;
#endif
}

static esp_err_t load_wifi_credentials(wifi_config_t *wifi_config) {
  nvs_handle_t nvs;
  esp_err_t err = nvs_open("beescale_wifi", NVS_READWRITE, &nvs);
  if (err != ESP_OK) {
    return err;
  }

  char ssid[sizeof(wifi_config->sta.ssid) + 1] = {0};
  char password[sizeof(wifi_config->sta.password) + 1] = {0};
  size_t ssid_size = sizeof(ssid);
  size_t password_size = sizeof(password);

  err = nvs_get_str(nvs, "ssid", ssid, &ssid_size);
  if (err == ESP_OK) {
    err = nvs_get_str(nvs, "password", password, &password_size);
  }
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    err = save_bootstrap_credentials(nvs);
    if (err == ESP_OK) {
      ssid_size = sizeof(ssid);
      password_size = sizeof(password);
      err = nvs_get_str(nvs, "ssid", ssid, &ssid_size);
    }
    if (err == ESP_OK) {
      err = nvs_get_str(nvs, "password", password, &password_size);
    }
  }

  nvs_close(nvs);
  if (err != ESP_OK) {
    return err;
  }

  const size_t ssid_length = strlen(ssid);
  const size_t password_length = strlen(password);
  if (ssid_length == 0 || ssid_length > sizeof(wifi_config->sta.ssid) ||
      password_length > sizeof(wifi_config->sta.password)) {
    return ESP_ERR_INVALID_SIZE;
  }

  memcpy(wifi_config->sta.ssid, ssid, ssid_length);
  memcpy(wifi_config->sta.password, password, password_length);
  return ESP_OK;
}

esp_err_t wifi_connect_start(void) {

  if (s_started) {
    return ESP_ERR_INVALID_STATE;
  }

  wifi_config_t wifi_config = {0};

  esp_err_t err = initialize_nvs();
  if (err != ESP_OK) {
    return err;
  }

  err = load_wifi_credentials(&wifi_config);
  if (err != ESP_OK) {
    return err;
  }

  s_wifi_events = xEventGroupCreate();
  if (s_wifi_events == NULL) {
    return ESP_ERR_NO_MEM;
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
  s_stopping = false;

  return ESP_OK;
}

esp_err_t wifi_connect_wait(TickType_t timeout_ticks) {
  if (!s_started || s_wifi_events == NULL) {
    return ESP_ERR_INVALID_STATE;
  }

  const EventBits_t bits = xEventGroupWaitBits(
      s_wifi_events, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, timeout_ticks);
  return (bits & WIFI_CONNECTED_BIT) != 0 ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t wifi_connect_stop(void) {
  if (!s_started) {
    return ESP_OK;
  }

  s_stopping = true;
  xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT);

  esp_err_t err = esp_wifi_stop();

  if (err == ESP_OK) {
    s_started = false;
  }

  return err;
}

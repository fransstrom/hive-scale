idf_component_register(SRCS "beescale.c" "ota.c" "wifi.c" "time_sync.c" "hx711_helper.c" "hive.c" "mqtt_helper.c" "measurement_queue.c"
                      INCLUDE_DIRS
                      PRIV_REQUIRES app_update esp_http_client esp_https_ota
                                    esp_netif esp_wifi json mbedtls nvs_flash)

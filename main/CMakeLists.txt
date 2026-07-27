idf_component_register(SRCS "beescale.c" "ota.c" "wifi.c"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES app_update esp_http_client esp_https_ota
                                  esp_wifi mbedtls nvs_flash)

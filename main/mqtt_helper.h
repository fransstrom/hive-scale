#pragma once
#include "esp_err.h"
esp_err_t mqtt_init();
int mqtt_publish(float weight);

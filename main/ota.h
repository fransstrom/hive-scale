#pragma once

#include <stdbool.h>

#include "esp_err.h"

bool ota_check_is_due(void);
esp_err_t ota_confirm_running_image(void);
esp_err_t ota_check_for_update(void);

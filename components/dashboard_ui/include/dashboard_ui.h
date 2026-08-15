#pragma once

#include "app_model.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t dashboard_ui_create(void);
void dashboard_ui_update(const app_snapshot_t *snapshot);

#ifdef __cplusplus
}
#endif

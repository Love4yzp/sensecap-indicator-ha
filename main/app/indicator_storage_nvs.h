#ifndef INDICATOR_STORAGE_NVS_H
#define INDICATOR_STORAGE_NVS_H

#include "view_data.h"
#include <esp_err.h>
#include <nvs.h>

#ifdef __cplusplus
extern "C" {
#endif

int indicator_nvs_init(void);

esp_err_t indicator_nvs_write(char* p_key, void* p_data, size_t len);

// p_len : inout
esp_err_t indicator_nvs_read(char* p_key, void* p_data, size_t* p_len);

#ifdef __cplusplus
}
#endif

#endif

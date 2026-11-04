#include "wifi.h"
#include "indicator_enabler.h"
#include "entity_core.h"
#include "native_api.h"
#include "esp_err.h"

int indicator_model_init(void) {
#ifdef STORAGE_NVS_H
	indicator_nvs_init();
#else
	#error "Please inplement storage model"
#endif

#ifdef BTN_H
	indicator_btn_init();
#endif

#ifdef INDICATOR_DISPLAY_H
	indicator_display_init(); // lcd bl on
#endif

#ifdef RP2040_H
	esp32_rp2040_init();
	#ifdef SENSOR_H
	indicator_sensor_init();
	#endif
#endif

#ifdef CMD_H
	indicator_cmd_init();
#endif

#ifdef HA_H
	ESP_ERROR_CHECK(entity_core_init() ? ESP_OK : ESP_FAIL);
	indicator_ha_model_init();
	ESP_ERROR_CHECK(native_api_init());
#endif

#ifdef WIFI_H
	#ifdef MQTT_H
	indicator_mqtt_init();
	#endif
	indicator_wifi_model_init();

#endif

#ifdef INDICATOR_LORAWAN_H
	indicator_lorawan_init();
	#ifdef CMD_H
	indicator_cmd_init();
	#endif
#endif

#ifdef INDICATOR_TERMINAL_H
	indicator_terminal_init();
#endif
}

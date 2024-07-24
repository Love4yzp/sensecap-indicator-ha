#include "indicator_enabler.h"

#include "ui.h"
#include "view_data.h"

int indicator_view_init(void) {
	lv_port_sem_take();
	ui_init(); /* (must be 480*800, set LCD_EVB_SCREEN_ROTATION_90 in
				  menuconfig)*/
	lv_port_sem_give();

#ifdef SENSOR_H
	view_sensor_init();
#endif

#ifdef INDICATOR_WIFI_H
	indicator_wifi_view_init();
#endif
}
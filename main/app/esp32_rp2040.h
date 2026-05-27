/**
 * @file esp32_rp2040.h
 * @date  12 December 2023

 * @author Spencer Yan
 *
 * @note Description of the file
 *
 * @copyright © 2023, Seeed Studio
 */

#ifndef ESP32_RP2040_H
#define ESP32_RP2040_H

#ifdef __cplusplus
extern "C" {
#endif

#include "view_data.h"
enum pkt_type_cmd
{
	PKT_TYPE_NONE = 0,
	PKT_TYPE_CMD_COLLECT_INTERVAL = 0xA0, // uin32_t
	PKT_TYPE_CMD_BEEP_ON = 0xA1, // uin32_t  ms: on time
	PKT_TYPE_CMD_BEEP_OFF = 0xA2,
	PKT_TYPE_CMD_SHUTDOWN = 0xA3,
	PKT_TYPE_CMD_POWER_ON = 0xA4,
	PKT_TYPE_CMD_RESCAN_GROVE = 0xA5,
};

enum pkt_type_data
{
	// Inner Sensor
	// PKT_TYPE_SENSOR_SCD41_TEMP      = 0xB0, // not accurate
	// PKT_TYPE_SENSOR_SCD41_HUMIDITY = 0xB1,
	PKT_TYPE_SENSOR_SCD41_CO2 = 0xB2, // float

	// SHT41 AHT
	PKT_TYPE_SENSOR_SHT41_TEMP = 0xB3,
	PKT_TYPE_SENSOR_SHT41_HUMIDITY = 0xB4, // float
	
	// Inner Sensor SGP40
	PKT_TYPE_SENSOR_SGP40_TVOC_INDEX = 0xB5, // float

	// Dynamic sensor registry packets.
	PKT_TYPE_SENSOR_ATTACHED = 0xB8,
	PKT_TYPE_SENSOR_DETACHED = 0xB9,
	PKT_TYPE_SENSOR_VALUE = 0xBA,

	// SEN5x
	PKT_TYPE_SENSOR_SEN5X_massConcentrationPm1p0 = 0xB6,
	PKT_TYPE_SENSOR_SEN5X_massConcentrationPm2p5 = 0xB7,
	PKT_TYPE_SENSOR_SEN5X_massConcentrationPm4p0 = 0xB8,
	PKT_TYPE_SENSOR_SEN5X_massConcentrationPm10p0 = 0xB9,
	PKT_TYPE_SENSOR_SEN5X_ambientHumidity = 0xBA,
	PKT_TYPE_SENSOR_SEN5X_ambientTemperature = 0xBB,
	PKT_TYPE_SENSOR_SEN5X_vocIndex = 0xBC,
	PKT_TYPE_SENSOR_SEN5X_noxIndex = 0xBD,

	// SFA3X
	PKT_TYPE_SENSOR_SFA3X_HCHO = 0xBE,
	PKT_TYPE_SENSOR_SFA3X_HUMIDITY = 0xBF,
	PKT_TYPE_SENSOR_SFA3X_TEMP = 0xC0,
};

#define PKT_SENSOR_ID_AHT20_TEMP 0
#define PKT_SENSOR_ID_AHT20_HUMIDITY 1
#define PKT_SENSOR_ID_SCD41_CO2 2
#define PKT_SENSOR_ID_SGP40_VOC 3
#define PKT_SENSOR_ID_SCD41_TEMP 4
#define PKT_SENSOR_ID_SCD41_HUMIDITY 5
#define PKT_SENSOR_ID_GROVE_BASE 0x10

#define PKT_SENSOR_CAT_TEMP 0
#define PKT_SENSOR_CAT_HUMIDITY 1
#define PKT_SENSOR_CAT_CO2 2
#define PKT_SENSOR_CAT_VOC 3
#define PKT_SENSOR_CAT_LIGHT 4
#define PKT_SENSOR_CAT_PRESSURE 5
#define PKT_SENSOR_CAT_PM 6
#define PKT_SENSOR_CAT_NOX 7
#define PKT_SENSOR_CAT_UNKNOWN 255

void esp32_rp2040_init(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*ESP32_RP2040_H*/

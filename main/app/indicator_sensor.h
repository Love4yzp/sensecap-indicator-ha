/**
 * @file indicator_sensor.h
 * @brief
 * Inplement the Sensor model
 * parase the data from RP2040 and triger View to display
 *
 */
#ifndef SENSOR_H
#define SENSOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "view_data.h"

#define SENSOR_OK				0
#define SENSOR_ERR_INVALID_TYPE -1
#define SENSOR_ERR_MUTEX_FAIL	-2

#ifdef VIEW_DEBUG
extern const char* enum sensor_data_typeStrings[];
#endif

typedef enum
{
	SENSOR_STATUS_OK,
	SENSOR_STATUS_INITED,
	SENSOR_STATUS_WARNING,
	SENSOR_STATUS_ERROR,
} SensorStatus;

// 定义 SensorData 结构
typedef struct
{
	float value; // 传感器的测量值
	enum sensor_data_type type; // 传感器的类型
	SensorStatus status; // 传感器的当前状态
	// bool         timeValid; // 时间戳是否有效
	// uint64_t     timeStamp; // 传感器数据的时间戳
} SensorData;

const char* getSensorName(const enum sensor_data_type type);
float getSensorFloatValue(const enum sensor_data_type type);
int getSensorIntValue(const enum sensor_data_type type);

SensorData* get_current_sensor_data(SensorData* data, enum sensor_data_type type);

int UpdateSensorData(const enum sensor_data_type type, uint8_t* p_data);
void indicator_sensor_init(void);

/* View */
void view_sensor_init();
#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*SENSOR_H*/
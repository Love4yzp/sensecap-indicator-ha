/**
 * @file utility.h
 * @date  29 February 2024

 * @author Spencer Yan
 *
 * @note Description of the file
 *
 * @copyright © 2024, Seeed Studio
 */

#pragma once

#include <Arduino.h>
#include <PacketSerial.h>
#include <Wire.h>

/* Cobs pkt type */
enum pkt_type {
    PKT_TYPE_NONE                 = 0,
    PKT_TYPE_CMD_COLLECT_INTERVAL = 0xA0,  // uin32_t
    PKT_TYPE_CMD_BEEP_ON          = 0xA1,  // uin32_t  ms: on time
    PKT_TYPE_CMD_SHUTDOWN         = 0xA3,
    PKT_TYPE_CMD_POWER_ON         = 0xA4,

    // Inner Sensor
    // PKT_TYPE_SENSOR_SCD41_TEMP      = 0xB0, // not accurate
    // PKT_TYPE_SENSOR_SCD41_HUMIDITY = 0xB1,
    PKT_TYPE_SENSOR_SCD41_CO2 = 0xB2,  // float

    // Inner Sensor SGP40
    PKT_TYPE_SENSOR_SGP40_TVOC_INDEX = 0xB3,  // float

    // SHT41 AHT
    PKT_TYPE_SENSOR_SHT41_TEMP     = 0xB4,
    PKT_TYPE_SENSOR_SHT41_HUMIDITY = 0xB5,  // float
};

void sensor_data_send(PacketSerial& _PacketSerial, enum pkt_type type, float data);

// Sensors
typedef struct {
    float humidity;
    float temperature;
} AHTData;

typedef struct {
    uint16_t srawVoc;
    int32_t  vocIndex;
} SPG40Data;

typedef struct {
    uint16_t co2;
    float    humidity;
    float    temperature;
} SCD4XData;


/************************ aht  temp & humidity ****************************/

void sensor_aht_init(void);
bool sensor_aht_get(AHTData& data);
void sensor_aht_print(const AHTData& data);
/************************ sgp40 tvoc  ****************************/

void sensor_sgp40_init(void);
bool sensor_sgp40_get(SPG40Data& data, const uint16_t compensationRh, const uint16_t compensationT);
void sensor_sgp40_print(const SPG40Data& data);

/************************ scd4x  co2 ****************************/

void sensor_scd4x_init(void);
bool sensor_scd4x_get(SCD4XData& data);
void sensor_scd4x_print(const SCD4XData& data);

/************************ grove  ****************************/
void grove_adc_get(void);  // todo
/************************ beep ****************************/
void beep_init(void);
void beep_off(void);
void beep_on(void);  // Give it a beep
/************************ Format printer ****************************/
void printUint16Hex(uint16_t value);
void printSerialNumber(uint16_t serial0, uint16_t serial1, uint16_t serial2);
/************************ Sensor Power ****************************/
void sensor_power_on(void);
void sensor_power_off(void);
/************************ Delay ****************************/
class NonBlockingDelay {
   private:
    unsigned long previousMillis;  // Last time
    unsigned long interval;  // interval to wait

   public:
    NonBlockingDelay(unsigned long interval) : interval(interval), previousMillis(0) {}

    /**
   * @brief if it is time to execute the function(similar to a timer)
   * 
   * @return true 
   * @return false 
   */
    bool check()
    {
        unsigned long currentMillis = millis();
        if (currentMillis - previousMillis >= interval) {
            previousMillis = currentMillis;  // update time
            return true;  // exceute time
        }
        return false;  // not the time yet
    }

    /**
   * @brief Set the Interval time
   * 
   * @param newInterval 
   */
    void setInterval(unsigned long newInterval) { interval = newInterval; }
};

/************************ END ****************************/
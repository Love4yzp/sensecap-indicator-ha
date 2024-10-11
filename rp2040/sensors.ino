/**
 * @file sensors.ino
 * @author Spencer
 * @brief Sensor instances
 * @version 0.1
 * @date 2024-02-29
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#include <SensirionI2CScd4x.h>
#include <SensirionI2CSen5x.h>
#include <SensirionI2CSfa3x.h>
#include <SensirionI2CSgp40.h>
#include <VOCGasIndexAlgorithm.h>
#include "AHT20.h"
#include "indicator_rp2040.hpp"

/************************ instance  ****************************/
AHT20             AHT;    // grove-sensor: humi & temp
SensirionI2CSgp40 sgp40;  // tvoc
SensirionI2CScd4x scd4x;  // co2

/************************ Sensor Power ****************************/
// The built-in sensor needs to be powered on
void sensor_power_on(void)
{
    pinMode(18, OUTPUT);
    digitalWrite(18, HIGH);
}

void sensor_power_off(void)
{
    pinMode(18, OUTPUT);
    digitalWrite(18, LOW);
}

/************************ grove  adc ****************************/
void grove_adc_get(void)
{
    String dataString = "";
    int    adc0       = analogRead(26);
    dataString += String(adc0);
    dataString += ',';
    int adc1 = analogRead(27);
    dataString += String(adc1);
    Serial.print("grove adc: ");
    Serial.println(dataString);
}

/********************* sensor data send to  esp32 ***********************/
void sensor_data_send(PacketSerial& packetSerial, pkt_type type, float data)
{
    uint8_t data_buf[32];
    data_buf[0] = static_cast<uint8_t>(type);  // Assign packet type to the first byte

    // Copy float data starting from the second byte
    memcpy(&data_buf[1], &data, sizeof(float));

    // Calculate the total number of bytes to send
    int packetLength = 1 + sizeof(float);  // 1 byte for type + bytes for float

    packetSerial.send(data_buf, packetLength);  // Send the data

    // Debugging output, if DEBUG is defined
#if DEBUG
    Serial.print("---> send len:");
    Serial.print(packetLength);
    Serial.print(", data: ");
    for (int i = 0; i < packetLength; i++) {
        Serial.printf("0x%x ", data_buf[i]);
    }
    Serial.println();
#endif
}

/************************ aht  temp & humidity ****************************/

void sensor_aht_init(void)
{
    AHT.begin();
}

bool sensor_aht_get(AHTData& data)
{
    float humi, temp;
    int   ret        = AHT.getSensor(&humi, &temp);
    data.humidity    = humi * 100;
    data.temperature = temp;
    if (ret == 0) {  // GET DATA FAIL
        Serial.println("GET DATA FROM AHT20 FAIL");
        return false;
    }
    return true;
}

void sensor_aht_print(const AHTData& data)
{
    Serial.printf("AHT20: humidity: %.2f % \t temperature: %.2f\n", data.humidity, data.temperature);
    // Serial.print("humidity:");
    // Serial.print(data.humidity * 100);
    // Serial.print("%\t temperature: ");
    // Serial.println(data.temperature);
}

/************************ sgp40 tvoc  ****************************/
int32_t              index_offset;
int32_t              learning_time_offset_hours;
int32_t              learning_time_gain_hours;
int32_t              gating_max_duration_minutes;
int32_t              std_initial;
int32_t              gain_factor;
VOCGasIndexAlgorithm voc_algorithm;

void sensor_sgp40_init(void)
{
    uint16_t error;
    char     errorMessage[256];

    sgp40.begin(Wire);

    uint16_t serialNumber[3];
    uint8_t  serialNumberSize = 3;

    error = sgp40.getSerialNumber(serialNumber, serialNumberSize);

    if (error) {
        Serial.print("Error trying to execute getSerialNumber(): ");
        errorToString(error, errorMessage, 256);
        Serial.println(errorMessage);
    } else {
        Serial.print("SerialNumber:");
        Serial.print("0x");
        for (size_t i = 0; i < serialNumberSize; i++) {
            uint16_t value = serialNumber[i];
            Serial.print(value < 4096 ? "0" : "");
            Serial.print(value < 256 ? "0" : "");
            Serial.print(value < 16 ? "0" : "");
            Serial.print(value, HEX);
        }
        Serial.println();
    }

    uint16_t testResult;
    error = sgp40.executeSelfTest(testResult);
    if (error) {
        Serial.print("Error trying to execute executeSelfTest(): ");
        errorToString(error, errorMessage, 256);
        Serial.println(errorMessage);
    } else if (testResult != 0xD400) {
        Serial.print("executeSelfTest failed with error: ");
        Serial.println(testResult);
    }

    voc_algorithm.get_tuning_parameters(index_offset, learning_time_offset_hours, learning_time_gain_hours, gating_max_duration_minutes, std_initial, gain_factor);

    Serial.println("\nVOC Gas Index Algorithm parameters");
    Serial.print("Index offset:\t");
    Serial.println(index_offset);
    Serial.print("Learing time offset hours:\t");
    Serial.println(learning_time_offset_hours);
    Serial.print("Learing time gain hours:\t");
    Serial.println(learning_time_gain_hours);
    Serial.print("Gating max duration minutes:\t");
    Serial.println(gating_max_duration_minutes);
    Serial.print("Std inital:\t");
    Serial.println(std_initial);
    Serial.print("Gain factor:\t");
    Serial.println(gain_factor);
}

bool sensor_sgp40_get(SPG40Data& data, const uint16_t compensationRh, const uint16_t compensationT)
{
    data.srawVoc   = 0;  // Initialize with default value
    uint16_t error = sgp40.measureRawSignal(compensationRh, compensationT, data.srawVoc);

    if (error != 0) {
        char errorMessage[256];
        Serial.print("Error trying to execute measureRawSignal(): ");
        errorToString(error, errorMessage, 256);
        Serial.println(errorMessage);
        return false;  // Indicate failure
    }

    data.vocIndex = voc_algorithm.process(data.srawVoc);  // Store the VOC index in the data structure

    return true;  // Indicate success
}

void sensor_sgp40_print(const SPG40Data& data)
{
    Serial.print("sensor sgp40: ");
    if (data.srawVoc != 0) {  // Assuming a SRAW_VOC value of 0 is considered invalid
        Serial.printf("SRAW_VOC: %d\tVOC Index: %d\n", data.srawVoc, data.vocIndex);
        // Serial.print("SRAW_VOC: ");
        // Serial.println(data.srawVoc);
        // Serial.print("VOC Index: ");
        // Serial.println(data.vocIndex);
    } else {
        Serial.println("Invalid SPG40 data.");
    }
}

/************************ scd4x  co2 ****************************/

void sensor_scd4x_init(void)
{
    uint16_t error;
    char     errorMessage[256];

    scd4x.begin(Wire);

    // stop potentially previously started measurement
    error = scd4x.stopPeriodicMeasurement();
    if (error) {
        Serial.print("Error trying to execute stopPeriodicMeasurement(): ");
        errorToString(error, errorMessage, 256);
        Serial.println(errorMessage);
    }

    uint16_t serial0;
    uint16_t serial1;
    uint16_t serial2;
    error = scd4x.getSerialNumber(serial0, serial1, serial2);
    if (error) {
        Serial.print("Error trying to execute getSerialNumber(): ");
        errorToString(error, errorMessage, 256);
        Serial.println(errorMessage);
    } else {
        printSerialNumber(serial0, serial1, serial2);
    }

    // Start Measurement
    error = scd4x.startPeriodicMeasurement();
    if (error) {
        Serial.print("Error trying to execute startPeriodicMeasurement(): ");
        errorToString(error, errorMessage, 256);
        Serial.println(errorMessage);
    }
    // scd4x.powerDown();
}
bool sensor_scd4x_get(SCD4XData& data)
{
    // Read Measurement
    uint16_t error = scd4x.readMeasurement(data.co2, data.temperature, data.humidity);
    if (error != 0) {
        char errorMessage[256];
        Serial.print("Error trying to execute readMeasurement(): ");
        errorToString(error, errorMessage, 256);
        Serial.println(errorMessage);
        return false;  // Indicate failure
    } else if (data.co2 == 0) {
        Serial.println("Invalid sample detected, skipping.");
        return false;  // Considered a failure due to invalid sample
    }
    return true;  // Indicate success
}

void sensor_scd4x_print(const SCD4XData& data)
{
    // Check if the data is valid; adjust according to your needs
    Serial.print("sensor scd4x: ");
    if (data.co2 != 0) {  // Assuming a CO2 value of 0 is invalid
        Serial.print("Co2: ");
        Serial.print(data.co2);
        Serial.print("\tTemperature: ");
        Serial.print(data.temperature);
        Serial.print("\tHumidity: ");
        Serial.println(data.humidity);
    } else {
        Serial.println("Invalid SCD4X data.");
    }
}
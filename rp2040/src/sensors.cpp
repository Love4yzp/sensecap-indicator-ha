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
#include "AHT20.h"
#include <math.h>
#include <SensirionI2CSgp40.h>
#include <SensirionI2cScd4x.h>
#include <string.h>
#include <VOCGasIndexAlgorithm.h>
#include "indicator_rp2040.hpp"

/************************ instance  ****************************/
AHT20             AHT;    // grove-sensor: humi & temp
SensirionI2CSgp40 sgp40;  // tvoc
SensirionI2cScd4x scd4x;  // co2

static uint8_t sht41_address = 0x00;

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
static uint8_t sensor_packet_checksum(const uint8_t* data, size_t len)
{
    uint8_t checksum = 0;
    for (size_t i = 0; i < len; i++) {
        checksum ^= data[i];
    }
    return checksum;
}

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

void sensor_attached_send(PacketSerial& packetSerial, uint8_t id, uint8_t category, const char* name, const char* unit)
{
    uint8_t name_len = static_cast<uint8_t>(strlen(name));
    uint8_t unit_len = static_cast<uint8_t>(strlen(unit));
    if (name_len > 15) {
        name_len = 15;
    }
    if (unit_len > 7) {
        unit_len = 7;
    }

    uint8_t data_buf[5 + 16 + 8] = {0};
    size_t  offset               = 0;
    data_buf[offset++]           = static_cast<uint8_t>(PKT_TYPE_SENSOR_ATTACHED);
    data_buf[offset++]           = id;
    data_buf[offset++]           = category;
    data_buf[offset++]           = name_len;
    memcpy(&data_buf[offset], name, name_len);
    offset += name_len;
    data_buf[offset++] = unit_len;
    memcpy(&data_buf[offset], unit, unit_len);
    offset += unit_len;
    data_buf[offset++] = sensor_packet_checksum(data_buf, offset);

    packetSerial.send(data_buf, offset);
}

void sensor_detached_send(PacketSerial& packetSerial, uint8_t id)
{
    uint8_t data_buf[3] = {
        static_cast<uint8_t>(PKT_TYPE_SENSOR_DETACHED),
        id,
        0,
    };
    data_buf[2] = sensor_packet_checksum(data_buf, 2);
    packetSerial.send(data_buf, sizeof(data_buf));
}

void sensor_value_send(PacketSerial& packetSerial, uint8_t id, float data)
{
    uint8_t data_buf[7] = {0};
    data_buf[0]         = static_cast<uint8_t>(PKT_TYPE_SENSOR_VALUE);
    data_buf[1]         = id;
    memcpy(&data_buf[2], &data, sizeof(float));
    data_buf[6] = sensor_packet_checksum(data_buf, 6);
    packetSerial.send(data_buf, sizeof(data_buf));
}

/************************ aht  temp & humidity ****************************/

void sensor_aht_init(void)
{
    AHT.begin();
}

bool sensor_aht_get(AHTData& data)
{
    data.humidity    = AHT.getHumidity();
    data.temperature = AHT.getTemperature();
    if (isnan(data.humidity) || isnan(data.temperature)) {
        Serial.println("GET DATA FROM AHT20 FAIL");
        return false;
    }
    return true;
}

static uint8_t sht41_crc8(const uint8_t* data, size_t len)
{
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

static bool sht41_probe_address(uint8_t address)
{
    Wire.beginTransmission(address);
    Wire.write(0x89);  // read serial number
    if (Wire.endTransmission() != 0) {
        return false;
    }
    delay(1);
    if (Wire.requestFrom(address, (uint8_t)6) != 6) {
        return false;
    }
    uint8_t data[6];
    for (uint8_t i = 0; i < sizeof(data); i++) {
        data[i] = Wire.read();
    }
    return sht41_crc8(&data[0], 2) == data[2] && sht41_crc8(&data[3], 2) == data[5];
}

bool sensor_sht41_init(void)
{
    const uint8_t addresses[] = {0x44, 0x45, 0x46};
    sht41_address = 0x00;
    for (uint8_t i = 0; i < sizeof(addresses); i++) {
        if (sht41_probe_address(addresses[i])) {
            sht41_address = addresses[i];
            Wire.beginTransmission(sht41_address);
            Wire.write(0x94);  // soft reset
            Wire.endTransmission();
            delay(2);
            Serial.printf("SHT41 found at 0x%02X\n", sht41_address);
            return true;
        }
    }
    Serial.println("SHT41 not found");
    return false;
}

bool sensor_sht41_get(AHTData& data)
{
    if (sht41_address == 0x00) {
        return false;
    }

    Wire.beginTransmission(sht41_address);
    Wire.write(0xFD);  // high precision measurement
    if (Wire.endTransmission() != 0) {
        return false;
    }
    delay(36);

    if (Wire.requestFrom(sht41_address, (uint8_t)6) != 6) {
        return false;
    }
    uint8_t raw[6];
    for (uint8_t i = 0; i < sizeof(raw); i++) {
        raw[i] = Wire.read();
    }
    if (sht41_crc8(&raw[0], 2) != raw[2] || sht41_crc8(&raw[3], 2) != raw[5]) {
        return false;
    }

    uint16_t raw_temp = ((uint16_t)raw[0] << 8) | raw[1];
    uint16_t raw_humi = ((uint16_t)raw[3] << 8) | raw[4];
    data.temperature = -45.0f + 175.0f * ((float)raw_temp / 65535.0f);
    data.humidity    = -6.0f + 125.0f * ((float)raw_humi / 65535.0f);
    if (data.humidity < 0.0f) {
        data.humidity = 0.0f;
    }
    if (data.humidity > 100.0f) {
        data.humidity = 100.0f;
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

    scd4x.begin(Wire, 0x62);

    // stop potentially previously started measurement
    error = scd4x.stopPeriodicMeasurement();
    if (error) {
        Serial.print("Error trying to execute stopPeriodicMeasurement(): ");
        errorToString(error, errorMessage, 256);
        Serial.println(errorMessage);
    }

    uint64_t serialNumber;
    error = scd4x.getSerialNumber(serialNumber);
    if (error) {
        Serial.print("Error trying to execute getSerialNumber(): ");
        errorToString(error, errorMessage, 256);
        Serial.println(errorMessage);
    } else {
        Serial.print("Serial: 0x");
        Serial.println(static_cast<unsigned long long>(serialNumber), HEX);
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

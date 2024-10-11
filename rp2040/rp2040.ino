/**
 * @file rp2040.ino
 * @date  01 March 2024

 * @author Spencer Yan
 *
 * @note :
 * Product Wiki:
https://wiki.seeedstudio.com/Sensor/SenseCAP/SenseCAP_Indicator/Get_started_with_SenseCAP_Indicator/
 * System Diagram:
https://files.seeedstudio.com/wiki/SenseCAP/SenseCAP_Indicator/SenseCAP_Indicator_6.png
 * Grove Ports:
https://files.seeedstudio.com/wiki/SenseCAP/SenseCAP_Indicator/grove.png
 *
 * @copyright © 2024, Seeed Studio
 */

#include "indicator_rp2040.hpp"

PacketSerial packetSerial;

/************************ recv cmd from esp32  ****************************/

/**
 * @brief the function when received data(pre-defined) from ESP32 
 * @param buffer
 * @param size
*/
void onPacketReceived(const uint8_t* buffer, size_t size)
{
#if DEBUG
    Serial.printf("<--- recv len:%d, data: ", size);
    for (int i = 0; i < size; i++) {
        Serial.printf("0x%x ", buffer[i]);
    }
    Serial.println("");
#endif
    if (size < 1) {
        return;
    }
    switch (buffer[0]) {
        case PKT_TYPE_CMD_SHUTDOWN: {
            Serial.println("cmd shutdown");
            sensor_power_off();
            break;
        }
        case PKT_TYPE_CMD_BEEP_ON:
            beep_on();
            break;
        default:
            break;
    }
}

/************************ setuo & loop ****************************/

// static bool sd_init_flag = 0;

void setup()
{
    Serial.begin(115200);  // Connect to PC via USB

    // The Serial connect to inner ESP32S3
    Serial1.setRX(17);  //
    Serial1.setTX(16);
    Serial1.begin(115200);
    // Prepare PacketSerial, which will reduce the sent data size
    packetSerial.setStream(&Serial1);
    packetSerial.setPacketHandler(&onPacketReceived);

    // The built-in sensor needs to be powered on
    sensor_power_on();

    // I2C init
    Wire.setSDA(20);
    Wire.setSCL(21);
    Wire.begin();

    beep_init();
    // beep_on();

    sensor_aht_init();
    sensor_sgp40_init();
    sensor_scd4x_init();
}

/*************************** delay *****************************/
// NonBlockingDelay adc_delay(3800);
NonBlockingDelay aht_delay(2000);  //
NonBlockingDelay sgp40_delay(3800);  // Tvoc
NonBlockingDelay scd4x_delay(3800);  // CO2 Above 3500 would be good

/*************************** data to send *****************************/
AHTData   data_aht = {0};
SPG40Data data_spg = {0};
SCD4XData data_scd = {0};

/************************ compensation  ****************************/
uint16_t defaultCompenstaionRh = 0x8000;
uint16_t defaultCompenstaionT  = 0x6666;

#define USING_AHT_COMPENSATION 1
uint16_t SPG4x_compensationRh = defaultCompenstaionRh;
uint16_t SPG4x_compensationT  = defaultCompenstaionT;

/**
 * @brief Calibrate the sensor data
 *
 * @param rawValue the raw value
 * @param scaleFactor the scale factor default is 1.0
 * @param offset  the offset default is 0.0
 * @return float
 */
float calibrateSensor(float rawValue, float scaleFactor = 1.0, float offset = 0.0)
{
    float calibratedValue = (rawValue * scaleFactor) + offset;
    return calibratedValue;
}

/************************ END ****************************/

void loop()
{
    if (scd4x_delay.check() && sensor_scd4x_get(data_scd)) {  // External Sensor, only use CO2; As
        // Temp and Humi is not accurate.
        sensor_scd4x_print(data_scd);

        // don't need to calibrate the co2
        float co2 = calibrateSensor(static_cast<float>(data_scd.co2));
        sensor_data_send(packetSerial, PKT_TYPE_SENSOR_SCD41_CO2,
                         co2);  // uint16_t to float
    }

// Temporarily use SPG40 to get the compensation data
#if USING_AHT_COMPENSATION
    if (sgp40_delay.check() && sensor_sgp40_get(data_spg, SPG4x_compensationRh, SPG4x_compensationT))
#else
    SPG4x_compensationRh = defaultCompenstaionRh;  // modify as you need
    SPG4x_compensationT  = defaultCompenstaionT;  // modify as you need
    if (sgp40_delay.check() && sensor_sgp40_get(data_spg, SPG4x_compensationRh, SPG4x_compensationT))
#endif
    {
        sensor_sgp40_print(data_spg);
        // get the vocIndex from SPG40
        float voxIndex = calibrateSensor(static_cast<float>(data_spg.vocIndex));
        sensor_data_send(packetSerial, PKT_TYPE_SENSOR_SGP40_TVOC_INDEX, voxIndex);
    }

    if (aht_delay.check() && sensor_aht_get(data_aht)) {  // External Sensor
        sensor_aht_print(data_aht);

        // get the *calibration* data
        float humidity = calibrateSensor(static_cast<float>(data_aht.humidity), 1, 0.0);  // humidity = 1 * raw_humidity + 0
        sensor_data_send(packetSerial, PKT_TYPE_SENSOR_SHT41_HUMIDITY, humidity);  // send the humidity to ESP32

        // get the temperature from ATH
        float temperature = calibrateSensor(static_cast<float>(data_aht.temperature));
        sensor_data_send(packetSerial, PKT_TYPE_SENSOR_SHT41_TEMP, temperature);

#if USING_AHT_COMPENSATION
        SPG4x_compensationRh = static_cast<uint16_t>(data_aht.humidity * 65535 / 100);
        SPG4x_compensationT  = static_cast<uint16_t>((data_aht.temperature + 45) * 65535 / 175);
#endif
    }
}

void loop1()
{
    packetSerial.update();
    if (packetSerial.overflow()) {}
}
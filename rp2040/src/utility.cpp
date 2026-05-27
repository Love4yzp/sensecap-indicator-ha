#include "indicator_rp2040.hpp"

/************************ beep ****************************/
const int Buzzer = 19;

void beep_init(void)
{
    pinMode(Buzzer, OUTPUT);
}
void beep_off(void)
{
    digitalWrite(19, LOW);
}
void beep_on(void)
{
    analogWrite(Buzzer, 127);
    delay(50);
    analogWrite(Buzzer, 0);
}

/************************ Format printer ****************************/
void printUint16Hex(uint16_t value)
{
    Serial.print(value < 4096 ? "0" : "");
    Serial.print(value < 256 ? "0" : "");
    Serial.print(value < 16 ? "0" : "");
    Serial.print(value, HEX);
}

void printSerialNumber(uint16_t serial0, uint16_t serial1, uint16_t serial2)
{
    Serial.print("Serial: 0x");
    printUint16Hex(serial0);
    printUint16Hex(serial1);
    printUint16Hex(serial2);
    Serial.println();
}
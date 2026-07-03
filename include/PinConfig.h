#pragma once

// I2C to DS3231 (arduino-esp32 defaults)
#define PIN_I2C_SDA 21
#define PIN_I2C_SCL 22

// DS3231 INT/SQW -> ESP32 deep-sleep wake source.
// GPIO33 is RTC-capable (valid ext0 wakeup pin), not a strapping/flash pin.
// SQW is open-drain: make sure there is a pull-up to 3.3V (most DS3231
// breakout boards already include one).
#define PIN_RTC_INT GPIO_NUM_33

// BTS7960 (IBT-2) motor driver
#define PIN_MOTOR_R_EN 25
#define PIN_MOTOR_L_EN 26
#define PIN_MOTOR_RPWM 27
#define PIN_MOTOR_LPWM 14
// R_IS / L_IS (current sense) intentionally left unconnected - not used.

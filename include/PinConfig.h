#pragma once

// I2C to DS3231 (arduino-esp32 defaults)
#define PIN_I2C_SDA 21
#define PIN_I2C_SCL 22

// DS3231 INT/SQW -> ESP32 deep-sleep wake source.
// GPIO15 is RTC-capable (valid ext0 wakeup pin). It is a strapping pin
// (MTDO, boot log verbosity only - not flash voltage or download mode),
// so at worst a reset that lands while the RTC alarm is asserted will
// silence that boot's serial log; it has no effect on wake/scheduling.
// SQW is open-drain: make sure there is a pull-up to 3.3V (most DS3231
// breakout boards already include one).
#define PIN_RTC_INT GPIO_NUM_15

// BTS7960 (IBT-2) motor driver
#define PIN_MOTOR_R_EN 26
#define PIN_MOTOR_L_EN 27
#define PIN_MOTOR_RPWM 33
#define PIN_MOTOR_LPWM 25
// R_IS / L_IS (current sense) intentionally left unconnected - not used.

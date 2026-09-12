#include "DoorController.h"

#include <Arduino.h>

#include "Debug.h"
#include "config.h"

DoorController::DoorController(ConfigStore& store, RtcManager& rtc) : store_(store), rtc_(rtc) {}

void DoorController::begin() {
    pinMode(PIN_MOTOR_IN1, OUTPUT);
    pinMode(PIN_MOTOR_IN2, OUTPUT);
    pinMode(PIN_MOTOR_SLEEP, OUTPUT);
    stopMotor();
}

void DoorController::open(Config& cfg) { run(cfg, DoorAction::OPENED, /*rpwmHigh=*/true); }

void DoorController::close(Config& cfg) { run(cfg, DoorAction::CLOSED, /*rpwmHigh=*/false); }

void DoorController::run(Config& cfg, DoorAction action, bool rpwmHigh) {
    
    TRACE(action == DoorAction::OPENED ? "[Door] opening" : "[Door] closing");
    
    // Direction: verify against actual wiring during hardware bring-up and
    // swap RPWM/LPWM below if "open" and "close" are reversed.
    bool invert = cfg.motorInvertDirection;
    digitalWrite(PIN_MOTOR_IN1, (rpwmHigh && !invert) || (!rpwmHigh && invert) ? HIGH : LOW);
    digitalWrite(PIN_MOTOR_IN2, (rpwmHigh && !invert) || (!rpwmHigh && invert) ? LOW : HIGH);

    // Blink the status LED at 2Hz (250ms half-period) for the duration of
    // the move instead of a single blocking delay, so "door moving" is
    // visible without pulling in a timer/thread.
    constexpr unsigned long kBlinkHalfPeriodMs = 250;
    bool ledOn = true;
    for (unsigned long remaining = cfg.motorRunMs; remaining > 0;) {
        unsigned long step = remaining < kBlinkHalfPeriodMs ? remaining : kBlinkHalfPeriodMs;
        delay(step);
        remaining -= step;
        ledOn = !ledOn;
        digitalWrite(PIN_STATUS_LED, ledOn ? HIGH : LOW);
    }

    stopMotor();

    // Movement done but device is still awake - back to solid on.
    digitalWrite(PIN_STATUS_LED, HIGH);

    cfg.lastOperationAction = action;
    cfg.lastOperationUnixTime = rtc_.isTimeValid() ? rtc_.now().unixtime() : 0;
    store_.save(cfg);

    TRACE(action == DoorAction::OPENED ? "[Door] opened" : "[Door] closed");
}

void DoorController::stopMotor() {
    digitalWrite(PIN_MOTOR_IN1, LOW);
    digitalWrite(PIN_MOTOR_IN2, LOW);
}

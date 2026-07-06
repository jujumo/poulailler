#include "DoorController.h"

#include <Arduino.h>

#include "Debug.h"
#include "config.h"

DoorController::DoorController(ConfigStore& store) : store_(store) {}

void DoorController::begin() {
    pinMode(PIN_MOTOR_R_EN, OUTPUT);
    pinMode(PIN_MOTOR_L_EN, OUTPUT);
    pinMode(PIN_MOTOR_RPWM, OUTPUT);
    pinMode(PIN_MOTOR_LPWM, OUTPUT);
    stopMotor();
}

void DoorController::open(Config& cfg, bool force) {
    run(cfg, DoorState::OPEN, /*rpwmHigh=*/true, force);
}

void DoorController::close(Config& cfg, bool force) {
    run(cfg, DoorState::CLOSED, /*rpwmHigh=*/false, force);
}

void DoorController::run(Config& cfg, DoorState target, bool rpwmHigh, bool force) {
    if (!force && cfg.doorState == target) {
        return;  // already there - idempotent unless explicitly forced
    }

    TRACE(target == DoorState::OPEN ? "[Door] opening" : "[Door] closing");

    // Write-ahead: record "in motion / unsure" before the risky part, so a
    // brownout mid-move leaves an honest UNKNOWN state rather than a stale
    // wrong OPEN/CLOSED.
    cfg.doorState = DoorState::UNKNOWN;
    store_.save(cfg);

    digitalWrite(PIN_MOTOR_R_EN, HIGH);
    digitalWrite(PIN_MOTOR_L_EN, HIGH);
    // Direction: verify against actual wiring during hardware bring-up and
    // swap RPWM/LPWM below if "open" and "close" are reversed.
    digitalWrite(PIN_MOTOR_RPWM, rpwmHigh ? HIGH : LOW);
    digitalWrite(PIN_MOTOR_LPWM, rpwmHigh ? LOW : HIGH);

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

    cfg.doorState = target;
    store_.save(cfg);

    TRACE(target == DoorState::OPEN ? "[Door] opened" : "[Door] closed");
}

void DoorController::stopMotor() {
    digitalWrite(PIN_MOTOR_RPWM, LOW);
    digitalWrite(PIN_MOTOR_LPWM, LOW);
    digitalWrite(PIN_MOTOR_R_EN, LOW);
    digitalWrite(PIN_MOTOR_L_EN, LOW);
}

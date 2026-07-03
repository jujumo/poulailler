#include "DoorController.h"

#include <Arduino.h>

#include "PinConfig.h"

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

    delay(cfg.motorRunMs);

    stopMotor();

    cfg.doorState = target;
    store_.save(cfg);
}

void DoorController::stopMotor() {
    digitalWrite(PIN_MOTOR_RPWM, LOW);
    digitalWrite(PIN_MOTOR_LPWM, LOW);
    digitalWrite(PIN_MOTOR_R_EN, LOW);
    digitalWrite(PIN_MOTOR_L_EN, LOW);
}

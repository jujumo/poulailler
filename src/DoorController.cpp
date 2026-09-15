#include "DoorController.h"

#include <Arduino.h>

#include "Debug.h"
#include "config.h"

DoorController::DoorController(ConfigStore& store, RtcManager& rtc) : store_(store), rtc_(rtc) {}

void DoorController::begin() {
    pinMode(PIN_MOTOR_IN1, OUTPUT);
    pinMode(PIN_MOTOR_IN2, OUTPUT);
    pinMode(PIN_MOTOR_SLEEP, OUTPUT);
    digitalWrite(PIN_MOTOR_SLEEP, HIGH);
    stopMotor();
    TRACEF("[Door] driver enabled sleep=%d in1=%d in2=%d", digitalRead(PIN_MOTOR_SLEEP),
           digitalRead(PIN_MOTOR_IN1), digitalRead(PIN_MOTOR_IN2));
}

void DoorController::signalReady(const Config& cfg) {
    constexpr unsigned long kSignalPulseMs = 100;

    for (int pulse = 0; pulse < 2; ++pulse) {
        setDirection(DoorAction::OPENED, cfg.motorInvertDirection);
        delay(kSignalPulseMs);
        stopMotor();

        setDirection(DoorAction::CLOSED, cfg.motorInvertDirection);
        delay(kSignalPulseMs);
        stopMotor();
    }
}

void DoorController::open(Config& cfg) { run(cfg, DoorAction::OPENED); }

void DoorController::close(Config& cfg) { run(cfg, DoorAction::CLOSED); }

void DoorController::setDirection(DoorAction action, bool invert) {
    bool directionHigh = action == DoorAction::OPENED;
    bool driveHigh = (directionHigh && !invert) || (!directionHigh && invert);
    digitalWrite(PIN_MOTOR_IN1, driveHigh ? HIGH : LOW);
    digitalWrite(PIN_MOTOR_IN2, driveHigh ? LOW : HIGH);
}

void DoorController::run(Config& cfg, DoorAction action) {
    
    TRACE(action == DoorAction::OPENED ? "[Door] opening" : "[Door] closing");
    
    // Direction: verify against actual wiring during hardware bring-up and
    // swap RPWM/LPWM below if "open" and "close" are reversed.
    setDirection(action, cfg.motorInvertDirection);
        TRACEF("[Door] command sleep=%d in1=%d in2=%d runMs=%lu", digitalRead(PIN_MOTOR_SLEEP),
            digitalRead(PIN_MOTOR_IN1), digitalRead(PIN_MOTOR_IN2),
            static_cast<unsigned long>(action == DoorAction::OPENED ? cfg.motorOpenDurationMs
                                                                      : cfg.motorCloseDurationMs));

    // Blink the status LED at 2Hz (250ms half-period) for the duration of
    // the move instead of a single blocking delay, so "door moving" is
    // visible without pulling in a timer/thread.
    constexpr unsigned long kBlinkHalfPeriodMs = 250;
    bool ledOn = true;
    unsigned long remaining = action == DoorAction::OPENED ? cfg.motorOpenDurationMs
                                                            : cfg.motorCloseDurationMs;
    for (; remaining > 0;) {
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

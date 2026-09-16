#include "DoorController.h"

#include <Arduino.h>

#include "Debug.h"
#include "config.h"

DoorController::DoorController(ConfigStore& store, RtcManager& rtc) : store_(store), rtc_(rtc) {}

void DoorController::begin() {
    pinMode(PIN_MOTOR_IN1, OUTPUT);
    pinMode(PIN_MOTOR_IN2, OUTPUT);
    pinMode(PIN_MOTOR_SLEEP, OUTPUT);
    stopMoving();
    digitalWrite(PIN_MOTOR_SLEEP, LOW);
    TRACEF("[Door] driver enabled sleep=%d in1=%d in2=%d", digitalRead(PIN_MOTOR_SLEEP),
           digitalRead(PIN_MOTOR_IN1), digitalRead(PIN_MOTOR_IN2));
}

void DoorController::jitter(const Config& cfg) {
    constexpr unsigned long kSignalPulseMs = 100;

    digitalWrite(PIN_MOTOR_SLEEP, HIGH);
    for (int pulse = 0; pulse < 2; ++pulse) {
        startMoving(cfg.motorInvertDirection ? Direction::CLOSE : Direction::OPEN);
        delay(kSignalPulseMs);
        stopMoving();

        startMoving(cfg.motorInvertDirection ? Direction::OPEN : Direction::CLOSE);
        delay(kSignalPulseMs);
        stopMoving();
    }
    digitalWrite(PIN_MOTOR_SLEEP, LOW);
}

void DoorController::open(Config& cfg) {
    const Direction direction = cfg.motorInvertDirection ? Direction::CLOSE : Direction::OPEN;
    TRACE("[Door] opening");
    operateDoor(cfg.motorOpenDurationMs, direction);

    cfg.lastOperationAction = DoorAction::OPENED;
    cfg.lastOperationUnixTime = rtc_.isTimeValid() ? rtc_.now().unixtime() : 0;
    store_.save(cfg);
    TRACE("[Door] opened");
}

void DoorController::close(Config& cfg) {
    const Direction direction = cfg.motorInvertDirection ? Direction::OPEN : Direction::CLOSE;
    TRACE("[Door] closing");
    operateDoor(cfg.motorCloseDurationMs, direction);

    cfg.lastOperationAction = DoorAction::CLOSED;
    cfg.lastOperationUnixTime = rtc_.isTimeValid() ? rtc_.now().unixtime() : 0;
    store_.save(cfg);
    TRACE("[Door] closed");
}

void DoorController::startMoving(Direction direction) {
    bool openIn1High = direction == Direction::OPEN;
    digitalWrite(PIN_MOTOR_IN1, openIn1High ? HIGH : LOW);
    digitalWrite(PIN_MOTOR_IN2, openIn1High ? LOW : HIGH);
}

void DoorController::stopMoving() {
    digitalWrite(PIN_MOTOR_IN1, LOW);
    digitalWrite(PIN_MOTOR_IN2, LOW);
}


void DoorController::operateDoor(uint32_t durationMs, Direction direction) {
    // Direction: verify against actual wiring during hardware bring-up and
    // swap RPWM/LPWM below if "open" and "close" are reversed.
    digitalWrite(PIN_MOTOR_SLEEP, HIGH);
    startMoving(direction);
        TRACEF("[Door] command sleep=%d in1=%d in2=%d runMs=%lu", digitalRead(PIN_MOTOR_SLEEP),
            digitalRead(PIN_MOTOR_IN1), digitalRead(PIN_MOTOR_IN2),
            static_cast<unsigned long>(durationMs));

    // Blink the status LED at 2Hz (250ms half-period) for the duration of
    // the move instead of a single blocking delay, so "door moving" is
    // visible without pulling in a timer/thread.
    constexpr unsigned long kBlinkHalfPeriodMs = 250;
    bool ledOn = true;
    unsigned long remaining = durationMs;
    for (; remaining > 0;) {
        unsigned long step = remaining < kBlinkHalfPeriodMs ? remaining : kBlinkHalfPeriodMs;
        delay(step);
        remaining -= step;
        ledOn = !ledOn;
        digitalWrite(PIN_STATUS_LED, ledOn ? HIGH : LOW);
    }

    stopMoving();
    digitalWrite(PIN_MOTOR_SLEEP, LOW);

    // Movement done - back to solid on while the motor driver sleeps.
    digitalWrite(PIN_STATUS_LED, HIGH);

}

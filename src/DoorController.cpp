#include "DoorController.h"

#include <Arduino.h>
#include "Debug.h"

DoorController::DoorController(Config& config) : config_(config)
{}

void DoorController::begin() 
{
    pinMode(PIN_MOTOR_IN1, OUTPUT);
    pinMode(PIN_MOTOR_IN2, OUTPUT);
    pinMode(PIN_MOTOR_SLEEP, OUTPUT);
    stopMoving();
    digitalWrite(PIN_MOTOR_SLEEP, LOW);
    TRACEF("[Door] driver enabled sleep=%d in1=%d in2=%d", digitalRead(PIN_MOTOR_SLEEP),
           digitalRead(PIN_MOTOR_IN1), digitalRead(PIN_MOTOR_IN2));
}

void DoorController::jitter() 
{
    constexpr unsigned long kSignalPulseMs = 100;

    digitalWrite(PIN_MOTOR_SLEEP, HIGH);
    for (int pulse = 0; pulse < 2; ++pulse) {
        startMoving(Direction::OPEN);
        delay(kSignalPulseMs);
        stopMoving();

        startMoving(Direction::CLOSE);
        delay(kSignalPulseMs);
        stopMoving();
    }
    digitalWrite(PIN_MOTOR_SLEEP, LOW);
}

void DoorController::open() 
{
    TRACE("[Door] opening");
    operateDoor(config_.motor_open_duration_ms, Direction::OPEN);
    TRACE("[Door] opened");
}

void DoorController::close() 
{
    TRACE("[Door] closing");
    operateDoor(config_.motor_close_duration_ms, Direction::CLOSE);
    TRACE("[Door] closed");
}

void DoorController::startMoving(Direction direction) 
{
    bool openIn1High = direction == Direction::OPEN;
    openIn1High = openIn1High != config_.motor_invert_direction;
    TRACEF("[Door] startMoving dir=%s in1=%s.", 
        direction == Direction::OPEN ? "OPEN" : "CLOSE",
        openIn1High ? "True" : "False");
    digitalWrite(PIN_MOTOR_IN1, openIn1High ? HIGH : LOW);
    digitalWrite(PIN_MOTOR_IN2, openIn1High ? LOW : HIGH);
}

void DoorController::stopMoving()
{
    digitalWrite(PIN_MOTOR_IN1, LOW);
    digitalWrite(PIN_MOTOR_IN2, LOW);
}


void DoorController::operateDoor(uint32_t durationMs, Direction direction) 
{
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


const char* doorActionName(DoorAction action)
{
    switch (action) {
        case DoorAction::OPENED:
            return "OPENED";
        case DoorAction::CLOSED:
            return "CLOSED";
        default:
            return "NONE";
    }
}

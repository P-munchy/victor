#ifndef MOTORS_H
#define MOTORS_H

#include "portable.h"

const u8 MOTOR_COUNT = 4;

// Initialize the PWM peripheral on the designated pins in the source file.
void MotorsInit();

// Set the (unitless) power for a specified motor in the range [-798, 798].
void MotorsSetPower(u8 motorID, s16 power);

// Updates the PWM values for the timers in a safe manner
void MotorsUpdate();

#endif

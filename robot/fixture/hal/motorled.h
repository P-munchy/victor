#ifndef __MOTORLED_H
#define __MOTORLED_H

// Set motor speed between 0 and MOTOR_PWM_MAXVAL
void MotorPWM(int pwm);

// Drive one of 16 LEDs on the backpack, then measure voltage on ADC
int LEDTest(u8 led);

#endif

#ifndef MOTOR_CONTROL_H
#define MOTOR_CONTROL_H

#include <Arduino.h>

extern bool handBrake;
extern String currentDir;

void initMotors();
void stopAll();
void emergencyBrake();
void smoothStop();
void motorDrive(int,int,int,int,int,int);
void executeMove(String dir, int spd);

#endif

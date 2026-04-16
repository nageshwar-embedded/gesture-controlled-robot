#include "motor_control.h"
#include "sensor_ultrasonic.h"

bool handBrake = false;
String currentDir = "stop";

#define IN1 12
#define IN2 14
#define IN3 27
#define IN4 26
#define ENA 13
#define ENB 25

void initMotors() {
  pinMode(IN1,OUTPUT); pinMode(IN2,OUTPUT);
  pinMode(IN3,OUTPUT); pinMode(IN4,OUTPUT);
  pinMode(ENA,OUTPUT); pinMode(ENB,OUTPUT);
}

void stopAll() {
  digitalWrite(IN1,LOW); digitalWrite(IN2,LOW);
  digitalWrite(IN3,LOW); digitalWrite(IN4,LOW);
  analogWrite(ENA,0); analogWrite(ENB,0);
  currentDir="stop";
}

void motorDrive(int s1,int s2,int s3,int s4,int spA,int spB){
  if(handBrake){ stopAll(); return; }

  digitalWrite(IN1,s1); digitalWrite(IN2,s2);
  digitalWrite(IN3,s3); digitalWrite(IN4,s4);

  analogWrite(ENA,spA);
  analogWrite(ENB,spB);
}

void executeMove(String dir,int spd){
  currentDir=dir;

  if(dir=="forward"){
    if(getDistance()>30) motorDrive(HIGH,LOW,HIGH,LOW,spd,spd);
    else stopAll();
  }
  else if(dir=="backward"){
    motorDrive(LOW,HIGH,LOW,HIGH,spd,spd);
  }
  else if(dir=="left"){
    motorDrive(LOW,HIGH,HIGH,LOW,spd,spd);
  }
  else if(dir=="right"){
    motorDrive(HIGH,LOW,LOW,HIGH,spd,spd);
  }
  else{
    stopAll();
  }
}

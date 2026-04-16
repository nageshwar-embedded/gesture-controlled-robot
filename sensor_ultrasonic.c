#include "sensor_ultrasonic.h"

#define TRIG_PIN 5
#define ECHO_PIN 18

void initUltrasonic(){
  pinMode(TRIG_PIN,OUTPUT);
  pinMode(ECHO_PIN,INPUT);
}

int getDistance(){
  digitalWrite(TRIG_PIN,LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN,HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN,LOW);

  long d = pulseIn(ECHO_PIN,HIGH,25000);
  return (d==0)?400:(int)(d*0.034/2);
}

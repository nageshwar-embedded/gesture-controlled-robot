#include "auto_mode.h"
#include "motor_control.h"
#include "sensor_ultrasonic.h"

bool autoMode = false;

void runAutoMode(){
  int d = getDistance();

  if(d > 30){
    executeMove("forward",180);
  } else {
    stopAll();
    delay(200);
    executeMove("backward",180);
    delay(300);
    stopAll();
  }
}

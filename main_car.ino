#include "motor_control.h"
#include "sensor_ultrasonic.h"
#include "espnow_comm.h"
#include "auto_mode.h"

void setup() {
  Serial.begin(115200);

  initMotors();
  initUltrasonic();
  initESPNow();

  Serial.println("System Ready");
}

void loop() {
  if (autoMode) {
    runAutoMode();
  }

  if (newGloveCmd) {
    executeMove(String(glovePacket.cmd), glovePacket.speed);
    newGloveCmd = false;
  }
}

#ifndef ESPNOW_COMM_H
#define ESPNOW_COMM_H

#include <esp_now.h>

typedef struct {
  char cmd[12];
  int speed;
} GlovePacket;

extern GlovePacket glovePacket;
extern bool newGloveCmd;

void initESPNow();

#endif

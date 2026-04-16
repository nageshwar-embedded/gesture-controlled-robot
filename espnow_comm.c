#include "espnow_comm.h"
#include <WiFi.h>

GlovePacket glovePacket;
bool newGloveCmd=false;

void onReceive(const esp_now_recv_info_t *info,const uint8_t *data,int len){
  if(len==sizeof(GlovePacket)){
    memcpy(&glovePacket,data,sizeof(GlovePacket));
    newGloveCmd=true;
  }
}

void initESPNow(){
  WiFi.mode(WIFI_STA);

  if(esp_now_init()!=ESP_OK){
    Serial.println("ESP-NOW init failed");
    return;
  }

  esp_now_register_recv_cb(onReceive);
}

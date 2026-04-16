#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h>
#include <esp_now.h>

// ── PINS ──
const int IN1 = 12; const int IN2 = 14;
const int IN3 = 27; const int IN4 = 26;
const int ENA = 13; const int ENB = 25;
const int trigPin = 5; const int echoPin = 18;
const int servoPin = 19;
const int batteryPin = 34;

#define MOTOR_PWM_HZ  1000
#define MOTOR_PWM_RES 8

String CAM_IP = "192.168.4.2";
bool camIPFound = false;

volatile bool autoMode  = false;
volatile bool handBrake = false;
volatile bool gloveMode = false;
String currentDir = "stop";
int maxLimit  = 180;
int servoPos  = 90;
int sweepDir  = 10;
int lastDistance = 0;
unsigned long lastGlovePacketTime = 0;
Servo myServo;
WebServer server(80);

typedef struct {
  char cmd[12];
  int  speed;
} GlovePacket;

GlovePacket glovePacket;
volatile bool newGloveCmd = false;

// ════════════════════════════════════════════════════════════
//  MOTOR CONTROL
// ════════════════════════════════════════════════════════════
void stopAll() {
  digitalWrite(IN1,LOW); digitalWrite(IN2,LOW);
  digitalWrite(IN3,LOW); digitalWrite(IN4,LOW);
  ledcWrite(ENA,0); ledcWrite(ENB,0);
  currentDir = "stop";
}

// ── EMERGENCY BRAKE — strongest possible stop ──
// Active braking: reverse pulse stops car instantly
// Normal stop se 3x faster
void emergencyBrake() {
  Serial.println("EMERGENCY BRAKE");

  // Step 1: Cut power
  ledcWrite(ENA,0); ledcWrite(ENB,0);
  delay(10);

  // Step 2: Reverse pulse based on direction
  int bp = 255; // max brake power

  if (currentDir=="forward"||currentDir=="fwdleft"||currentDir=="fwdright") {
    digitalWrite(IN1,LOW);  digitalWrite(IN2,HIGH);
    digitalWrite(IN3,LOW);  digitalWrite(IN4,HIGH);
    ledcWrite(ENA,bp); ledcWrite(ENB,bp);
    delay(100);
  } else if (currentDir=="backward"||currentDir=="bwdleft"||currentDir=="bwdright") {
    digitalWrite(IN1,HIGH); digitalWrite(IN2,LOW);
    digitalWrite(IN3,HIGH); digitalWrite(IN4,LOW);
    ledcWrite(ENA,bp); ledcWrite(ENB,bp);
    delay(100);
  } else if (currentDir=="left") {
    digitalWrite(IN1,LOW);  digitalWrite(IN2,HIGH);
    digitalWrite(IN3,HIGH); digitalWrite(IN4,LOW);
    ledcWrite(ENA,bp); ledcWrite(ENB,bp);
    delay(60);
  } else if (currentDir=="right") {
    digitalWrite(IN1,HIGH); digitalWrite(IN2,LOW);
    digitalWrite(IN3,LOW);  digitalWrite(IN4,HIGH);
    ledcWrite(ENA,bp); ledcWrite(ENB,bp);
    delay(60);
  }

  // Step 3: Full stop
  stopAll();

  // Step 4: Second pulse for guarantee
  delay(10);
  if (currentDir=="forward"||currentDir=="fwdleft"||currentDir=="fwdright"||
      currentDir=="backward"||currentDir=="bwdleft"||currentDir=="bwdright") {
    ledcWrite(ENA,200); ledcWrite(ENB,200);
    delay(60);
  }
  stopAll();
}

// ── SMOOTH STOP — joystick release pe gentle stop ──
// Active braking NAHI — sirf power cut
// Yahi misbehavior fix karta hai
void smoothStop() {
  if (currentDir == "stop") return;
  // Gradually reduce — no reverse pulse
  int spd = 150;
  while (spd > 0) {
    ledcWrite(ENA, spd);
    ledcWrite(ENB, spd);
    spd -= 50;
    delay(8);
  }
  stopAll();
}

void motorDrive(int s1,int s2,int s3,int s4,int spA,int spB) {
  if (handBrake) { stopAll(); return; }
  digitalWrite(IN1,s1); digitalWrite(IN2,s2);
  digitalWrite(IN3,s3); digitalWrite(IN4,s4);
  ledcWrite(ENA, constrain(spA,0,255));
  ledcWrite(ENB, constrain(spB,0,255));
}

// ════════════════════════════════════════════════════════════
//  EXECUTE MOVE — clean, no active brake on direction change
// ════════════════════════════════════════════════════════════
void executeMove(String dir, int spd) {
  if (handBrake) { stopAll(); return; }

  // ── Direction reversal: brief stop only ──
  bool wasForward  = (currentDir=="forward"||currentDir=="fwdleft"||currentDir=="fwdright");
  bool wasBackward = (currentDir=="backward"||currentDir=="bwdleft"||currentDir=="bwdright");
  bool goForward   = (dir=="forward"||dir=="fwdleft"||dir=="fwdright");
  bool goBackward  = (dir=="backward"||dir=="bwdleft"||dir=="bwdright");

  if ((wasForward && goBackward) || (wasBackward && goForward)) {
    // Clean stop before reversing — no active brake
    ledcWrite(ENA,0); ledcWrite(ENB,0);
    delay(40);
  }

  currentDir = dir;

  if (dir=="forward") {
    if (getDistance()>30) motorDrive(HIGH,LOW,HIGH,LOW,spd,spd);
    else { smoothStop(); currentDir="stop"; }
  }
  else if (dir=="fwdleft") {
    if (getDistance()>30) motorDrive(LOW,LOW,HIGH,LOW,0,spd);
    else { smoothStop(); currentDir="stop"; }
  }
  else if (dir=="fwdright") {
    if (getDistance()>30) motorDrive(HIGH,LOW,LOW,LOW,spd,0);
    else { smoothStop(); currentDir="stop"; }
  }
  else if (dir=="backward") {
    motorDrive(LOW,HIGH,LOW,HIGH,spd,spd);
  }
  else if (dir=="bwdleft") {
    motorDrive(LOW,LOW,LOW,HIGH,0,spd);
  }
  else if (dir=="bwdright") {
    motorDrive(LOW,HIGH,LOW,LOW,spd,0);
  }
  else if (dir=="left") {
    motorDrive(LOW,HIGH,HIGH,LOW,spd,spd);
  }
  else if (dir=="right") {
    motorDrive(HIGH,LOW,LOW,HIGH,spd,spd);
  }
  else {
    // Stop command — smooth stop
    smoothStop();
    currentDir = "stop";
  }
}

int getDistance() {
  digitalWrite(trigPin,LOW);  delayMicroseconds(2);
  digitalWrite(trigPin,HIGH); delayMicroseconds(10);
  digitalWrite(trigPin,LOW);
  long d = pulseIn(echoPin,HIGH,25000);
  lastDistance = (d==0) ? 400 : (int)(d*0.034/2);
  return lastDistance;
}

void runAutoMode() {
  if (!autoMode||handBrake) { stopAll(); return; }
  servoPos += sweepDir;
  if (servoPos<=60||servoPos>=120) sweepDir=-sweepDir;
  myServo.write(servoPos);
  int d = getDistance();
  if (d>30) {
    currentDir="forward";
    motorDrive(HIGH,LOW,HIGH,LOW,maxLimit,maxLimit);
  } else {
    emergencyBrake(); delay(200);
    currentDir="backward";
    motorDrive(LOW,HIGH,LOW,HIGH,maxLimit,maxLimit);
    delay(400); stopAll();
    myServo.write(30);  delay(500); int r=getDistance();
    myServo.write(150); delay(500); int l=getDistance();
    myServo.write(90);  delay(500); servoPos=90;
    if (l>r) { currentDir="left";  motorDrive(LOW,HIGH,HIGH,LOW,maxLimit,maxLimit); }
    else     { currentDir="right"; motorDrive(HIGH,LOW,LOW,HIGH,maxLimit,maxLimit); }
    delay(600); stopAll();
  }
}

// ════════════════════════════════════════════════════════════
//  ESP-NOW CALLBACK
// ════════════════════════════════════════════════════════════
void onGloveData(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (!gloveMode) return;
  if (len != sizeof(GlovePacket)) return;
  memcpy(&glovePacket, data, sizeof(GlovePacket));
  newGloveCmd = true;
  lastGlovePacketTime = millis();
}

// ════════════════════════════════════════════════════════════
//  CAM IP DETECT
// ════════════════════════════════════════════════════════════
void findCamIP() {
  String candidates[] = {"192.168.4.2","192.168.4.3","192.168.4.4","192.168.4.5"};
  for (String ip : candidates) {
    WiFiClient client; client.setTimeout(800);
    if (client.connect(ip.c_str(),80)) {
      CAM_IP=ip; camIPFound=true; client.stop();
      Serial.println("CAM: "+CAM_IP); return;
    }
    client.stop(); delay(100);
  }
}

// ════════════════════════════════════════════════════════════
//  HTML
// ════════════════════════════════════════════════════════════
String getHTML() {
  String h = "";
  h += "<!DOCTYPE html><html lang='en'><head>";
  h += "<meta name='viewport' content='width=device-width,initial-scale=1.0,maximum-scale=1.0,user-scalable=no,viewport-fit=cover'>";
  h += "<meta charset='UTF-8'>";
  h += "<meta name='apple-mobile-web-app-capable' content='yes'>";
  h += "<meta name='apple-mobile-web-app-status-bar-style' content='black-translucent'>";
  h += "<title>NS-CAR</title>";
  h += "<link href='https://fonts.googleapis.com/css2?family=Rajdhani:wght@500;600;700&family=Share+Tech+Mono&display=swap' rel='stylesheet'>";

  h += "<style>";
  h += ":root{--g:#00FF41;--gd:rgba(0,255,65,0.45);--gg:rgba(0,255,65,0.25);--a:#FFB300;--r:#FF3B30;--p:#9C27B0;--bg:#020804;--pn:#060f07;--bd:rgba(0,255,65,0.2);--mo:'Share Tech Mono',monospace;--sa:'Rajdhani',sans-serif;}";
  h += "*,*::before,*::after{margin:0;padding:0;box-sizing:border-box;-webkit-tap-highlight-color:transparent;user-select:none;-webkit-user-select:none;}";
  h += "html,body{width:100%;height:100%;overflow:hidden;touch-action:none;background:var(--bg);color:var(--g);font-family:var(--sa);}";
  h += "body::after{content:'';position:fixed;inset:0;background:repeating-linear-gradient(0deg,transparent,transparent 2px,rgba(0,0,0,0.07) 2px,rgba(0,0,0,0.07) 4px);pointer-events:none;z-index:1;}";
  h += "#app{position:fixed;inset:0;padding:env(safe-area-inset-top) env(safe-area-inset-right) env(safe-area-inset-bottom) env(safe-area-inset-left);display:grid;grid-template-rows:auto 1fr auto auto auto;height:100%;overflow:hidden;}";
  h += ".hd{display:flex;align-items:center;justify-content:space-between;padding:5px 12px;background:var(--pn);border-bottom:1px solid var(--bd);position:relative;}";
  h += ".hd::after{content:'';position:absolute;bottom:-1px;left:8%;right:8%;height:1px;background:var(--g);box-shadow:0 0 6px var(--g);}";
  h += ".logo{font-size:clamp(14px,4vw,18px);font-weight:700;letter-spacing:3px;text-shadow:0 0 10px var(--g);}";
  h += ".logo em{color:var(--a);font-style:normal;}";
  h += ".hr{display:flex;align-items:center;gap:10px;}";
  h += ".sig{display:flex;align-items:flex-end;gap:2px;height:13px;}";
  h += ".sig div{width:3px;background:var(--g);border-radius:1px;box-shadow:0 0 3px var(--g);}";
  h += ".sig div:nth-child(1){height:4px}.sig div:nth-child(2){height:7px}.sig div:nth-child(3){height:10px}.sig div:nth-child(4){height:13px;opacity:.3}";
  h += ".bt{font-family:var(--mo);font-size:clamp(9px,2.3vw,12px);letter-spacing:1px;}";
  h += ".mp{font-size:clamp(8px,2vw,11px);font-weight:700;letter-spacing:2px;padding:2px 7px;border:1px solid var(--g);border-radius:2px;background:rgba(0,255,65,0.08);animation:gP 2s ease-in-out infinite;}";
  h += ".mp.auto{color:var(--a);border-color:var(--a);background:rgba(255,179,0,0.08);}";
  h += ".mp.brake{color:var(--r);border-color:var(--r);background:rgba(255,59,48,0.08);animation:none;}";
  h += ".mp.glove{color:#CE93D8;border-color:var(--p);background:rgba(156,39,176,0.1);}";
  h += "@keyframes gP{0%,100%{box-shadow:0 0 3px var(--gg);}50%{box-shadow:0 0 10px var(--gg);}}";
  h += ".cw{position:relative;z-index:2;margin:5px 8px 3px;border:1px solid var(--bd);border-radius:4px;overflow:hidden;background:#000;min-height:0;}";
  h += ".cw::before,.cw::after{content:'';position:absolute;width:16px;height:16px;z-index:10;pointer-events:none;}";
  h += ".cw::before{top:5px;left:5px;border-top:2px solid var(--g);border-left:2px solid var(--g);box-shadow:0 0 5px var(--gg);}";
  h += ".cw::after{bottom:5px;right:5px;border-bottom:2px solid var(--g);border-right:2px solid var(--g);box-shadow:0 0 5px var(--gg);}";
  h += ".ctr,.cbl{position:absolute;width:16px;height:16px;z-index:10;pointer-events:none;}";
  h += ".ctr{top:5px;right:5px;border-top:2px solid var(--g);border-right:2px solid var(--g);}";
  h += ".cbl{bottom:5px;left:5px;border-bottom:2px solid var(--g);border-left:2px solid var(--g);}";
  h += ".xh{position:absolute;top:50%;left:50%;transform:translate(-50%,-50%);width:28px;height:28px;z-index:10;pointer-events:none;opacity:.45;}";
  h += ".xh::before{content:'';position:absolute;top:50%;left:0;right:0;height:1px;background:var(--g);box-shadow:0 0 3px var(--g);}";
  h += ".xh::after{content:'';position:absolute;left:50%;top:0;bottom:0;width:1px;background:var(--g);box-shadow:0 0 3px var(--g);}";
  h += ".xr{position:absolute;top:50%;left:50%;transform:translate(-50%,-50%);width:12px;height:12px;border:1px solid var(--g);border-radius:50%;z-index:10;pointer-events:none;opacity:.45;}";
  h += ".cs{position:absolute;inset:0;z-index:5;pointer-events:none;background:linear-gradient(to bottom,transparent 0%,rgba(0,255,65,0.04) 50%,transparent 100%);background-size:100% 8px;animation:sc 3s linear infinite;}";
  h += "@keyframes sc{0%{background-position:0 -100%}100%{background-position:0 200%}}";
  h += ".rec{position:absolute;top:7px;left:50%;transform:translateX(-50%);display:none;align-items:center;gap:4px;z-index:11;font-family:var(--mo);font-size:9px;letter-spacing:2px;}";
  h += ".rec.on{display:flex;}";
  h += ".rd{width:6px;height:6px;background:var(--r);border-radius:50%;box-shadow:0 0 6px var(--r);animation:bl 1s step-end infinite;}";
  h += "@keyframes bl{0%,100%{opacity:1}50%{opacity:0}}";
  h += "#cf{position:absolute;inset:0;width:100%;height:100%;object-fit:cover;display:none;}";
  h += ".co{position:absolute;inset:0;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:5px;background:#000;}";
  h += ".ci{font-size:clamp(22px,5.5vw,32px);opacity:.18;}";
  h += ".cl{font-family:var(--mo);font-size:clamp(8px,2vw,11px);letter-spacing:3px;color:var(--gd);}";
  h += ".cs2{font-family:var(--mo);font-size:clamp(7px,1.7vw,9px);color:var(--gd);opacity:0;animation:fp 4s ease-in-out infinite;}";
  h += "@keyframes fp{0%,100%{opacity:0}30%,70%{opacity:.5}}";
  h += ".cb{position:absolute;bottom:0;left:0;right:0;padding:2px 8px;background:rgba(0,0,0,0.65);display:flex;justify-content:space-between;z-index:10;pointer-events:none;}";
  h += ".cb span{font-family:var(--mo);font-size:clamp(6px,1.6vw,9px);letter-spacing:1px;color:var(--gd);}";
  h += ".st{display:grid;grid-template-columns:repeat(3,1fr);gap:4px;margin:0 8px 3px;}";
  h += ".sb{background:var(--pn);border:1px solid var(--bd);border-radius:3px;padding:3px 2px;text-align:center;position:relative;overflow:hidden;}";
  h += ".sb::before{content:'';position:absolute;top:0;left:10%;right:10%;height:1px;background:linear-gradient(90deg,transparent,var(--g),transparent);opacity:.4;}";
  h += ".sl{font-size:clamp(6px,1.6vw,8px);letter-spacing:2px;color:var(--gd);text-transform:uppercase;}";
  h += ".sv{font-family:var(--mo);font-size:clamp(13px,3.8vw,19px);color:var(--g);text-shadow:0 0 5px var(--g);line-height:1.1;}";
  h += ".su{font-size:clamp(6px,1.5vw,8px);color:var(--gd);letter-spacing:1px;}";
  h += ".sv.warn{color:var(--a);text-shadow:0 0 5px var(--a);}";
  h += ".sv.danger{color:var(--r);text-shadow:0 0 5px var(--r);}";
  h += ".js{margin:0 8px 3px;background:var(--pn);border:1px solid var(--bd);border-radius:4px;padding:3px 10px 5px;display:flex;flex-direction:column;align-items:center;position:relative;}";
  h += ".gswitch{position:absolute;top:4px;right:6px;display:flex;align-items:center;gap:4px;z-index:20;}";
  h += ".gswitch-lbl{font-family:var(--mo);font-size:clamp(6px,1.5vw,8px);color:var(--gd);letter-spacing:1px;}";
  h += ".toggle{width:32px;height:17px;background:#1a2a1c;border:1px solid var(--bd);border-radius:9px;position:relative;cursor:pointer;transition:all 0.2s;}";
  h += ".toggle.on{background:rgba(156,39,176,0.5);border-color:var(--p);}";
  h += ".toggle::after{content:'';position:absolute;top:2px;left:2px;width:11px;height:11px;background:var(--gd);border-radius:50%;transition:all 0.2s;}";
  h += ".toggle.on::after{left:17px;background:#CE93D8;box-shadow:0 0 5px rgba(156,39,176,0.8);}";
  h += ".gcmd{display:none;font-family:var(--mo);font-size:clamp(10px,2.8vw,13px);color:#CE93D8;letter-spacing:3px;margin-bottom:2px;}";
  h += ".gcmd.on{display:block;}";
  h += ".jl{font-size:clamp(7px,1.7vw,9px);letter-spacing:3px;color:var(--gd);margin-bottom:1px;}";
  h += ".ja{position:relative;display:flex;align-items:center;justify-content:center;width:100%;}";
  h += ".jd{font-family:var(--mo);font-size:clamp(8px,1.9vw,10px);color:var(--gd);letter-spacing:1px;position:absolute;}";
  h += ".jd.u{top:0;left:50%;transform:translateX(-50%);}";
  h += ".jd.d{bottom:0;left:50%;transform:translateX(-50%);}";
  h += ".jd.l{left:2px;top:50%;transform:translateY(-50%);}";
  h += ".jd.r{right:2px;top:50%;transform:translateY(-50%);}";
  h += "#jc{width:clamp(110px,33vw,170px);height:clamp(110px,33vw,170px);border-radius:50%;background:radial-gradient(circle,#0a1a0c 0%,#020804 70%);border:2px solid var(--bd);box-shadow:0 0 14px rgba(0,255,65,0.07),inset 0 0 18px rgba(0,0,0,0.5);position:relative;touch-action:none;}";
  h += "#jc.gloveActive{border-color:rgba(156,39,176,0.4);opacity:0.35;pointer-events:none;}";
  h += "#jc::before{content:'';position:absolute;top:50%;left:10%;right:10%;height:1px;background:var(--bd);}";
  h += "#jc::after{content:'';position:absolute;left:50%;top:10%;bottom:10%;width:1px;background:var(--bd);}";
  h += ".jr{position:absolute;top:50%;left:50%;transform:translate(-50%,-50%);border-radius:50%;border:1px solid var(--bd);pointer-events:none;}";
  h += ".jr:nth-child(1){width:66%;height:66%;}.jr:nth-child(2){width:33%;height:33%;}";
  h += "#jk{width:clamp(40px,12vw,58px);height:clamp(40px,12vw,58px);background:radial-gradient(circle at 35% 35%,#1a4a1e,#0a1a0c);border-radius:50%;position:absolute;top:50%;left:50%;transform:translate(-50%,-50%);border:2px solid var(--g);box-shadow:0 0 10px var(--gg);touch-action:none;}";
  h += "#jk.on{box-shadow:0 0 20px var(--g),0 0 40px var(--gg);}";
  h += "#jk::after{content:'';position:absolute;top:50%;left:50%;transform:translate(-50%,-50%);width:6px;height:6px;border-radius:50%;background:var(--g);box-shadow:0 0 5px var(--g);}";
  h += ".tr{display:flex;align-items:center;gap:8px;width:100%;margin-top:3px;}";
  h += ".tl{font-size:clamp(7px,1.7vw,9px);letter-spacing:2px;color:var(--gd);white-space:nowrap;min-width:42px;}";
  h += ".tv{font-family:var(--mo);font-size:clamp(11px,3.2vw,14px);color:var(--g);text-shadow:0 0 4px var(--g);min-width:26px;text-align:right;}";
  h += "input[type=range]{flex:1;-webkit-appearance:none;height:3px;border-radius:2px;outline:none;background:linear-gradient(90deg,var(--g) var(--p,71%),#1a2a1c var(--p,71%));}";
  h += "input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:clamp(16px,4.5vw,20px);height:clamp(16px,4.5vw,20px);border-radius:50%;background:var(--g);border:2px solid var(--bg);box-shadow:0 0 7px var(--g);cursor:pointer;}";
  h += ".ctl{margin:0 8px 5px;display:grid;grid-template-columns:1fr 1fr 1fr;grid-template-rows:auto auto auto;gap:4px;}";
  h += ".cb2{padding:clamp(5px,1.8vw,9px) 2px;border:1px solid var(--bd);border-radius:4px;background:var(--pn);color:var(--g);font-family:var(--sa);font-size:clamp(7px,2vw,10px);font-weight:700;letter-spacing:1px;text-transform:uppercase;cursor:pointer;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:1px;-webkit-appearance:none;transition:all 0.12s;}";
  h += ".cb2 .ic{font-size:clamp(11px,3vw,16px);line-height:1;}";
  h += ".cb2:active{background:rgba(0,255,65,0.08);}";
  h += ".cb2.gon{border-color:var(--g);color:var(--g);background:rgba(0,255,65,0.1);box-shadow:0 0 8px var(--gg);}";
  h += ".cb2.aon{border-color:var(--a);color:var(--a);background:rgba(255,179,0,0.08);}";
  h += ".cb2.son{border-color:#4CAF50;color:#4CAF50;background:rgba(76,175,80,0.1);}";
  h += ".cb2.qon{border-color:var(--p);color:#CE93D8;background:rgba(156,39,176,0.1);box-shadow:0 0 8px rgba(156,39,176,0.3);}";
  h += ".brk{grid-column:1/-1;padding:clamp(7px,2.2vw,11px) 4px;border:2px solid rgba(255,59,48,0.35);border-radius:4px;background:rgba(255,59,48,0.04);color:var(--r);font-family:var(--sa);font-size:clamp(10px,3vw,14px);font-weight:700;letter-spacing:4px;text-transform:uppercase;cursor:pointer;-webkit-appearance:none;transition:all 0.15s;}";
  h += ".brk.active{background:rgba(255,59,48,0.22);box-shadow:0 0 22px rgba(255,59,48,0.55);border-color:var(--r);}";
  h += ".brk:active{background:rgba(255,59,48,0.18);}";
  h += "#toast{position:fixed;top:55px;left:50%;transform:translateX(-50%);background:rgba(255,59,48,0.92);color:#fff;padding:4px 14px;border-radius:3px;font-family:var(--sa);font-size:clamp(9px,2.5vw,12px);font-weight:700;letter-spacing:2px;display:none;z-index:9999;}";
  h += "#snap_toast{position:fixed;top:55px;left:50%;transform:translateX(-50%);background:rgba(76,175,80,0.92);color:#fff;padding:4px 14px;border-radius:3px;font-family:var(--sa);font-size:clamp(9px,2.5vw,12px);font-weight:700;letter-spacing:2px;display:none;z-index:9999;}";
  h += "#iptoast{position:fixed;bottom:70px;left:50%;transform:translateX(-50%);background:rgba(0,255,65,0.15);border:1px solid var(--g);color:var(--g);padding:4px 14px;border-radius:3px;font-family:var(--mo);font-size:clamp(8px,2vw,11px);letter-spacing:1px;display:none;z-index:9999;}";
  h += "#boot{position:fixed;inset:0;background:var(--bg);z-index:99999;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:5px;font-family:var(--mo);}";
  h += ".bn{font-size:clamp(20px,6vw,28px);letter-spacing:6px;color:var(--g);text-shadow:0 0 20px var(--g);margin-bottom:14px;}";
  h += ".bl{font-size:clamp(9px,2.3vw,11px);letter-spacing:1px;color:var(--gd);opacity:0;animation:bf 0.3s forwards;}";
  h += "@keyframes bf{to{opacity:1}}";
  h += ".bb{width:clamp(140px,38vw,200px);height:3px;background:#0a1a0c;border-radius:2px;margin-top:14px;overflow:hidden;}";
  h += ".bbr{height:100%;width:0%;background:var(--g);box-shadow:0 0 8px var(--g);animation:bar 1.6s ease forwards 0.4s;}";
  h += "@keyframes bar{to{width:100%}}";
  h += "</style></head><body>";

  h += "<div id='boot'>";
  h += "<div class='bn'>NS-CAR</div>";
  h += "<div class='bl' style='animation-delay:.05s'>&#9658; SYSTEM INIT...</div>";
  h += "<div class='bl' style='animation-delay:.30s'>&#9658; MOTOR DRIVER OK</div>";
  h += "<div class='bl' style='animation-delay:.55s'>&#9658; SENSOR ARRAY OK</div>";
  h += "<div class='bl' style='animation-delay:.80s'>&#9658; ESP-NOW READY</div>";
  h += "<div class='bl' style='animation-delay:1.05s'>&#9658; WIFI ESTABLISHED</div>";
  h += "<div class='bb'><div class='bbr'></div></div>";
  h += "</div>";

  h += "<div id='toast'></div><div id='snap_toast'></div><div id='iptoast'></div>";
  h += "<div id='app'>";
  h += "<div class='hd'>";
  h += "<div class='logo'>NS<em>-</em>CAR</div>";
  h += "<div class='hr'>";
  h += "<div class='sig'><div></div><div></div><div></div><div></div></div>";
  h += "<div class='bt' id='bTxt'>BAT:--%</div>";
  h += "<div class='mp' id='mPill'>STBY</div>";
  h += "</div></div>";

  h += "<div class='cw'>";
  h += "<div class='ctr'></div><div class='cbl'></div>";
  h += "<div class='xh'></div><div class='xr'></div>";
  h += "<div class='cs'></div>";
  h += "<div class='rec' id='rec'><div class='rd'></div><span>LIVE</span></div>";
  h += "<img id='cf' src='' alt=''>";
  h += "<div class='co' id='co'>";
  h += "<div class='ci'>[ ]</div>";
  h += "<div class='cl'>CAMERA OFFLINE</div>";
  h += "<div class='cs2'>AWAITING SIGNAL...</div>";
  h += "</div>";
  h += "<div class='cb'>";
  h += "<span>CAM-01 // <span id='camIPLabel'>" + CAM_IP + "</span></span>";
  h += "<span id='cRes'>-- x --</span>";
  h += "<span id='cTime'>00:00</span>";
  h += "</div></div>";

  h += "<div class='st'>";
  h += "<div class='sb'><div class='sl'>SPEED</div><div class='sv' id='sDsp'>180</div><div class='su'>PWM</div></div>";
  h += "<div class='sb'><div class='sl'>DIST</div><div class='sv' id='dDsp'>---</div><div class='su'>CM</div></div>";
  h += "<div class='sb'><div class='sl'>MODE</div><div class='sv' id='mDsp' style='font-size:clamp(9px,2.5vw,13px);letter-spacing:0'>IDLE</div><div class='su'>&nbsp;</div></div>";
  h += "</div>";

  h += "<div class='js' id='jsPanel'>";
  h += "<div class='gswitch'>";
  h += "<span class='gswitch-lbl'>&#x1F9E4; GLOVE</span>";
  h += "<div class='toggle' id='gToggle' onclick='toggleGlove()'></div>";
  h += "</div>";
  h += "<div class='gcmd' id='gcmd'>WAITING...</div>";
  h += "<div class='jl' id='jLabel'>DRIVE CONTROL</div>";
  h += "<div class='ja'>";
  h += "<span class='jd u'>FWD</span><span class='jd d'>REV</span><span class='jd l'>L</span><span class='jd r'>R</span>";
  h += "<div id='jc'><div class='jr'></div><div class='jr'></div><div id='jk'></div></div>";
  h += "</div>";
  h += "<div class='tr'>";
  h += "<span class='tl'>THROTTLE</span>";
  h += "<input type='range' min='0' max='255' value='180' id='sld' oninput='onSpd(this.value)'>";
  h += "<span class='tv' id='sVal'>180</span>";
  h += "</div></div>";

  h += "<div class='ctl'>";
  h += "<button class='cb2' id='camBtn' onclick='toggleCam()'><span class='ic'>[ ]</span><span>CAM OFF</span></button>";
  h += "<button class='cb2' id='fBtn' onclick='toggleFlash()'><span class='ic'>*</span><span>FLASH OFF</span></button>";
  h += "<button class='cb2' id='snapBtn' onclick='takeSnapshot()'><span class='ic'>&#128247;</span><span>SNAP</span></button>";
  h += "<button class='cb2' id='aBtn' onclick='toggleAuto()'><span class='ic'>@</span><span>AUTO OFF</span></button>";
  h += "<button class='cb2' id='mBtn' onclick='setManual()'><span class='ic'>+</span><span>MANUAL</span></button>";
  h += "<button class='cb2' id='qBtn' onclick='toggleQuality()'><span class='ic'>&#9889;</span><span>FAST</span></button>";
  h += "<button class='brk' id='brkBtn' onclick='doBrake()'>-- EMERGENCY BRAKE --</button>";
  h += "</div>";
  h += "</div>";

  h += "<script>";
  h += "var CAM_IP='" + CAM_IP + "';";
  h += "var camOn=false,flashOn=false,autoOn=false,braked=false,gloveOn=false;";
  h += "var lastCmd='',maxSpd=180,ctmr=null,csec=0;";
  h += "var camBusy=false,snapBusy=false,qualBusy=false;";
  h += "var qualMode=0;";

  h += "setTimeout(function(){var b=document.getElementById('boot');b.style.transition='opacity 0.5s';b.style.opacity='0';setTimeout(function(){b.style.display='none';},500);},2200);";

  h += "function detectCamIP(){var c=['192.168.4.2','192.168.4.3','192.168.4.4','192.168.4.5'];var i=0;function t(){if(i>=c.length)return;var ip=c[i++];var ct=new AbortController();var tid=setTimeout(function(){ct.abort();},800);fetch('http://'+ip+'/status',{signal:ct.signal,cache:'no-cache'}).then(function(r){clearTimeout(tid);return r.json();}).then(function(d){if(d.heap){CAM_IP=ip;document.getElementById('camIPLabel').textContent=ip;var el=document.getElementById('iptoast');el.textContent='CAM FOUND: '+ip;el.style.display='block';setTimeout(function(){el.style.display='none';},3000);}else{t();}}).catch(function(){clearTimeout(tid);t();});}t();}";
  h += "setTimeout(detectCamIP,2500);";

  h += "function onSpd(v){maxSpd=parseInt(v);document.getElementById('sVal').textContent=v;document.getElementById('sDsp').textContent=v;document.getElementById('sld').style.setProperty('--p',Math.round((v/255)*100)+'%');fetch('/limit?val='+v).catch(function(){});}";
  h += "document.getElementById('sld').style.setProperty('--p','71%');";

  h += "setInterval(function(){fetch('/getdist').then(function(r){return r.text();}).then(function(d){var dist=parseInt(d);var el=document.getElementById('dDsp');if(isNaN(dist)||dist>=400){el.textContent='400+';el.className='sv';}else{el.textContent=dist;if(dist>60)el.className='sv';else if(dist>30)el.className='sv warn';else el.className='sv danger';}}).catch(function(){document.getElementById('dDsp').textContent='ERR';});},500);";

  h += "setInterval(function(){if(!gloveOn)return;fetch('/glovecmd').then(function(r){return r.text();}).then(function(d){document.getElementById('gcmd').textContent=d.toUpperCase();}).catch(function(){});},200);";

  h += "var joy=document.getElementById('jk');var cont=document.getElementById('jc');";
  h += "cont.addEventListener('touchstart',onMv,{passive:false});";
  h += "cont.addEventListener('touchmove',onMv,{passive:false});";
  h += "cont.addEventListener('touchend',onEd,{passive:false});";
  h += "cont.addEventListener('touchcancel',onEd,{passive:false});";

  h += "function onMv(e){if(gloveOn)return;e.preventDefault();var t=e.touches[0],r=cont.getBoundingClientRect();var x=t.clientX-r.left-r.width/2;var y=t.clientY-r.top-r.height/2;var d=Math.sqrt(x*x+y*y);var mxR=r.width/2-joy.offsetWidth/2-2;if(d>mxR){x*=mxR/d;y*=mxR/d;d=mxR;}joy.style.transform='translate(calc(-50% + '+x+'px),calc(-50% + '+y+'px))';joy.classList.add('on');var cmd='stop';if(y<-22&&x<-22)cmd='fwdleft';else if(y<-22&&x>22)cmd='fwdright';else if(y>22&&x<-22)cmd='bwdleft';else if(y>22&&x>22)cmd='bwdright';else if(y<-22)cmd='forward';else if(y>22)cmd='backward';else if(x<-22)cmd='left';else if(x>22)cmd='right';if(cmd!==lastCmd){if(braked){braked=false;fetch('/unbrake').catch(function(){});document.getElementById('brkBtn').classList.remove('active');}fetch('/move?dir='+cmd+'&spd='+maxSpd).catch(function(){});lastCmd=cmd;setMd('MANUAL');}}";

  h += "function onEd(e){if(gloveOn)return;e.preventDefault();joy.style.transform='translate(-50%,-50%)';joy.classList.remove('on');lastCmd='stop';fetch('/smoothstop').catch(function(){});if(!autoOn&&!braked)setMd('IDLE');}";

  h += "function setMd(m){document.getElementById('mDsp').textContent=m;var p=document.getElementById('mPill');p.textContent=m;p.className='mp';if(m==='AUTO ON')p.classList.add('auto');if(m==='BRAKE')p.classList.add('brake');if(m==='GLOVE')p.classList.add('glove');}";

  h += "function doBrake(){braked=true;autoOn=false;gloveOn=false;fetch('/glove?on=0').catch(function(){});updateGloveUI(false);setMd('BRAKE');document.getElementById('aBtn').className='cb2';document.getElementById('aBtn').innerHTML='<span class=\\'ic\\'>@</span><span>AUTO OFF</span>';document.getElementById('brkBtn').classList.add('active');for(var i=0;i<8;i++){(function(dl){setTimeout(function(){fetch('/brake').catch(function(){});},dl);})(i*40);}}";

  h += "function toggleAuto(){if(autoOn){fetch('/manual').catch(function(){});autoOn=false;document.getElementById('aBtn').className='cb2';document.getElementById('aBtn').innerHTML='<span class=\\'ic\\'>@</span><span>AUTO OFF</span>';setMd('IDLE');}else{braked=false;gloveOn=false;updateGloveUI(false);fetch('/unbrake').catch(function(){});document.getElementById('brkBtn').classList.remove('active');fetch('/auto').catch(function(){});autoOn=true;document.getElementById('aBtn').className='cb2 aon';document.getElementById('aBtn').innerHTML='<span class=\\'ic\\'>@</span><span>AUTO ON</span>';setMd('AUTO ON');}}";

  h += "function setManual(){braked=false;fetch('/unbrake').catch(function(){});document.getElementById('brkBtn').classList.remove('active');fetch('/manual').catch(function(){});autoOn=false;document.getElementById('aBtn').className='cb2';document.getElementById('aBtn').innerHTML='<span class=\\'ic\\'>@</span><span>AUTO OFF</span>';var mb=document.getElementById('mBtn');mb.className='cb2 gon';setTimeout(function(){mb.className='cb2';},350);setMd('MANUAL');}";

  h += "function updateGloveUI(on){var tog=document.getElementById('gToggle');var jc=document.getElementById('jc');var gcmd=document.getElementById('gcmd');var jlbl=document.getElementById('jLabel');if(on){tog.classList.add('on');jc.classList.add('gloveActive');gcmd.classList.add('on');jlbl.textContent='GLOVE ACTIVE';jlbl.style.color='#CE93D8';setMd('GLOVE');}else{tog.classList.remove('on');jc.classList.remove('gloveActive');gcmd.classList.remove('on');jlbl.textContent='DRIVE CONTROL';jlbl.style.color='';}}";

  h += "function toggleGlove(){if(gloveOn){gloveOn=false;fetch('/glove?on=0').catch(function(){});fetch('/smoothstop').catch(function(){});updateGloveUI(false);if(!autoOn&&!braked)setMd('IDLE');}else{braked=false;autoOn=false;document.getElementById('aBtn').className='cb2';document.getElementById('aBtn').innerHTML='<span class=\\'ic\\'>@</span><span>AUTO OFF</span>';document.getElementById('brkBtn').classList.remove('active');fetch('/unbrake').catch(function(){});fetch('/glove?on=1').catch(function(){});gloveOn=true;updateGloveUI(true);}}";

  h += "function camFetch(url,tries,cb){tries=tries||3;fetch(url,{method:'GET',mode:'cors',cache:'no-cache'}).then(function(r){if(!r.ok)throw new Error('HTTP '+r.status);return r.json();}).then(function(d){cb(null,d);}).catch(function(e){if(tries>1){setTimeout(function(){camFetch(url,tries-1,cb);},800);}else{cb(e,null);}});}";

  h += "function toggleCam(){if(camBusy)return;camBusy=true;var btn=document.getElementById('camBtn');btn.style.opacity='0.5';camFetch('http://'+CAM_IP+'/camtoggle',3,function(err,d){btn.style.opacity='1';camBusy=false;if(err||!d){toast('CAM NOT REACHABLE');detectCamIP();return;}camOn=(d.camera==='on');var feed=document.getElementById('cf');var off=document.getElementById('co');var rec=document.getElementById('rec');if(camOn){feed.src='http://'+CAM_IP+':81/stream';feed.style.display='block';off.style.display='none';btn.className='cb2 gon';btn.innerHTML='<span class=\\'ic\\'>[O]</span><span>CAM ON</span>';rec.classList.add('on');document.getElementById('cRes').textContent=qualMode===0?'320x240':'640x480';startT();}else{feed.src='';feed.style.display='none';off.style.display='flex';btn.className='cb2';btn.innerHTML='<span class=\\'ic\\'>[ ]</span><span>CAM OFF</span>';rec.classList.remove('on');document.getElementById('cRes').textContent='-- x --';stopT();flashOn=false;var fb=document.getElementById('fBtn');fb.className='cb2';fb.innerHTML='<span class=\\'ic\\'>*</span><span>FLASH OFF</span>';}});}";

  h += "function toggleFlash(){if(!camOn){toast('ENABLE CAMERA FIRST');return;}camFetch('http://'+CAM_IP+'/flash',3,function(err,d){if(err||!d)return;if(d.error){toast(d.error.toUpperCase());return;}flashOn=(d.flash==='on');var btn=document.getElementById('fBtn');btn.className=flashOn?'cb2 aon':'cb2';btn.innerHTML=flashOn?'<span class=\\'ic\\'>*</span><span>FLASH ON</span>':'<span class=\\'ic\\'>*</span><span>FLASH OFF</span>';});}";

  h += "function toggleQuality(){if(!camOn){toast('ENABLE CAMERA FIRST');return;}if(qualBusy)return;qualBusy=true;qualMode=qualMode===0?1:0;var btn=document.getElementById('qBtn');btn.style.opacity='0.5';btn.innerHTML='<span class=\\'ic\\'>&#8987;</span><span>WAIT...</span>';var feed=document.getElementById('cf');feed.src='';setTimeout(function(){fetch('http://'+CAM_IP+'/setmode?mode='+qualMode,{cache:'no-cache'}).then(function(r){return r.json();}).then(function(d){btn.style.opacity='1';qualBusy=false;if(qualMode===0){btn.className='cb2';btn.innerHTML='<span class=\\'ic\\'>&#9889;</span><span>FAST</span>';document.getElementById('cRes').textContent='320x240';}else{btn.className='cb2 qon';btn.innerHTML='<span class=\\'ic\\'>&#128247;</span><span>QUALITY</span>';document.getElementById('cRes').textContent='640x480';}setTimeout(function(){feed.src='http://'+CAM_IP+':81/stream?t='+Date.now();},1200);}).catch(function(){btn.style.opacity='1';qualBusy=false;qualMode=qualMode===0?1:0;btn.className=qualMode===0?'cb2':'cb2 qon';btn.innerHTML=qualMode===0?'<span class=\\'ic\\'>&#9889;</span><span>FAST</span>':'<span class=\\'ic\\'>&#128247;</span><span>QUALITY</span>';toast('QUALITY SWITCH FAILED');});},400);}";

  h += "function takeSnapshot(){if(!camOn){toast('ENABLE CAMERA FIRST');return;}if(snapBusy)return;snapBusy=true;var btn=document.getElementById('snapBtn');btn.className='cb2 son';btn.innerHTML='<span class=\\'ic\\'>&#128247;</span><span>WAIT...</span>';var feed=document.getElementById('cf');feed.src='';setTimeout(function(){fetch('http://'+CAM_IP+'/snapshot',{cache:'no-cache'}).then(function(r){if(!r.ok)throw new Error('failed');return r.blob();}).then(function(blob){var url=URL.createObjectURL(blob);var a=document.createElement('a');var ts=new Date().toISOString().replace(/[:.]/g,'-').slice(0,19);a.href=url;a.download='ns_car_'+ts+'.jpg';a.click();snapToast('SNAPSHOT SAVED');}).catch(function(){toast('SNAPSHOT FAILED');}).finally(function(){snapBusy=false;btn.className='cb2';btn.innerHTML='<span class=\\'ic\\'>&#128247;</span><span>SNAP</span>';setTimeout(function(){feed.src='http://'+CAM_IP+':81/stream?t='+Date.now();},600);});},400);}";

  h += "function startT(){csec=0;ctmr=setInterval(function(){csec++;var m=String(Math.floor(csec/60)).padStart(2,'0');var s=String(csec%60).padStart(2,'0');document.getElementById('cTime').textContent=m+':'+s;},1000);}";
  h += "function stopT(){clearInterval(ctmr);document.getElementById('cTime').textContent='00:00';csec=0;}";
  h += "setInterval(function(){fetch('/getbatt').then(function(r){return r.text();}).then(function(d){document.getElementById('bTxt').textContent='BAT:'+parseInt(d)+'%';}).catch(function(){});},4000);";
  h += "function toast(msg){var el=document.getElementById('toast');el.textContent='! '+msg;el.style.display='block';setTimeout(function(){el.style.display='none';},2500);}";
  h += "function snapToast(msg){var el=document.getElementById('snap_toast');el.textContent=msg;el.style.display='block';setTimeout(function(){el.style.display='none';},2000);}";

  h += "</script></body></html>";
  return h;
}

// ════════════════════════════════════════════════════════════
//  SETUP
// ════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);

  pinMode(IN1,OUTPUT); pinMode(IN2,OUTPUT);
  pinMode(IN3,OUTPUT); pinMode(IN4,OUTPUT);
  pinMode(trigPin,OUTPUT); pinMode(echoPin,INPUT);

  ledcAttach(ENA,MOTOR_PWM_HZ,MOTOR_PWM_RES);
  ledcAttach(ENB,MOTOR_PWM_HZ,MOTOR_PWM_RES);
  stopAll();

  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  myServo.setPeriodHertz(50);
  myServo.attach(servoPin,500,2400);
  myServo.write(90);

  WiFi.mode(WIFI_AP);
  WiFi.softAP("Nageshwar_Singh_Car","12345678");
  WiFi.setSleep(false);

  Serial.println("\n==========================================");
  Serial.println("CAR MAC (paste into glove code):");
  Serial.println(WiFi.softAPmacAddress());
  Serial.println("==========================================\n");

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW FAILED");
  } else {
    esp_now_register_recv_cb(onGloveData);
    Serial.println("ESP-NOW ready");
  }

  delay(3000);
  findCamIP();

  server.on("/", []() { server.send(200,"text/html",getHTML()); });
  server.on("/camip", []() { findCamIP(); server.send(200,"text/plain",CAM_IP); });

  server.on("/unbrake", []() {
    handBrake=false;
    server.send(200,"text/plain","OK");
  });

  // ── EMERGENCY BRAKE — strongest stop ──
  server.on("/brake", []() {
    autoMode=false; handBrake=true; gloveMode=false;
    emergencyBrake();
    delay(50);
    emergencyBrake(); // double for guarantee
    server.send(200,"text/plain","OK");
  });

  // ── SMOOTH STOP — joystick release ──
  server.on("/smoothstop", []() {
    if (!handBrake) smoothStop();
    server.send(200,"text/plain","OK");
  });

  server.on("/move", []() {
    if (gloveMode) { server.send(200,"text/plain","GLOVE_MODE"); return; }
    handBrake=false; autoMode=false;
    String dir=server.arg("dir");
    int    spd=server.arg("spd").toInt();
    executeMove(dir, spd);
    server.send(200,"text/plain","OK");
  });

  server.on("/glove", []() {
    int on=server.arg("on").toInt();
    gloveMode=(on==1);
    if (!gloveMode) smoothStop();
    autoMode=false; handBrake=false;
    server.send(200,"text/plain",gloveMode?"ON":"OFF");
  });

  server.on("/glovecmd", []() {
    server.send(200,"text/plain",String(glovePacket.cmd));
  });

  server.on("/stop",   []() { smoothStop(); server.send(200,"text/plain","OK"); });
  server.on("/auto",   []() { handBrake=false; gloveMode=false; autoMode=true;  server.send(200,"text/plain","OK"); });
  server.on("/manual", []() { handBrake=false; gloveMode=false; autoMode=false; smoothStop(); server.send(200,"text/plain","OK"); });
  server.on("/limit",  []() { maxLimit=server.arg("val").toInt(); server.send(200,"text/plain","OK"); });
  server.on("/getdist",[]() { server.send(200,"text/plain",String(getDistance())); });

  server.on("/getbatt", []() {
    float v=(analogRead(batteryPin)/4095.0)*3.3*4.03;
    server.send(200,"text/plain",String(constrain(map((int)(v*100),1050,1260,0,100),0,100)));
  });

  server.begin();
  Serial.println("Ready — " + WiFi.softAPIP().toString());
}

// ════════════════════════════════════════════════════════════
//  LOOP
// ════════════════════════════════════════════════════════════
void loop() {
  server.handleClient();

  // ── Glove watchdog — 300ms packet nahi aaya toh smooth stop ──
  if (gloveMode && (millis() - lastGlovePacketTime > 300)) {
    if (currentDir != "stop") {
      Serial.println("GLOVE TIMEOUT — STOP");
      smoothStop();
    }
  }

  // ── Glove command ──
  if (gloveMode && newGloveCmd) {
    newGloveCmd=false;
    if (!handBrake && !autoMode) {
      String dir = String(glovePacket.cmd);
      int    spd = constrain(glovePacket.speed,0,255);
      // Stop command pe emergency brake
      if (dir == "stop") {
        smoothStop();
      } else {
        executeMove(dir, spd);
      }
    }
  }

  if (autoMode) runAutoMode();
}

#include <WiFi.h>
#include <WebServer.h>

// ================= WiFi =================
const char* ssid = "TEST_ESP32";
const char* password = "12345678";
WebServer server(80);

// ================= L298N =================
#define INA 14
#define INB 27
#define INC 26
#define IND 25
#define ENA_PIN 32    // ← ты поменял местами ENA и ENB
#define ENB_PIN 33

// ================= Line Sensor Pins =================
const int SENSOR_PINS[5] = {16, 17, 18, 19, 21};

// ================= PWM =================
const int PWM_FREQ = 20000;
const int PWM_RES  = 8;

// ================= Speed =================
uint8_t manualSpeed = 230;
uint8_t autoSpeed   = 220;   // для Obstacle и Follow
uint8_t lineSpeed   = 175;   // отдельная скорость для линии (рекомендую)
uint8_t lineSpeedTurn   = 254;

// ================= Ultrasonic =================
#define TRIG_PIN 4
#define ECHO_PIN 5

// ================= Flags =================
bool forwardCmd = false;
bool backCmd    = false;
bool leftCmd    = false;
bool rightCmd   = false;

bool obstacleMode = false;
bool followMode   = false;
bool lineMode     = false;

// ================= Auto Obstacle =================
enum AutoState { AUTO_IDLE, AUTO_FORWARD, AUTO_BACKWARD, AUTO_TURN };
AutoState autoState = AUTO_IDLE;

unsigned long autoStateStart = 0;
bool turnLeftNext = true;

const unsigned long BACK_TIME = 600;
const unsigned long TURN_TIME = 700;
const float OBSTACLE_DIST_CM = 20.0;

// ================= HTML =================
const char index_html[] PROGMEM = R"=====(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
body{background:#111;color:#fff;font-family:Arial;text-align:center}
.btn{padding:16px 26px;margin:6px;border:none;border-radius:10px;background:#333;color:#fff;font-size:16px;width:140px}
.btn:active{background:#555}
.btn-on{background:#2e7d32}
.btn-off{background:#c62828}
</style>
</head>
<body>
<h2>ESP32 Web Robot Car</h2>
<button class="btn" onmousedown="cmd('f',1)" onmouseup="cmd('f',0)" ontouchstart="cmd('f',1)" ontouchend="cmd('f',0)">Forward</button><br>
<button class="btn" onmousedown="cmd('l',1)" onmouseup="cmd('l',0)" ontouchstart="cmd('l',1)" ontouchend="cmd('l',0)">Left</button>
<button class="btn" onclick="cmd('s',1)">Stop</button>
<button class="btn" onmousedown="cmd('r',1)" onmouseup="cmd('r',0)" ontouchstart="cmd('r',1)" ontouchend="cmd('r',0)">Right</button><br>
<button class="btn" onmousedown="cmd('b',1)" onmouseup="cmd('b',0)" ontouchstart="cmd('b',1)" ontouchend="cmd('b',0)">Backward</button>

<h3>Obstacle Avoid Mode</h3>
<button class="btn btn-on" onclick="mode(1)">ON</button>
<button class="btn btn-off" onclick="mode(0)">OFF</button>

<h3>Follow Mode</h3>
<button class="btn btn-on" onclick="follow(1)">ON</button>
<button class="btn btn-off" onclick="follow(0)">OFF</button>

<h3>Line Follower</h3>
<button class="btn btn-on" onclick="line(1)">ON</button>
<button class="btn btn-off" onclick="line(0)">OFF</button>

<script>
function cmd(d,s){ fetch(`/cmd?dir=${d}&state=${s}`); }
function mode(v){ fetch(`/mode?auto=${v}`); }
function follow(v){ fetch(`/follow?f=${v}`); }
function line(v){ fetch(`/line?l=${v}`); }
</script>
</body>
</html>
)=====";

// ================= Motor =================
void setMotorSpeed(uint8_t left, uint8_t right) {
  ledcWrite(ENA_PIN, left);
  ledcWrite(ENB_PIN, right);
}

void driveStop() {
  digitalWrite(INA,LOW); digitalWrite(INB,LOW);
  digitalWrite(INC,LOW); digitalWrite(IND,LOW);
  setMotorSpeed(0,0);
}

void driveForward(uint8_t s) {
  digitalWrite(INA,HIGH); digitalWrite(INB,LOW);
  digitalWrite(INC,HIGH); digitalWrite(IND,LOW);
  setMotorSpeed(s,s);
}

void driveBackward(uint8_t s) {
  digitalWrite(INA,LOW);  digitalWrite(INB,HIGH);
  digitalWrite(INC,LOW);  digitalWrite(IND,HIGH);
  setMotorSpeed(s,s);
}

void driveRight(uint8_t s) {
  digitalWrite(INA,LOW);  digitalWrite(INB,HIGH);
  digitalWrite(INC,HIGH); digitalWrite(IND,LOW);
  setMotorSpeed(s,s);
}

void driveLeft(uint8_t s) {
  digitalWrite(INA,HIGH); digitalWrite(INB,LOW);
  digitalWrite(INC,LOW);  digitalWrite(IND,HIGH);
  setMotorSpeed(s,s);
}

// ===== Диагональ =====
void driveForwardLeft(uint8_t s) {
  digitalWrite(INA,HIGH); digitalWrite(INB,LOW);
  digitalWrite(INC,LOW);  digitalWrite(IND,HIGH);
  setMotorSpeed(s,s);
}

void driveForwardRight(uint8_t s) {
  digitalWrite(INA,LOW);  digitalWrite(INB,HIGH);
  digitalWrite(INC,HIGH); digitalWrite(IND,LOW);
  setMotorSpeed(s,s);
}

// ================= Ultrasonic =================
float getDistanceCm() {
  digitalWrite(TRIG_PIN,LOW); delayMicroseconds(2);
  digitalWrite(TRIG_PIN,HIGH); delayMicroseconds(10);
  digitalWrite(TRIG_PIN,LOW);
  unsigned long d = pulseIn(ECHO_PIN,HIGH,30000);
  if(d==0) return -1;
  return d*0.0343/2;
}

// ================= Line Follower =================
void handleLineFollower() {
  int s1 = digitalRead(SENSOR_PINS[0]);
  int s2 = digitalRead(SENSOR_PINS[1]);
  int s3 = digitalRead(SENSOR_PINS[2]);
  int s4 = digitalRead(SENSOR_PINS[3]);
  int s5 = digitalRead(SENSOR_PINS[4]);

  Serial.print("S: ");
  Serial.print(s1); Serial.print(s2); Serial.print(s3); 
  Serial.print(s4); Serial.print(s5);

  if(s1 == 1 && s2 == 1 && s3 == 1 && s4 == 1 && s5 == 1) {
    Serial.println(" → НАЗАД");

    driveBackward(210);
    delay(80);   // ← регулируй (80–200 мс)

    driveStop();
  }


  if(s1 == 1 && s2 == 0 && s3 == 0 && s4 == 0 && s5 == 0){
    Serial.println(" → ВПРАВО");
    driveForwardRight(lineSpeedTurn);
  }
  if(s1 == 0 && s2 == 0 && s3 == 0 && s4 == 0 && s5 == 1){
    Serial.println(" → ВЛЕВО");
    driveForwardLeft(lineSpeedTurn); // здесь я сделал поворот видно условие
  }



  if (s3 == 0 || s3 == 0 && s2 == 0 && s4 == 0) {
    Serial.println(" → ПРЯМО");
    driveForward(lineSpeed);
  }
  else if ((s2 == 0 && s4 == 0)) {
    Serial.println(" → ПРЯМО");
    driveForward(lineSpeed);
  }


  else if(s1 == 0 && s2 == 1 && s3 == 1 && s4 == 1 && s5 == 1){
    Serial.println(" → ВЛЕВО");
    driveForwardLeft(lineSpeedTurn);
  }
  else if(s1 == 1 && s2 == 1 && s3 == 1 && s4 == 1 && s5 == 0){
    Serial.println(" → ВПРАВО");
    driveForwardRight(lineSpeedTurn);
  }

  else if(s1 == 1 && s2 == 0 && s3 == 0 && s4 == 0 && s5 == 0){
    Serial.println(" → ВПРАВО");
    driveForwardRight(lineSpeedTurn);
  }
  else if(s1 == 0 && s2 == 0 && s3 == 0 && s4 == 0 && s5 == 1){
    Serial.println(" → ВЛЕВО");
    driveForwardLeft(lineSpeedTurn);
  }


  else if (s1 == 1 && s4 == 1 && s5 == 1) {
    Serial.println(" → ВЛЕВО");
    driveForwardLeft(lineSpeedTurn);
  }
  else if (s1 == 1 && s2 == 1 && s5 == 1) {
    Serial.println(" → ВПРАВО");
    driveForwardRight(lineSpeedTurn);
  }


// ПРЯМО

  else if (s1 == 1 || s2 == 1 || (s1 == 1 && s2 == 1)) {
    Serial.println(" → ВПРАВО");
    driveForwardRight(lineSpeedTurn);
  }
  else if (s4 == 1 || s5 == 1 || (s4 == 1 && s5 == 1)) {
    Serial.println(" → ВЛЕВО");
    driveForwardLeft(lineSpeedTurn);
  }

  else if(s3 == 1){

    if (s1 == 1 && s4 == 1 && s5 == 1) {
      Serial.println(" → ВПРАВО");
      driveForwardRight(lineSpeedTurn); // вот здесь
    }
    else if (s1 == 1 && s2 == 1 && s5 == 1) {
      Serial.println(" → ВЛЕВО");
      driveForwardLeft(lineSpeedTurn);
    }


    else if (s1 == 1 || s2 == 1 || (s1 == 1 && s2 == 1)) {
      Serial.println(" → ВПРАВО");
      driveForwardRight(lineSpeedTurn);
    }
    else if (s4 == 1 || s5 == 1 || (s4 == 1 && s5 == 1)) {
      Serial.println(" → ВЛЕВО");
      driveForwardLeft(lineSpeedTurn);// и здесь
    }
  }
  else {
    Serial.println(" → СТОП");
    driveStop();
  }
}
// ================= Obstacle =================
void handleObstacle() {
  unsigned long now = millis();
  switch(autoState) {
    case AUTO_IDLE: autoState = AUTO_FORWARD; break;

    case AUTO_FORWARD: {
      float d = getDistanceCm();
      if (d > 0 && d < OBSTACLE_DIST_CM) {
        driveStop();
        autoState = AUTO_BACKWARD;
        autoStateStart = now;
      } else {
        driveForward(autoSpeed);
      }
      break;
    }

    case AUTO_BACKWARD:
      if (now - autoStateStart >= BACK_TIME) {
        autoState = AUTO_TURN;
        autoStateStart = now;
      } else {
        driveBackward(autoSpeed);
      }
      break;

    case AUTO_TURN:
      turnLeftNext ? driveLeft(autoSpeed) : driveRight(autoSpeed);
      if (now - autoStateStart >= TURN_TIME) {
        driveStop();
        turnLeftNext = !turnLeftNext;
        autoState = AUTO_FORWARD;
      }
      break;
  }
}

// ================= Follow =================
void handleFollow() {
  float d = getDistanceCm();
  if (d < 0) { driveStop(); return; }
  if (d < 12)      driveBackward(autoSpeed);
  else if (d <= 20) driveStop();
  else if (d <= 45) driveForward(autoSpeed);
  else             driveStop();
}

// ================= Manual =================
void handleManual() {
  if(forwardCmd) driveForward(manualSpeed);
  else if(backCmd) driveBackward(manualSpeed);
  else if(leftCmd) driveLeft(manualSpeed);
  else if(rightCmd) driveRight(manualSpeed);
  else driveStop();
}

// ================= HTTP =================
void handleRoot() { 
  server.send_P(200, "text/html", index_html); 
}

void resetModes() {
  obstacleMode = false;
  followMode = false;
  lineMode = false;
  forwardCmd = backCmd = leftCmd = rightCmd = false;
  driveStop();
}

void handleCmd() {
  if(obstacleMode || followMode || lineMode) {
    server.send(200,"text/plain","IGNORED");
    return;
  }
  String d = server.arg("dir");
  bool p = server.arg("state").toInt();

  if(d=="f") forwardCmd = p;
  else if(d=="b") backCmd = p;
  else if(d=="l") leftCmd = p;
  else if(d=="r") rightCmd = p;
  else if(d=="s") resetModes();

  server.send(200,"text/plain","OK");
}

void handleMode() {
  resetModes();
  obstacleMode = server.arg("auto").toInt();
  if (obstacleMode) autoState = AUTO_FORWARD;
  server.send(200,"text/plain","OK");
}

void handleFollowHttp() {
  resetModes();
  followMode = server.arg("f").toInt();
  server.send(200,"text/plain","OK");
}

void handleLineHttp() {
  resetModes();
  lineMode = server.arg("l").toInt();
  server.send(200,"text/plain","OK");
}

// ================= SETUP =================
void setup() {
  Serial.println("Starting WiFi...");
  WiFi.mode(WIFI_AP);
  bool ok = WiFi.softAP(ssid, password);

  if(ok) Serial.println("WiFi started OK");
  else Serial.println("WiFi FAILED");

  Serial.begin(115200);
  delay(500);                    // небольшая пауза
  Serial.println("\n\n=== ESP32 Line Follower Started ===");
  Serial.println("WiFi: ESP32_Car");
  Serial.println("IP: 192.168.4.1");

  pinMode(INA,OUTPUT); pinMode(INB,OUTPUT);
  pinMode(INC,OUTPUT); pinMode(IND,OUTPUT);

  ledcAttach(ENA_PIN, PWM_FREQ, PWM_RES);
  ledcAttach(ENB_PIN, PWM_FREQ, PWM_RES);

  for(int i = 0; i < 5; i++) pinMode(SENSOR_PINS[i], INPUT);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);

  server.on("/", handleRoot);
  server.on("/cmd", handleCmd);
  server.on("/mode", handleMode);
  server.on("/follow", handleFollowHttp);
  server.on("/line", handleLineHttp);
  server.begin();

  driveStop();   // важно

  Serial.println("\nESP32 Car Ready!");
  Serial.println("IP: 192.168.4.1");
}

// ================= LOOP =================
void loop() {
  server.handleClient();

  if (lineMode)        handleLineFollower();
  else if (followMode) handleFollow();
  else if (obstacleMode) handleObstacle();
  else                 handleManual();

  delay(10);
}
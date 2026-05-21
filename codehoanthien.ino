// Recommended: Use Frank de Brabander's LiquidCrystal_I2C library for ESP8266 compatibility
#define BLYNK_TEMPLATE_ID "TMPL6MNMxrvms"
#define BLYNK_TEMPLATE_NAME "DO MUC NUOC"
#define BLYNK_AUTH_TOKEN "ST76_wvnfdXyeTWBL2nYxqmwJn5C1j8Z"

#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ESP8266WebServer.h>

// Wi-Fi (STA credentials)
char ssid[] = "Phuoc Sang";
char pass[] = "chaolongfuocsang";
char new_ssid[32] = "";
char new_pass[64] = "";
bool use_new_credentials = false;

// AP WiFi
const char* ap_ssid = "ESP8266_AP";
const char* ap_pass = "12345678";
IPAddress apIP(192, 168, 4, 1);

// Pins
#define trigPin D1
#define echoPin D0
#define ledPin D4
#define relayPin D7
#define buzzerPin D5
#define buttonPin D6

// LCD
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Measurement
unsigned long lastMeasureTime = 0;
const unsigned long measureInterval = 1000;
const float waterLevelThreshold = 10.0;
const float emptyWaterDist = 30.0;

// State
float distance = 0;
bool pumpState = false, buzzerManualOff = false, lastBuzzerState = false;
float lastDisplayedDistance = -1, lastDisplayedPercent = -1;
bool lastPumpStateSync = false, isAPMode = false, manualPumpOff = false;
bool manualPumpOverride = false, manualPumpState = false;
unsigned long manualControlTime = 0;
const unsigned long manualTimeout = 1000;
bool waterWasBelowThreshold = false;

// Button
int buttonState = HIGH, lastButtonState = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;

// Blynk Virtual Pins
#define VPIN_WATER_PERCENT V1
#define VPIN_WATER_DISTANCE V2
#define VPIN_PUMP_STATE V3
#define VPIN_BUZZER_BUTTON V4
#define VPIN_PUMP_MANUAL V5

// WebServer
ESP8266WebServer server(80);

void setup() {
  Serial.begin(115200);
  Wire.begin(D3, D2);
  lcd.init();
  lcd.backlight();
  lcd.print("Loading.....");
  delay(2000);
  lcd.clear();

  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  pinMode(ledPin, OUTPUT);
  pinMode(relayPin, OUTPUT);
  pinMode(buzzerPin, OUTPUT);
  pinMode(buttonPin, INPUT_PULLUP);
  digitalWrite(ledPin, LOW);
  digitalWrite(relayPin, LOW);
  digitalWrite(buzzerPin, LOW);

  distance = measureDistanceCM();
  float percent = calculateWaterPercent(distance);
  controlPumpAndBuzzer(distance);
  lcd.setCursor(0, 0);
  lcd.print("Khoang cach:");
  updateLCD(distance, percent, pumpState);
  lastDisplayedDistance = distance;
  lastDisplayedPercent = percent;

  // Start WiFi in AP+STA mode
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(ap_ssid, ap_pass);
  
  // Attempt STA connection
  WiFi.disconnect(); // Ensure clean state
  WiFi.begin(use_new_credentials && new_ssid[0] ? new_ssid : ssid, use_new_credentials && new_pass[0] ? new_pass : pass);
  Serial.println("Attempting STA connection to: " + String(use_new_credentials && new_ssid[0] ? new_ssid : ssid));
  unsigned long startAttempt = millis();
  bool connected = false;
  while (millis() - startAttempt < 12000) { // Extended timeout to 12s
    if (WiFi.status() == WL_CONNECTED) {
      connected = true;
      break;
    }
    delay(500);
  }
  
  isAPMode = !connected;
  if (connected) {
    Serial.println("STA Connected, IP: " + WiFi.localIP().toString());
    Blynk.config(BLYNK_AUTH_TOKEN);
    Blynk.connect(10000);
    if (Blynk.connected()) syncBlynk(distance, percent);
  } else {
    Serial.println("STA Failed, status: " + String(WiFi.status()) + ", running AP mode");
  }
  displayWiFiStatus(connected, connected ? "STA" : "AP");

  server.on("/", handleRoot);
  server.on("/api/status", handleStatus);
  server.on("/api/pump/on", handlePumpOn);
  server.on("/api/pump/off", handlePumpOff);
  server.on("/api/pump/reset", handlePumpReset);
  server.on("/api/buzzer/off", handleBuzzerOff);
  server.on("/api/switch/ap", handleSwitchToAP);
  server.on("/api/switch/sta", handleSwitchToSTA);
  server.on("/config", handleConfig);
  server.on("/api/config", HTTP_POST, handleConfigSubmit);
  server.begin();
}

float measureDistanceCM() {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  long duration = pulseIn(echoPin, HIGH, 30000);
  float dist = duration ? duration * 0.034 / 2 : -1.0;
  return (dist < 0 || dist > 400) ? -1.0 : dist;
}

float calculateWaterPercent(float dist) {
  if (dist < 0) return -1.0;
  if (dist <= waterLevelThreshold) return 100.0;
  if (dist >= emptyWaterDist) return 0.0;
  return constrain((emptyWaterDist - dist) / (emptyWaterDist - waterLevelThreshold) * 100.0, 0.0, 100.0);
}

void updateLCD(float dist, float percent, bool pump) {
  lcd.setCursor(0, 1);
  lcd.print("                ");
  lcd.setCursor(0, 1);
  if (dist < 0) lcd.print("Khong thay vat");
  else {
    lcd.print(dist, 1);
    lcd.print("cm ");
    lcd.print(percent, 0);
    lcd.print("% ");
    lcd.print(pump ? "ON" : "OFF");
  }
}

void controlPumpAndBuzzer(float dist) {
  bool shouldPumpAuto = dist > waterLevelThreshold;
  if (dist <= waterLevelThreshold) waterWasBelowThreshold = true;
  else if (dist > waterLevelThreshold && waterWasBelowThreshold && (manualPumpOverride || buzzerManualOff)) {
    manualPumpOverride = false;
    manualPumpOff = false;
    buzzerManualOff = false;
    waterWasBelowThreshold = false;
  }

  pumpState = manualPumpOverride ? manualPumpState : shouldPumpAuto;
  digitalWrite(relayPin, pumpState ? HIGH : LOW);
  digitalWrite(ledPin, pumpState ? HIGH : LOW);

  bool buzzerShouldBeOn = shouldPumpAuto && !buzzerManualOff;
  if (!shouldPumpAuto) buzzerManualOff = false;
  digitalWrite(buzzerPin, buzzerShouldBeOn ? HIGH : LOW);

  if (!isAPMode && WiFi.status() == WL_CONNECTED && Blynk.connected()) {
    if (pumpState != lastPumpStateSync) {
      Blynk.virtualWrite(VPIN_PUMP_STATE, pumpState);
      Blynk.virtualWrite(VPIN_PUMP_MANUAL, pumpState);
      lastPumpStateSync = pumpState;
    }
    if (buzzerShouldBeOn != lastBuzzerState) {
      Blynk.virtualWrite(VPIN_BUZZER_BUTTON, buzzerShouldBeOn);
      lastBuzzerState = buzzerShouldBeOn;
    }
  }
}

void syncBlynk(float dist, float percent) {
  Blynk.virtualWrite(VPIN_WATER_PERCENT, percent);
  Blynk.virtualWrite(VPIN_WATER_DISTANCE, dist);
  Blynk.virtualWrite(VPIN_PUMP_STATE, pumpState);
  Blynk.virtualWrite(VPIN_PUMP_MANUAL, pumpState);
  Blynk.virtualWrite(VPIN_BUZZER_BUTTON, digitalRead(buzzerPin));
  lastPumpStateSync = pumpState;
  lastBuzzerState = digitalRead(buzzerPin);
}

BLYNK_WRITE(VPIN_BUZZER_BUTTON) {
  buzzerManualOff = !param.asInt();
  digitalWrite(buzzerPin, buzzerManualOff ? LOW : (distance > waterLevelThreshold ? HIGH : LOW));
  if (!isAPMode && WiFi.status() == WL_CONNECTED && Blynk.connected()) {
    Blynk.virtualWrite(VPIN_BUZZER_BUTTON, !buzzerManualOff);
    lastBuzzerState = !buzzerManualOff;
  }
  updateLCD(distance, calculateWaterPercent(distance), pumpState);
}

BLYNK_WRITE(VPIN_PUMP_MANUAL) {
  manualPumpOverride = true;
  manualPumpState = param.asInt();
  manualPumpOff = !manualPumpState;
  waterWasBelowThreshold = false;
  controlPumpAndBuzzer(distance);
  if (!isAPMode && WiFi.status() == WL_CONNECTED && Blynk.connected()) {
    Blynk.virtualWrite(VPIN_PUMP_STATE, manualPumpState);
    lastPumpStateSync = manualPumpState;
  }
}

void checkButton() {
  int reading = digitalRead(buttonPin);
  if (reading != lastButtonState) lastDebounceTime = millis();
  if (millis() - lastDebounceTime > debounceDelay && reading != buttonState) {
    buttonState = reading;
    if (buttonState == LOW) {
      buzzerManualOff = true;
      digitalWrite(buzzerPin, LOW);
      if (!isAPMode && WiFi.status() == WL_CONNECTED && Blynk.connected()) {
        Blynk.virtualWrite(VPIN_BUZZER_BUTTON, 0);
        lastBuzzerState = false;
      }
      updateLCD(distance, calculateWaterPercent(distance), pumpState);
    }
  }
  lastButtonState = reading;
}

void loop() {
  server.handleClient();
  if (WiFi.status() == WL_CONNECTED) {
    if (isAPMode) {
      isAPMode = false;
      Serial.println("Switched to STA mode");
      Blynk.config(BLYNK_AUTH_TOKEN);
      Blynk.connect(10000);
    }
    if (Blynk.connected()) Blynk.run();
  } else if (!isAPMode) {
    Serial.println("STA disconnected, attempting reconnect...");
    WiFi.disconnect();
    WiFi.begin(use_new_credentials && new_ssid[0] ? new_ssid : ssid, use_new_credentials && new_pass[0] ? new_pass : pass);
    unsigned long startAttempt = millis();
    bool connected = false;
    while (millis() - startAttempt < 12000) {
      if (WiFi.status() == WL_CONNECTED) {
        connected = true;
        break;
      }
      delay(500);
    }
    if (connected) {
      Serial.println("STA Reconnected, IP: " + WiFi.localIP().toString());
      Blynk.config(BLYNK_AUTH_TOKEN);
      Blynk.connect(10000);
      if (Blynk.connected()) syncBlynk(distance, calculateWaterPercent(distance));
      displayWiFiStatus(true, "STA");
    } else {
      Serial.println("STA Reconnect failed, status: " + String(WiFi.status()) + ", staying in AP mode");
      isAPMode = true;
      displayWiFiStatus(false, "STA");
    }
  }

  if (millis() - lastMeasureTime >= measureInterval) {
    lastMeasureTime = millis();
    distance = measureDistanceCM();
    float percent = calculateWaterPercent(distance);
    controlPumpAndBuzzer(distance);
    updateLCD(distance, percent, pumpState);
    lastDisplayedDistance = distance;
    lastDisplayedPercent = percent;
    if (!isAPMode && WiFi.status() == WL_CONNECTED && Blynk.connected()) syncBlynk(distance, percent);
  }
  checkButton();
}

void displayWiFiStatus(bool success, String mode) {
  bool originalLedState = digitalRead(ledPin);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(mode + (success ? " Connected" : " Failed"));
  if (success && mode == "STA") {
    lcd.setCursor(0, 1);
    lcd.print(WiFi.localIP().toString());
    digitalWrite(ledPin, HIGH);
    delay(4000); // Display IP for 4 seconds
  } else {
    lcd.setCursor(0, 1);
    lcd.print(success ? "AP Mode" : "AP Mode");
    digitalWrite(ledPin, success ? HIGH : LOW);
    delay(500); // Short delay for non-STA or failed connection
  }
  digitalWrite(ledPin, originalLedState);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Khoang cach:");
  updateLCD(distance, calculateWaterPercent(distance), pumpState);
}

String webPage() {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>DO MUC NUOC</title><style>";
  html += "body{font-family:Arial;text-align:center;background:#f4f6fb}";
  html += ".card{max-width:420px;margin:18px auto;padding:16px;background:#fff;border-radius:12px;box-shadow:0 6px 18px rgba(0,0,0,0.06)}";
  html += "button{padding:10px 14px;margin:6px;border-radius:8px;border:none;cursor:pointer}";
  html += ".on{background:#28a745;color:#fff}.off{background:#dc3545;color:#fff}";
  html += ".reset{background:#007bff;color:#fff}.mute{background:#6c757d;color:#fff}";
  html += "input[type=text],input[type=password]{padding:8px;margin:6px;width:80%;border-radius:6px;border:1px solid #ccc}";
  html += ".config{background:#ffc107;color:#000}";
  html += ".notification{display:none;position:fixed;top:10px;left:50%;transform:translateX(-50%);padding:12px 24px;border-radius:8px;color:#fff;font-weight:bold}";
  html += ".success{background:#28a745}.error{background:#dc3545}";
  html += "</style><script>";
  html += "async function fetchStatus(){try{let r=await fetch('/api/status');let j=await r.json();";
  html += "document.getElementById('dist').innerText=j.distance>=0?j.distance.toFixed(1)+' cm':'N/A';";
  html += "document.getElementById('pct').innerText=j.percent>=0?j.percent.toFixed(0)+' %':'N/A';";
  html += "document.getElementById('pump').innerText=j.pump?'ON':'OFF';";
  html += "document.getElementById('buz').innerText=j.buzzer?'ON':'OFF';";
  html += "document.getElementById('pump').style.color=j.pump?'green':'red';";
  html += "document.getElementById('buz').style.color=j.buzzer?'green':'red';";
  html += "document.getElementById('mode').innerText=j.isAP?'AP':'STA';";
  html += "}catch(e){}}";
  html += "async function doAction(path){try{await fetch(path);fetchStatus();}catch(e){}}";
  html += "function showNotification(status){const n=document.getElementById('notification');";
  html += "n.style.display='block';n.className='notification '+status;";
  html += "n.innerText=status==='success'?'Kết nối '+(status==='success'&&window.location.pathname.includes('sta')?'STA':'AP')+' thành công!':";
  html += "'Kết nối '+(window.location.pathname.includes('sta')?'STA':'AP')+' thất bại, đã chuyển sang chế độ AP.';";
  html += "setTimeout(()=>{n.style.display='none';},5000);}";
  html += "window.onload=()=>{fetchStatus();setInterval(fetchStatus,500);";
  html += "const s=new URLSearchParams(window.location.search).get('status');";
  html += "if(s==='success'||s==='failed') showNotification(s);history.replaceState(null,null,'/');};";
  html += "</script></head><body><div id='notification' class='notification'></div>";
  html += "<div class='card'><h2>Hệ thống đo mực nước</h2>";
  html += "<p style='font-size:22px'><span id='dist'>--</span></p>";
  html += "<p style='margin:8px 0;padding:8px;border-radius:8px;background:#f7f9fc;text-align:left'>";
  html += "<b>Mức nước:</b> <span id='pct'>--</span></p>";
  html += "<p style='margin:8px 0;padding:8px;border-radius:8px;background:#f7f9fc;text-align:left'>";
  html += "<b>Máy bơm:</b> <span id='pump'>--</span></p>";
  html += "<p style='margin:8px 0;padding:8px;border-radius:8px;background:#f7f9fc;text-align:left'>";
  html += "<b>Còi:</b> <span id='buz'>--</span></p>";
  html += "<p style='font-size:12px;color:#666'>Chế độ WiFi: <b><span id='mode'>--</span></b></p>";
  html += "<div><button class='on' onclick=\"doAction('/api/pump/on')\">Bật bơm</button>";
  html += "<button class='off' onclick=\"doAction('/api/pump/off')\">Tắt bơm</button>";
  html += "<button class='reset' onclick=\"doAction('/api/pump/reset')\">Reset -> Auto</button>";
  html += "<button class='mute' onclick=\"doAction('/api/buzzer/off')\">Tắt còi</button></div>";
  html += "<div style='margin-top:12px'>";
  html += "<button onclick=\"doAction('/api/switch/ap')\">Chuyển AP</button>";
  html += "<button onclick=\"doAction('/api/switch/sta')\">Chuyển STA</button>";
  html += "<button class='config' onclick=\"window.location.href='/config'\">Cấu hình WiFi</button></div>";
  html += "<p style='font-size:12px;color:#666'>Ghi chú: Nếu tắt bơm thủ công, hệ thống sẽ không tự bật lại cho đến khi mực nước <= 10cm rồi lại >10cm.</p>";
  html += "</div></body></html>";
  return html;
}

void handleRoot() { server.send(200, "text/html", webPage()); }

void handleStatus() {
  String j = "{\"distance\":" + String(distance, 1) + ",\"percent\":" + String(calculateWaterPercent(distance), 1) +
             ",\"pump\":" + (pumpState ? "true" : "false") + ",\"buzzer\":" + ((digitalRead(buzzerPin) == HIGH && !buzzerManualOff) ? "true" : "false") +
             ",\"isAP\":" + (isAPMode ? "true" : "false") + "}";
  server.send(200, "application/json", j);
}

void handlePumpOn() {
  manualPumpOverride = true;
  manualPumpState = true;
  manualPumpOff = false;
  waterWasBelowThreshold = false;
  controlPumpAndBuzzer(distance);
  if (!isAPMode && WiFi.status() == WL_CONNECTED && Blynk.connected()) {
    Blynk.virtualWrite(VPIN_PUMP_MANUAL, 1);
    lastPumpStateSync = true;
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

void handlePumpOff() {
  manualPumpOverride = true;
  manualPumpState = false;
  manualPumpOff = true;
  waterWasBelowThreshold = false;
  controlPumpAndBuzzer(distance);
  if (!isAPMode && WiFi.status() == WL_CONNECTED && Blynk.connected()) {
    Blynk.virtualWrite(VPIN_PUMP_MANUAL, 0);
    lastPumpStateSync = false;
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

void handlePumpReset() {
  manualPumpOverride = false;
  manualPumpOff = false;
  buzzerManualOff = false;
  waterWasBelowThreshold = false;
  controlPumpAndBuzzer(distance);
  if (!isAPMode && WiFi.status() == WL_CONNECTED && Blynk.connected()) {
    Blynk.virtualWrite(VPIN_PUMP_MANUAL, pumpState);
    Blynk.virtualWrite(VPIN_BUZZER_BUTTON, digitalRead(buzzerPin));
    lastPumpStateSync = pumpState;
    lastBuzzerState = digitalRead(buzzerPin);
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleBuzzerOff() {
  buzzerManualOff = true;
  digitalWrite(buzzerPin, LOW);
  if (!isAPMode && WiFi.status() == WL_CONNECTED && Blynk.connected()) {
    Blynk.virtualWrite(VPIN_BUZZER_BUTTON, 0);
    lastBuzzerState = false;
  }
  updateLCD(distance, calculateWaterPercent(distance), pumpState);
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleSwitchToAP() {
  WiFi.disconnect();
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ap_ssid, ap_pass);
  isAPMode = true;
  displayWiFiStatus(true, "AP");
  server.sendHeader("Location", "/?status=success");
  server.send(303);
}

void handleSwitchToSTA() {
  WiFi.disconnect();
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(use_new_credentials && new_ssid[0] ? new_ssid : ssid, use_new_credentials && new_pass[0] ? new_pass : pass);
  Serial.println("Attempting STA connection to: " + String(use_new_credentials && new_ssid[0] ? new_ssid : ssid));
  unsigned long startAttempt = millis();
  bool connected = false;
  while (millis() - startAttempt < 12000) {
    if (WiFi.status() == WL_CONNECTED) {
      connected = true;
      break;
    }
    delay(500);
  }
  if (connected) {
    Serial.println("STA Connected, IP: " + WiFi.localIP().toString());
    Blynk.config(BLYNK_AUTH_TOKEN);
    Blynk.connect(10000);
    isAPMode = false;
    displayWiFiStatus(true, "STA");
    if (Blynk.connected()) syncBlynk(distance, calculateWaterPercent(distance));
    server.sendHeader("Location", "/?status=success");
  } else {
    Serial.println("STA Failed, status: " + String(WiFi.status()) + ", switching to AP mode");
    WiFi.disconnect();
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ap_ssid, ap_pass);
    isAPMode = true;
    displayWiFiStatus(false, "STA");
    server.sendHeader("Location", "/?status=failed");
  }
  server.send(303);
}

void handleConfig() {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>Cấu hình WiFi</title><style>";
  html += "body{font-family:Arial;text-align:center;background:#f4f6fb}";
  html += ".card{max-width:420px;margin:18px auto;padding:16px;background:#fff;border-radius:12px;box-shadow:0 6px 18px rgba(0,0,0,0.06)}";
  html += "input[type=text],input[type=password]{padding:8px;margin:6px;width:80%;border-radius:6px;border:1px solid #ccc}";
  html += "button{padding:10px 14px;margin:6px;border-radius:8px;border:none;cursor:pointer;background:#007bff;color:#fff}";
  html += ".back{background:#6c757d;color:#fff}";
  html += "</style></head><body><div class='card'><h2>Cấu hình WiFi STA</h2>";
  html += "<form action='/api/config' method='POST'>";
  html += "<p><label>Tên WiFi:</label><br><input type='text' name='ssid' placeholder='Nhập Tên WiFi' required></p>";
  html += "<p><label>Mật khẩu:</label><br><input type='password' name='pass' placeholder='Nhập mật khẩu' required></p>";
  html += "<button type='submit'>Lưu và kết nối</button>";
  html += "</form><p><button class='back' onclick=\"window.location.href='/'\">Quay lại</button></p>";
  html += "<p style='font-size:12px;color:#666'>Lưu ý: Sau khi lưu, ESP8266 sẽ thử kết nối đến WiFi mới.</p>";
  html += "</div></body></html>";
  server.send(200, "text/html", html);
}

void handleConfigSubmit() {
  if (server.hasArg("ssid") && server.hasArg("pass")) {
    String input_ssid = server.arg("ssid");
    String input_pass = server.arg("pass");
    input_ssid.trim(); // Remove leading/trailing spaces
    input_pass.trim();
    if (input_ssid.length() == 0 || input_pass.length() == 0) {
      Serial.println("Error: Empty SSID or password");
      server.send(400, "text/html", "<h3>Lỗi: Tên WiFi hoặc mật khẩu trống</h3><p><a href='/config'>Quay lại</a></p>");
      return;
    }
    
    input_ssid.toCharArray(new_ssid, sizeof(new_ssid));
    input_pass.toCharArray(new_pass, sizeof(new_pass));
    use_new_credentials = true;
    
    Serial.println("Attempting STA connection to: " + String(new_ssid) + ", Password: " + String(new_pass));
    
    WiFi.disconnect();
    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(new_ssid, new_pass);
    
    unsigned long startAttempt = millis();
    bool connected = false;
    int retryCount = 0;
    const int maxRetries = 2;
    
    while (retryCount < maxRetries && !connected) {
      retryCount++;
      Serial.println("Connection attempt #" + String(retryCount));
      while (millis() - startAttempt < 12000) {
        if (WiFi.status() == WL_CONNECTED) {
          connected = true;
          break;
        }
        delay(500);
      }
      if (!connected && retryCount < maxRetries) {
        Serial.println("Retry: Disconnecting and reattempting...");
        WiFi.disconnect();
        delay(500);
        WiFi.begin(new_ssid, new_pass);
        startAttempt = millis();
      }
    }
    
    displayWiFiStatus(connected, "STA");
    if (connected) {
      Serial.println("Connected to new STA: " + String(new_ssid) + ", IP: " + WiFi.localIP().toString());
      Blynk.config(BLYNK_AUTH_TOKEN);
      Blynk.connect(10000);
      if (Blynk.connected()) syncBlynk(distance, calculateWaterPercent(distance));
      server.sendHeader("Location", "/?status=success");
    } else {
      Serial.println("Failed to connect to new STA: " + String(new_ssid) + ", status: " + String(WiFi.status()));
      WiFi.disconnect();
      WiFi.mode(WIFI_AP);
      WiFi.softAP(ap_ssid, ap_pass);
      isAPMode = true;
      server.sendHeader("Location", "/?status=failed");
    }
    server.send(303);
  } else {
    Serial.println("Error: Missing SSID or password");
    server.send(400, "text/html", "<h3>Lỗi: Thiếu Tên WiFi hoặc mật khẩu</h3><p><a href='/config'>Quay lại</a></p>");
  }
}
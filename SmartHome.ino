#define BLYNK_TEMPLATE_ID "TMPL65SRoXUg5"
#define BLYNK_TEMPLATE_NAME "Quickstart Template"
#define BLYNK_AUTH_TOKEN "w5YqL4s68FKI083Hj38VK-AcmqTCpzX3"

#define BLYNK_PRINT Serial

#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>
#include <PZEM004Tv30.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <Adafruit_Sensor.h>

// ==================== FIREBASE ====================
#include <Firebase_ESP_Client.h>
FirebaseData fbdo;
FirebaseAuth authfb;
FirebaseConfig configfb;

#define API_KEY "AIzaSyBoMsziybAUvBQUfKrmyXqB-1L8y6V8rC4"
#define DATABASE_URL "https://cobakamar-8af3f-default-rtdb.asia-southeast1.firebasedatabase.app"
// =================================================

char auth[] = BLYNK_AUTH_TOKEN;
char ssid[] = "Ovaninda";
char pass[] = "samsung55";

BlynkTimer timer;

// ===== PZEM004T =====
#define PZEM_RX 16
#define PZEM_TX 17
PZEM004Tv30 pzem(Serial2, PZEM_RX, PZEM_TX);

// ===== DHT22 =====
#define DHTPIN 4
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

// ===== LCD =====
LiquidCrystal_I2C lcd(0x27, 20, 4);

// ===== Relay & Button =====
const int relayPins[4] = { 18, 19, 23, 5 };
const int buttonPins[4] = { 26, 25, 33, 32 };
int relayState[4] = { LOW, LOW, LOW, LOW };
int lastButtonState[4] = { HIGH, HIGH, HIGH, HIGH };
unsigned long lastDebounceTime[4] = { 0, 0, 0, 0 };
const unsigned long debounceDelay = 200;

const int vPins[4] = { V1, V2, V3, V4 };
const char* lampuNames[4] = { "Lampu 1", "Fan", "Lampu 3", "Lampu 4" };

const char* firebaseRelayKeys[4] = {
  "lamp1",
  "fan",
  "lamp3",
  "lamp4"
};

// ===== LCD Mode =====
enum DisplayMode { LAMPU_STATUS, PZEM_DISPLAY, TEMPHUMID };
DisplayMode currentMode = PZEM_DISPLAY;
unsigned long lastActivityTime = 0;
unsigned long lastDisplaySwitch = 0;
const unsigned long idleTimeout = 5000;
const unsigned long displaySwitchTime = 5000;

// ===== Sensor Variables =====
float temperature = NAN;
float humidity    = NAN;
float voltage     = NAN;
float currentVal  = NAN;
float power       = NAN;
float energy      = NAN;
float frequency   = NAN;
float pf          = NAN;

// Forward declarations
void setRelay(int id, int state, String source);

// ===================== BLYNK =====================
BLYNK_CONNECTED() {
  for (int i = 0; i < 4; i++)
    Blynk.syncVirtual(vPins[i]);
}

BLYNK_WRITE_DEFAULT() {
  int pinIndex = request.pin - V1;
  if (pinIndex >= 0 && pinIndex < 4) {
    setRelay(pinIndex, param.asInt(), "Blynk");
  }
}

// ===================== RELAY =====================
void setRelay(int id, int state, String source) {
  relayState[id] = state;
  digitalWrite(relayPins[id], state);
  Blynk.virtualWrite(vPins[id], state);

  String path = "kamar_ovan/";
  path += firebaseRelayKeys[id];

  if (!Firebase.RTDB.setInt(&fbdo, path, state)) {
    Serial.print("Firebase setInt failed: ");
    Serial.println(fbdo.errorReason());
  }

  Serial.printf("%s %s (%s)\n",
                lampuNames[id], state ? "nyala" : "mati", source.c_str());

  showLampStatus();
  currentMode = LAMPU_STATUS;
  lastActivityTime = millis();
}

// ===================== SETUP =====================
void setup() {
  Serial.begin(115200);

  for (int i = 0; i < 4; i++) {
    pinMode(relayPins[i], OUTPUT);
    pinMode(buttonPins[i], INPUT_PULLUP);
    digitalWrite(relayPins[i], LOW);
  }

  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(3, 0); lcd.print("MONITORING AND");
  lcd.setCursor(5, 1); lcd.print("CONTROLLING");
  lcd.setCursor(5, 2); lcd.print("OVAN'S ROOM");
  delay(1500);

  dht.begin();
  Serial2.begin(9600, SERIAL_8N1, PZEM_RX, PZEM_TX);

  Blynk.begin(auth, ssid, pass);

  // ======== FIREBASE FIX FOR ANONYMOUS =========
  configfb.api_key = API_KEY;
  configfb.database_url = DATABASE_URL;
  authfb.user.email = "";
  authfb.user.password = "";

  Firebase.begin(&configfb, &authfb);
  Firebase.reconnectWiFi(true);
  // ==============================================

  Serial.println("Testing Firebase...");
  if (Firebase.RTDB.setInt(&fbdo, "test/value", 123)) {
    Serial.println("Firebase Write OK!");
  } else {
    Serial.print("Firebase Write FAILED: ");
    Serial.println(fbdo.errorReason());
  }

  timer.setInterval(2000L, updateSensors);
  timer.setInterval(5000L, sendPZEMData);
  timer.setInterval(10000L, firebaseSensorSend);
  timer.setInterval(3000L, firebaseReadRelay);
}

// ===================== LOOP =====================
void loop() {
  Blynk.run();
  timer.run();
  checkButtons();

  if (currentMode == LAMPU_STATUS && millis() - lastActivityTime > idleTimeout) {
    currentMode = PZEM_DISPLAY;
    lastDisplaySwitch = millis();
    showPZEMDataLCD();
  }

  if (currentMode != LAMPU_STATUS &&
      millis() - lastDisplaySwitch > displaySwitchTime) {

    currentMode = (currentMode == PZEM_DISPLAY) ? TEMPHUMID : PZEM_DISPLAY;

    if (currentMode == TEMPHUMID)
      showTemperatureHumidity();
    else
      showPZEMDataLCD();

    lastDisplaySwitch = millis();
  }
}

// ===================== BUTTON =====================
void checkButtons() {
  for (int i = 0; i < 4; i++) {
    int reading = digitalRead(buttonPins[i]);

    if (reading == LOW && lastButtonState[i] == HIGH &&
        millis() - lastDebounceTime[i] > debounceDelay) {

      lastDebounceTime[i] = millis();
      setRelay(i, !relayState[i], "Manual");
    }
    lastButtonState[i] = reading;
  }
}

// ===================== LCD =====================
void showTemperatureHumidity() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();

  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Temperature :");
  lcd.setCursor(12, 0);
  if (!isnan(t)) lcd.print(t, 1), lcd.print("C");
  else lcd.print("--.-C");

  lcd.setCursor(0, 2); lcd.print("Humidity    :");
  lcd.setCursor(12, 2);
  if (!isnan(h)) lcd.print(h, 1), lcd.print("%");
  else lcd.print("--.-%");

  if (!isnan(t)) Blynk.virtualWrite(V11, t);
  if (!isnan(h)) Blynk.virtualWrite(V12, h);
}

void showPZEMDataLCD() {
  lcd.clear();
  lcd.setCursor(0, 0);
  if (!isnan(voltage)) lcd.print("Voltage: "), lcd.print(voltage, 1), lcd.print(" V");
  else lcd.print("Voltage: --.- V");

  lcd.setCursor(0, 1);
  if (!isnan(currentVal)) lcd.print("Current: "), lcd.print(currentVal, 2), lcd.print(" A");
  else lcd.print("Current: --.- A");

  lcd.setCursor(0, 2);
  if (!isnan(power)) lcd.print("Power  : "), lcd.print(power, 1), lcd.print(" W");
  else lcd.print("Power  : --.- W");

  lcd.setCursor(0, 3);
  if (!isnan(energy)) lcd.print("Energy : "), lcd.print(energy, 3), lcd.print(" kWh");
  else lcd.print("Energy : --.- kWh");
}

void showLampStatus() {
  lcd.clear();
  for (int i = 0; i < 4; i++) {
    lcd.setCursor(0, i);
    lcd.print(lampuNames[i]);
    lcd.print(": ");
    lcd.print(relayState[i] ? "ON " : "OFF");
  }
}

// ===================== SENSOR UPDATE =====================
void updateSensors() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();

  if (!isnan(t)) temperature = t;
  if (!isnan(h)) humidity = h;

  Blynk.virtualWrite(V11, temperature);
  Blynk.virtualWrite(V12, humidity);

  float v = pzem.voltage();
  float i = pzem.current();
  float p = pzem.power();
  float e = pzem.energy();
  float f = pzem.frequency();
  float pfv = pzem.pf();

  if (!isnan(v)) voltage = v;
  if (!isnan(i)) currentVal = i;
  if (!isnan(p)) power = p;
  if (!isnan(e)) energy = e;
  if (!isnan(f)) frequency = f;
  if (!isnan(pfv)) pf = pfv;
}

void sendPZEMData() {
  Blynk.virtualWrite(V5, voltage);
  Blynk.virtualWrite(V6, currentVal);
  Blynk.virtualWrite(V7, power);
  Blynk.virtualWrite(V8, energy);
  Blynk.virtualWrite(V9, frequency);
  Blynk.virtualWrite(V10, pf);
}

// ===================== FIREBASE SEND =====================
void firebaseSensorSend() {

  Firebase.RTDB.setFloat(&fbdo, "kamar_ovan/dht22/suhu", temperature);
  Firebase.RTDB.setFloat(&fbdo, "kamar_ovan/dht22/kelembaban", humidity);

  Firebase.RTDB.setFloat(&fbdo, "kamar_ovan/pzem/voltage", voltage);
  Firebase.RTDB.setFloat(&fbdo, "kamar_ovan/pzem/current", currentVal);
  Firebase.RTDB.setFloat(&fbdo, "kamar_ovan/pzem/power", power);
  Firebase.RTDB.setFloat(&fbdo, "kamar_ovan/pzem/energy", energy);
  Firebase.RTDB.setFloat(&fbdo, "kamar_ovan/pzem/frequency", frequency);
  Firebase.RTDB.setFloat(&fbdo, "kamar_ovan/pzem/pf", pf);
}

// ===================== FIREBASE READ RELAY =====================
void firebaseReadRelay() {

  for (int i = 0; i < 4; i++) {
    String path = "kamar_ovan/";
    path += firebaseRelayKeys[i];

    if (Firebase.RTDB.getInt(&fbdo, path)) {

      int fbState = fbdo.intData();
      int stateToApply = fbState ? 1 : 0;

      if (stateToApply != relayState[i]) {
        setRelay(i, stateToApply, "Firebase");
      }
    }
  }
}

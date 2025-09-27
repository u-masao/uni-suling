#include <Arduino.h>
#include <BLEMidi.h>
#include <NimBLEDevice.h>
#include <math.h>

// --- BLE MIDI 基本設定 ---
const char* MIDI_SERVICE_UUID = "03b80e5a-ede8-4b33-a751-6ce34ec4c700";
const char* DEVICE_NAME = "ESP32-Uni-Suling";

// --- 物理ピン設定 ---
const int SENSOR_ANALOG_PIN = 36;
const int SENSOR_POWER_PIN = 13;
const int LED_PIN = 2;

// --- LED PWM設定 ---
const int LED_CHANNEL = 0;
const int LED_FREQ = 5000;
const int LED_RESOLUTION = 8;

// --- MIDI設定 ---
const int MIDI_CHANNEL = 1;
const int MIDI_NOTE = 69;
const int NOTE_ON_VELOCITY = 127;

// --- ブレスカーブ・閾値設定 ---
const int NOTE_ON_THRESHOLD = 40;
const int NOTE_OFF_THRESHOLD = 30;
const float AFTERTOUCH_CURVE = 1.0;
const int SENSOR_MAX_VALUE = 100;
const int AFTERTOUCH_MIN = 0;
const int AFTERTOUCH_MAX = 127;

// --- 応答性・安定性向上のための設定 ---
const unsigned long SEND_INTERVAL_US = 20000;
const unsigned long RETRIGGER_DELAY_US = 5000;

// --- 状態管理用グローバル変数 ---
bool isNoteOn = false;
int lastSentAftertouch = -1;
unsigned long lastSendTime = 0;
unsigned long lastNoteOffTime = 0;

// BLEサーバーのコールバック
class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* pServer) {
    Serial.println("Client connected");
  }
  void onDisconnect(NimBLEServer* pServer) {
    Serial.println("Client disconnected");
  }
};

void setup() {
  Serial.begin(115200);
  pinMode(SENSOR_POWER_PIN, OUTPUT);
  digitalWrite(SENSOR_POWER_PIN, HIGH);
  ledcSetup(LED_CHANNEL, LED_FREQ, LED_RESOLUTION);
  ledcAttachPin(LED_PIN, LED_CHANNEL);

  BLEDevice::init(DEVICE_NAME);
  NimBLEServer* pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());
  BLEDevice::setSecurityAuth(true, false, true);

  NimBLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(MIDI_SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMaxPreferred(0x12);

  BLEMidiServer.begin(DEVICE_NAME);
  pAdvertising->start();
  Serial.println("ESP32 Breath Controller is ready.");
}

// アフタータッチ値を計算するヘルパー関数
int calcAftertouch(int rawValue) {
  float normalized = (float)constrain(rawValue, NOTE_ON_THRESHOLD, SENSOR_MAX_VALUE) - NOTE_ON_THRESHOLD;
  normalized /= (float)(SENSOR_MAX_VALUE - NOTE_ON_THRESHOLD);
  float curved = pow(normalized, AFTERTOUCH_CURVE);
  return constrain((int)(curved * 127), AFTERTOUCH_MIN, AFTERTOUCH_MAX);
}

void loop() {
  if (BLEMidiServer.isConnected()) {
    // ★★★ 4回サンプリングして平均値を計算 ★★★
    long total = 0;
    for (int i = 0; i < 4; i++) {
      total += analogRead(SENSOR_ANALOG_PIN);
    }
    int rawValue = total / 4;

    // Note ON ロジック
    if (!isNoteOn && rawValue >= NOTE_ON_THRESHOLD && (micros() - lastNoteOffTime > RETRIGGER_DELAY_US)) {
      isNoteOn = true;
      BLEMidiServer.noteOn(MIDI_CHANNEL, MIDI_NOTE, NOTE_ON_VELOCITY);
      int initialAftertouch = calcAftertouch(rawValue);
      BLEMidiServer.afterTouch(MIDI_CHANNEL, initialAftertouch);
      lastSentAftertouch = initialAftertouch;
      lastSendTime = micros();
      Serial.printf("%d,%d,1\n", lastSendTime, rawValue);
    }

    // Note OFF ロジック
    else if (isNoteOn && rawValue <= NOTE_OFF_THRESHOLD) {
      isNoteOn = false;
      BLEMidiServer.noteOff(MIDI_CHANNEL, MIDI_NOTE, 0);
      lastNoteOffTime = micros();
      Serial.printf("%d,%d,0\n", lastNoteOffTime, rawValue);
    }

    // 演奏中のアフタータッチ送信ロジック
    if (isNoteOn) {
      if (micros() - lastSendTime >= SEND_INTERVAL_US) {
        int aftertouch = calcAftertouch(rawValue);
        if (aftertouch != lastSentAftertouch) {
          BLEMidiServer.afterTouch(MIDI_CHANNEL, aftertouch);
          lastSentAftertouch = aftertouch;
          lastSendTime = micros();
          Serial.printf("%d,%d,2\n", lastSendTime, rawValue);
        }
      }
    }

    // LED輝度制御
    int brightness = isNoteOn ? map(lastSentAftertouch, 0, 127, 0, 255) : 0;
    ledcWrite(LED_CHANNEL, constrain(brightness, 0, 255));

  } else {
    digitalWrite(LED_PIN, (millis() / 500) % 2);
  }
}

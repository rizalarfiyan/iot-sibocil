#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <DFRobotDFPlayerMini.h>
#include <ESP32Servo.h>
#include <PN532.h>
#include <PN532_HSU.h>
#include <PubSubClient.h>
#include <SPI.h>
#include <ShiftRegister74HC595.h>
#include <SoftwareSerial.h>
#include <WiFi.h>
#include <millisDelay.h>
#include <qrcode.h>

// Input PIN
#define IR_SENSOR_PIN 36
#define METAL_SENSOR_PIN 39
#define CANCEL_BUTTON_PIN 22

// Otput PIN
#define SERVO_PIN 13
#define DATA_PIN 25
#define LATCH_PIN 33
#define CLOCK_PIN 32

// Declare PIN TFT
#define TFT_CS 14
#define TFT_RST 2
#define TFT_DC 15

// Declare PIN Software Serial
#define SS_RX_PIN 4
#define SS_TX_PIN 5

// Declare Color
#define ST77XX_DARK_GRAY 0x4228

// Declare Default
#define DEFAULT_TEXT_SIZE 2.5

// Wifi
const char *ssid = "hub space";
const char *password = "Password123@";

// Revend
const char *token = "2ZdEzVzwZae0BkYAmILaXL7W05R";

// MQTT
const char *mqtt_broker = "192.168.254.137";
const char *mqtt_username = "";
const char *mqtt_password = "";
const int mqtt_port = 1883;

const String topicTrigger = "revend/trigger";
const String topicAction = "revend/action/" + String(token);

// declare the enum for the state
enum RevendStep {
  STEP_CANCEL = 1,
  STEP_AUTH = 2,
  STEP_REVEND = 3,
};

enum RevendState {
  STATE_NONE = 0,
  STATE_ALREADY_REGISTERED = 1,
  STATE_MUST_REGISTER = 2,
  STATE_SUCCESS_REGISTER = 3,
};

// Current Data
struct CurrentData {
  RevendStep Step;
  RevendState State;
  String Identity;
  int PointSuccess;
  int PointFailed;
  int countIsSuccess;
  int countIsFailed;
  bool IsSet;
  String Link;
};

CurrentData current;

struct ActionDataResponse {
  RevendState State;
  String Link;
};

struct ActionResponse {
  RevendStep Step;
  ActionDataResponse Data;
};

void welcomeMessage();
void readByStep();
void callbackAction(ActionResponse res);
void callbackMQTT(char *topic, byte *payload, unsigned int length);
void clearScreen();
void drawProgressBar();
void displayCenteredText(String text, uint8_t textSize);
void displayQRCode(String text);
String readRFIDAndNFC();
void sendTriggerCancelRequest();
void sendTriggerCheckUser();
void sendTriggerSendStatus();

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
ShiftRegister74HC595<1> sr(DATA_PIN, CLOCK_PIN, LATCH_PIN);
PN532_HSU pn532shu(Serial1);
PN532 nfc(pn532shu);
SoftwareSerial softwareSerial(SS_RX_PIN, SS_TX_PIN);
DFRobotDFPlayerMini dfPlayer;
Servo servo;

WiFiClient espClient;
PubSubClient client(espClient);

int dotIndex = 0;
bool isLoading = false;
String loadingText = "Loading...";
bool isOpenServo = false;

unsigned long previousMillis = 0;
const long interval = 100;

const unsigned long LOADING_DELAY = 100;
millisDelay loadingDelay;

const unsigned long WELCOME_DELAY = 3000;
millisDelay welcomeDelay;

const int WAITING_COUNTER_SENSOR_DELAY = 2;
const unsigned long SENSOR_DELAY = 250;
millisDelay sensorDelay;

const unsigned long BLINK_DELAY = 1000;
millisDelay blinkDelay;

const unsigned long SERVO_WAITING_DELAY = 400;
millisDelay servoDelay;

void setup() {
  Serial.begin(115200);
  Serial.print(F("Recycle Vending Machine"));
  tft.init(240, 240, SPI_MODE3);
  tft.setRotation(2);

  Serial.println(F("Initialized"));

  clearScreen();
  displayCenteredText("Booting...", DEFAULT_TEXT_SIZE);

  Serial.println(F("Configuring Pins..."));
  pinMode(CANCEL_BUTTON_PIN, INPUT);
  pinMode(IR_SENSOR_PIN, INPUT);
  pinMode(METAL_SENSOR_PIN, INPUT);
  servo.attach(SERVO_PIN);

  softwareSerial.begin(9600);
  Serial.println(F("Initializing DFPlayer ... (May take 3~5 seconds)"));
  if (!dfPlayer.begin(softwareSerial, false, true)) {
    Serial.println(F("Unable to begin:"));
    Serial.println(F("1.Please recheck the connection!"));
    Serial.println(F("2.Please insert the SD card!"));
    displayCenteredText("DF Player Error!", DEFAULT_TEXT_SIZE);
    while (true) {
      delay(0);
    }
  }

  Serial.println(F("DFPlayer Mini online."));
  dfPlayer.volume(8);
  dfPlayer.playMp3Folder(1);

  Serial1.begin(9600, SERIAL_8N1, 16, 17);
  nfc.begin();

  uint32_t versiondata = nfc.getFirmwareVersion();
  if (!versiondata) {
    Serial.print("Didn't find PN53x board");
    while (1) {
    };
  }

  Serial.print("Found chip PN5");
  Serial.println((versiondata >> 24) & 0xFF, HEX);
  Serial.print("Firmware ver. ");
  Serial.print((versiondata >> 16) & 0xFF, DEC);
  Serial.print('.');
  Serial.println((versiondata >> 8) & 0xFF, DEC);

  nfc.SAMConfig();

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  clearScreen();
  displayCenteredText("Connecting Wifi...", DEFAULT_TEXT_SIZE);
  Serial.print("Connecting to WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(1000);
  }

  Serial.println();
  Serial.println("WiFi connected");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  clearScreen();
  displayCenteredText(WiFi.localIP().toString(), DEFAULT_TEXT_SIZE);
  delay(500);

  clearScreen();
  displayCenteredText("Connecting MQTT...", DEFAULT_TEXT_SIZE);
  client.setServer(mqtt_broker, mqtt_port);
  client.setCallback(callbackMQTT);
  while (!client.connected()) {
    String client_id = "revend-";
    client_id += String(WiFi.macAddress());
    Serial.printf("Conecting to MQTT broker (%s)\n", client_id.c_str());
    if (!client.connect(client_id.c_str(), mqtt_username, mqtt_password)) {
      Serial.print("MQTT failed with state: ");
      Serial.println(client.state());
      delay(1000);
    }
  }

  Serial.println("MQTT connected");
  client.subscribe(topicAction.c_str());

  loadingDelay.start(LOADING_DELAY);
  // servoDelay.start(SERVO_DELAY);
  blinkDelay.start(BLINK_DELAY);
  welcomeDelay.start(WELCOME_DELAY);

  welcomeMessage();
}

void loop() {
  client.loop();

  if (isLoading) {
    drawProgressBar();
  }

  if (welcomeDelay.justFinished()) {
    welcomeDelay.repeat();
    welcomeMessage();
  }

  int cancelButton = digitalRead(CANCEL_BUTTON_PIN);
  if (cancelButton == HIGH && current.Step != STEP_CANCEL && current.Identity != "") {
    current.Step = STEP_CANCEL;
    clearScreen();
    displayCenteredText("Canceled", DEFAULT_TEXT_SIZE);
    Serial.println("Cancel button pressed");
    sendTriggerCancelRequest();
    current.Identity = "";
    current.PointSuccess = 0;
    current.PointFailed = 0;
    current.countIsSuccess = 0;
    current.countIsFailed = 0;
    sr.setAllLow();
    welcomeDelay.repeat();
  }

  readByStep();

  if (servoDelay.justFinished() || isOpenServo) {
    isOpenServo = false;
    static bool isBack = true;
    if (isBack) {
      servo.write(180);
      servoDelay.start(SERVO_WAITING_DELAY);
      isBack = false;
    } else {
      servo.write(0);
      servoDelay.stop();
      isBack = true;
    }
  }
}

void welcomeMessage() {
  Serial.println("Welcome Revend");
  clearScreen();
  displayCenteredText("Revend", DEFAULT_TEXT_SIZE);
  //? voice selamat datang
  //? voice silahkan tempelkan kartu anda
}

void readSensor() {
  Serial.print("Read sensor: ");
  int irSensor = analogRead(IR_SENSOR_PIN);
  int metalSensor = analogRead(METAL_SENSOR_PIN);
  Serial.print(irSensor);
  Serial.print(" - ");
  Serial.println(metalSensor);
  bool hasObject = irSensor < 1200;
  bool isMetal = metalSensor < 500;
  if (isMetal && hasObject) {
    Serial.println("success botol kaleng!");
    if (current.IsSet) return;
    if (current.countIsSuccess < WAITING_COUNTER_SENSOR_DELAY) {
      current.countIsSuccess++;
      return;
    }
    current.PointSuccess++;
    current.IsSet = true;
    sr.setAllLow();
    sr.set(1, HIGH);
    sendTriggerSendStatus();
    isOpenServo = true;
    return;
  }

  if (hasObject && !isMetal) {
    Serial.println("not kaleng!");
    if (current.IsSet) return;
    if (current.countIsFailed < WAITING_COUNTER_SENSOR_DELAY) {
      current.countIsFailed++;
      return;
    }
    current.PointFailed++;
    current.IsSet = true;
    sr.setAllLow();
    sr.set(0, HIGH);
    sendTriggerSendStatus();
    return;
  }

  current.countIsSuccess = 0;
  current.countIsFailed = 0;
  current.IsSet = false;
  Serial.println("not detected!");

  sr.setAllLow();
  sr.set(2, HIGH);
}

void readByStep() {
  switch (current.Step) {
    case STEP_AUTH:
      switch (current.State) {
        case STATE_ALREADY_REGISTERED:
          if (current.IsSet) return;
          Serial.println("Already Registered");
          //? voice selamat datang, selamat bergabung
          //? voice silahkan ambil sampah anda
          //? set state to pilih sampah
          clearScreen();
          current.Step = STEP_REVEND;
          sensorDelay.start(SENSOR_DELAY);
          break;
        case STATE_MUST_REGISTER:
          if (current.IsSet) return;
          Serial.println("Must Register");
          clearScreen();
          displayQRCode(current.Link);
          break;
        case STATE_SUCCESS_REGISTER:
          if (current.IsSet) return;
          Serial.println("Success Register");
          //? voice selamat datang, selamat bergabung
          //? voice silahkan ambil sampah anda
          //? set state to pilih sampah
          clearScreen();
          current.Step = STEP_REVEND;
          sensorDelay.start(SENSOR_DELAY);
          break;
      }
      current.IsSet = true;
      break;
    case STEP_REVEND:
      if (sensorDelay.justFinished()) {
        sensorDelay.repeat();
        readSensor();
      }
      break;

    default:
      String tagId = readRFIDAndNFC();
      if (tagId != "" && current.Identity != tagId) {
        current.Identity = tagId;
        sendTriggerCheckUser();
      }
      break;
  }
}

void sendTriggerCancelRequest() {
  DynamicJsonDocument doc(150);
  doc["step"] = STEP_CANCEL;
  doc["data"]["device_id"] = String(token);
  doc["data"]["identity"] = current.Identity;
  String res = "";
  serializeJson(doc, res);
  client.publish(topicTrigger.c_str(), res.c_str());
  Serial.println("Send Trigger Cancel Request");
}

void sendTriggerCheckUser() {
  DynamicJsonDocument doc(150);
  doc["step"] = STEP_AUTH;
  doc["data"]["device_id"] = String(token);
  doc["data"]["identity"] = current.Identity;
  String res = "";
  serializeJson(doc, res);
  client.publish(topicTrigger.c_str(), res.c_str());
  Serial.println("Send Trigger Check User");
}

void sendTriggerSendStatus() {
  DynamicJsonDocument doc(256);
  doc["step"] = STEP_REVEND;
  doc["data"]["device_id"] = String(token);
  doc["data"]["identity"] = current.Identity;
  doc["data"]["failed"] = current.PointFailed;
  doc["data"]["success"] = current.PointSuccess;
  String res = "";
  serializeJson(doc, res);
  client.publish(topicTrigger.c_str(), res.c_str());
  Serial.println("Send Trigger Send Status");
}

void callbackMQTT(char *topic, byte *payload, unsigned int length) {
  if (String(topic) == topicAction) {
    String strRes = "";
    for (int i = 0; i < length; i++) {
      strRes += (char)payload[i];
    }
    DynamicJsonDocument doc(256);
    deserializeJson(doc, strRes);
    ActionResponse res;
    res.Step = doc["step"];
    res.Data.State = doc["data"]["state"];
    res.Data.Link = doc["data"]["link"].as<String>();
    Serial.println(doc["data"]["link"].as<String>());
    callbackAction(res);
    Serial.println(strRes);
    Serial.println("-----------------------");
  }
}

void callbackAction(ActionResponse res) {
  current.IsSet = false;
  current.Link = "";
  current.PointSuccess = 0;
  current.PointFailed = 0;
  welcomeDelay.stop();
  switch (res.Step) {
    case STEP_CANCEL:
      current.Step = STEP_CANCEL;
      current.State = STATE_NONE;
      current.Identity = "";
      break;
    case STEP_AUTH:
      current.Step = STEP_AUTH;
      current.State = res.Data.State;
      if (res.Data.State == STATE_MUST_REGISTER) {
        current.Link = res.Data.Link;
      }
      break;
    default:
      break;
  }
}

void drawProgressBar() {
  displayCenteredText(loadingText, DEFAULT_TEXT_SIZE);

  if (loadingDelay.justFinished()) {
    loadingDelay.repeat();

    const int numDots = 10;
    const int dotSpacing = 12;
    const int dotSize = 5;
    int totalWidth = numDots * (dotSize + dotSpacing) - dotSpacing;
    int startX = (240 - totalWidth) / 2;
    for (int i = 0; i < numDots; ++i) {
      int x = startX + i * (dotSize + dotSpacing);
      int y = tft.height() - 20;
      if (i == dotIndex) {
        tft.fillCircle(x, y, dotSize, ST77XX_WHITE);
      } else {
        tft.fillCircle(x, y, dotSize, ST77XX_DARK_GRAY);
      }
    }
    dotIndex = (dotIndex + 1) % numDots;
  }
}

void clearScreen() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
}

void displayCenteredText(String text, uint8_t textSize) {
  tft.setTextSize(textSize);
  tft.setTextWrap(false);
  int16_t x, y;
  uint16_t textWidth, textHeight;
  tft.getTextBounds(text, 0, 0, &x, &y, &textWidth, &textHeight);
  int16_t xPos = (tft.width() - textWidth) / 2;
  int16_t yPos = (tft.height() - textHeight) / 2;
  tft.setCursor(xPos, yPos);
  tft.print(text);
}

void displayQRCode(String text) {
  QRCode qrcode;
  uint8_t qrcodeData[qrcode_getBufferSize(12)];
  qrcode_initText(&qrcode, qrcodeData, 12, 0, text.c_str());

  float scaleFactor = 3;
  int displaySize = qrcode.size * scaleFactor;
  int xPos = (tft.width() - displaySize) / 2;
  int yPos = (tft.height() - displaySize) / 2;

  tft.fillScreen(ST77XX_WHITE);
  for (uint8_t y = 0; y < qrcode.size; y++) {
    for (uint8_t x = 0; x < qrcode.size; x++) {
      if (qrcode_getModule(&qrcode, x, y)) {
        tft.fillRect(xPos + x * scaleFactor, yPos + y * scaleFactor, scaleFactor, scaleFactor, ST77XX_BLACK);
      }
    }
  }
}

String readRFIDAndNFC() {
  uint8_t uid[] = {0, 0, 0, 0, 0, 0, 0};
  uint8_t uidLength;
  bool success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, &uid[0], &uidLength);
  if (success) {
    String tagId = "";
    for (uint8_t i = 0; i < uidLength; i++) {
      if (i > 0) {
        tagId += ".";
      }
      tagId += String(uid[i]);
    }

    Serial.print(uidLength, DEC);
    Serial.print(" bytes | ");
    Serial.println(tagId);
    return tagId;
  }

  return "";
}

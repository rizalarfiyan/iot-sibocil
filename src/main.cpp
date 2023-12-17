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

unsigned long previousMillis = 0;
const long interval = 100;

const unsigned long LOADING_DELAY = 100;
millisDelay loadingDelay;

const unsigned long BLINK_DELAY = 1000;
millisDelay blinkDelay;

const unsigned long SERVO_DELAY = 3000;
const unsigned long SERVO_WAITING_DELAY = 400;
millisDelay servoDelay;

void callback(char *topic, byte *payload, unsigned int length);
void clearScreen();
void drawProgressBar();
void displayCenteredText(String text, uint8_t textSize);
void displayQRCode(String text);
void readRFIDAndNFC();

int pos = 0;

struct ActionDataResponse {
  int State;
  String Link;
};

struct ActionResponse {
  int Step;
  ActionDataResponse Data;
};

void setup() {
  Serial.begin(115200);
  Serial.print(F("Hello World!"));
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

  client.setServer(mqtt_broker, mqtt_port);
  client.setCallback(callback);
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

  client.subscribe(topicAction.c_str());

  loadingDelay.start(LOADING_DELAY);
  // servoDelay.start(SERVO_DELAY);
  blinkDelay.start(BLINK_DELAY);
}

void loop() {
  client.loop();

  if (isLoading) {
    drawProgressBar();
  }

  if (digitalRead(CANCEL_BUTTON_PIN) == HIGH) {
    isLoading = true;
  } else {
    isLoading = false;
    dotIndex = 0;
    tft.fillScreen(ST77XX_BLACK);
  }

  // // RED => 0
  // // GREEN => 1
  // // BLUE => 2
  // if (blinkDelay.justFinished()) {
  //   blinkDelay.repeat();
  //   static int state = 0;
  //   sr.setAllLow();
  //   sr.set(state, HIGH);
  //   Serial.println(state);
  //   state++;
  //   if (state >= 3) {
  //     state = 0;
  //   }
  // }

  // displayQRCode("https://www.google.com");

  // Serial.println(analogRead(IR_SENSOR_PIN));
  // Serial.println(analogRead(METAL_SENSOR_PIN));
  // readRFIDAndNFC();

  // if (servoDelay.justFinished()) {
  //   static bool isBack = true;
  //   if (isBack) {
  //     servo.write(180);
  //     servoDelay.start(SERVO_WAITING_DELAY);
  //     isBack = false;
  //   } else {
  //     servo.write(0);
  //     servoDelay.stop();
  //     isBack = true;
  //   }
  // }
}

void callback(char *topic, byte *payload, unsigned int length) {
  if (String(topic) == topicAction) {
    String strRes = "";
    for (int i = 0; i < length; i++) {
      strRes += (char)payload[i];
    }
    DynamicJsonDocument doc(150);
    deserializeJson(doc, strRes);
    ActionResponse res;
    res.Step = doc["step"];
    res.Data.State = doc["data"]["state"];
    res.Data.Link = doc["data"]["link"].as<String>();
    Serial.println(strRes);
    Serial.println("-----------------------");
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
  uint8_t qrcodeData[qrcode_getBufferSize(3)];
  qrcode_initText(&qrcode, qrcodeData, 3, 0, text.c_str());

  int scaleFactor = 6;
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

void readRFIDAndNFC() {
  uint8_t uid[] = {0, 0, 0, 0, 0, 0, 0};
  uint8_t uidLength;
  bool success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, &uid[0], &uidLength);
  if (success) {
    Serial.print(uidLength, DEC);
    Serial.print(" bytes |");

    String tagId = "";
    for (uint8_t i = 0; i < uidLength; i++) {
      if (i > 0) {
        tagId += ".";
      }
      tagId += String(uid[i]);
    }

    Serial.print(" | ");
    Serial.print(tagId);
    Serial.println("");
  }
}

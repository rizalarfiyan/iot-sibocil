#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Arduino.h>
#include <DFRobotDFPlayerMini.h>
#include <SPI.h>
#include <SoftwareSerial.h>
#include <qrcode.h>

// Declare PIN TFT
#define TFT_CS 14
#define TFT_RST 2
#define TFT_DC 15

// Declare PIN Software Serial
#define SS_RX_PIN 4
#define SS_TX_PIN 5

// Sensor PIN
#define IR_SENSOR_PIN 36
#define METAL_SENSOR_PIN 39

// Declare Color
#define ST77XX_DARK_GRAY 0x4228

// Declare Default
#define DEFAULT_TEXT_SIZE 2.5

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
SoftwareSerial softwareSerial(SS_RX_PIN, SS_TX_PIN);
DFRobotDFPlayerMini dfPlayer;

int dotIndex = 0;
bool isLoading = false;
String loadingText = "Loading...";

unsigned long previousMillis = 0;
const long interval = 100;

const int buttonPin = 13;

void drawProgressBar();
void displayCenteredText(String text, uint8_t textSize);
void displayQRCode(String text);

void setup(void) {
  Serial.begin(115200);
  Serial.print(F("Hello World!"));
  tft.init(240, 240, SPI_MODE3);
  tft.setRotation(2);

  pinMode(buttonPin, INPUT);
  pinMode(IR_SENSOR_PIN, INPUT);
  pinMode(METAL_SENSOR_PIN, INPUT);

  Serial.println(F("Initialized"));

  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  displayCenteredText("Booting...", DEFAULT_TEXT_SIZE);

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

  delay(1000);
  displayQRCode("https://www.google.com");
  delay(5000);
}

void loop() {
  if (isLoading) {
    drawProgressBar();
  }

  if (digitalRead(buttonPin) == HIGH) {
    isLoading = true;
  } else {
    isLoading = false;
    dotIndex = 0;
    tft.fillScreen(ST77XX_BLACK);
  }

  Serial.println(analogRead(IR_SENSOR_PIN));
  Serial.println(analogRead(METAL_SENSOR_PIN));
}

void drawProgressBar() {
  displayCenteredText(loadingText, DEFAULT_TEXT_SIZE);

  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;

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

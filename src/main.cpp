#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <qrcode.h>
#include <DFRobotDFPlayerMini.h>

// Declare PIN
#define TFT_CS 14
#define TFT_RST 2
#define TFT_DC 15
#define TFT_MOSI 22
#define TFT_SCLK 18

// Declare Color
#define ST77XX_DARK_GRAY 0x4228

// Declare Default
#define DEFAULT_TEXT_SIZE 2.5

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
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
void printDetail(uint8_t type, int value);

void setup(void) {
  Serial.begin(115200);
  Serial.print(F("Hello World!"));
  tft.init(240, 240, SPI_MODE3);
  tft.setRotation(2);

  pinMode(buttonPin, INPUT);

  Serial.println(F("Initialized"));

  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  displayCenteredText("Booting...", DEFAULT_TEXT_SIZE);

  Serial1.begin(9600, SERIAL_8N1, 16, 17);
  Serial.println(F("Initializing DFPlayer ... (May take 3~5 seconds)"));

  if (!dfPlayer.begin(Serial1, false, true)) {  // Use serial to communicate with mp3.
    Serial.println(F("Unable to begin:"));
    Serial.println(F("1.Please recheck the connection!"));
    Serial.println(F("2.Please insert the SD card!"));
    displayCenteredText("DF Player Error!", DEFAULT_TEXT_SIZE);
    while (true) {
      delay(0);  // Code to compatible with ESP8266 watch dog.
    }
  }

  Serial.println(F("DFPlayer Mini online."));
  dfPlayer.volume(15);
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

void printDetail(uint8_t type, int value) {
  switch (type) {
    case TimeOut:
      Serial.println(F("Time Out!"));
      break;
    case WrongStack:
      Serial.println(F("Stack Wrong!"));
      break;
    case DFPlayerCardInserted:
      Serial.println(F("Card Inserted!"));
      break;
    case DFPlayerCardRemoved:
      Serial.println(F("Card Removed!"));
      break;
    case DFPlayerCardOnline:
      Serial.println(F("Card Online!"));
      break;
    case DFPlayerUSBInserted:
      Serial.println("USB Inserted!");
      break;
    case DFPlayerUSBRemoved:
      Serial.println("USB Removed!");
      break;
    case DFPlayerPlayFinished:
      Serial.print(F("Number:"));
      Serial.print(value);
      Serial.println(F(" Play Finished!"));
      break;
    case DFPlayerError:
      displayCenteredText("DF Player Error!", DEFAULT_TEXT_SIZE);
      Serial.print(F("DFPlayerError:"));
      switch (value) {
        case Busy:
          Serial.println(F("Card not found"));
          break;
        case Sleeping:
          Serial.println(F("Sleeping"));
          break;
        case SerialWrongStack:
          Serial.println(F("Get Wrong Stack"));
          break;
        case CheckSumNotMatch:
          Serial.println(F("Check Sum Not Match"));
          break;
        case FileIndexOut:
          Serial.println(F("File Index Out of Bound"));
          break;
        case FileMismatch:
          Serial.println(F("Cannot Find File"));
          break;
        case Advertise:
          Serial.println(F("In Advertise"));
          break;
        default:
          break;
      }
      break;
    default:
      break;
  }
}
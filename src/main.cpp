#include "esp_camera.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <Wire.h>

#include "config.hpp"

#define OLED_SDA 13
#define OLED_SCL 14
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define CAM_PIN_PWDN 32
#define CAM_PIN_RESET -1
#define CAM_PIN_XCLK 0
#define CAM_PIN_SIOD 26
#define CAM_PIN_SIOC 27

#define CAM_PIN_D7 35
#define CAM_PIN_D6 34
#define CAM_PIN_D5 39
#define CAM_PIN_D4 36
#define CAM_PIN_D3 21
#define CAM_PIN_D2 19
#define CAM_PIN_D1 18
#define CAM_PIN_D0 5
#define CAM_PIN_VSYNC 25
#define CAM_PIN_HREF 23
#define CAM_PIN_PCLK 22

const int LED_PIN = 4;

unsigned long imageCount = 0;
String slotStatus[3] = {"?", "?", "?"};
bool flashEnabled = false;

void initWifi() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 20);
  display.println("Conectando WiFi");
  display.setCursor(0, 35);
  display.println(WIFI_SSID);
  display.display();

  Serial.print("Connecting to ");
  Serial.println(WIFI_SSID);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int dots = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");

    display.fillRect(0, 50, SCREEN_WIDTH, 14, SSD1306_BLACK);
    display.setCursor(0, 50);
    for (int i = 0; i < (dots % 4); i++) {
      display.print(".");
    }
    display.display();
    dots++;
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 20);
  display.println("WiFi Conectado!");
  display.setCursor(0, 35);
  display.println(WiFi.localIP());
  display.display();
  delay(2000);

  Serial.println("WiFi connected");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
}

bool initCamera() {
  camera_config_t config = {
      .pin_pwdn = CAM_PIN_PWDN,
      .pin_reset = CAM_PIN_RESET,
      .pin_xclk = CAM_PIN_XCLK,
      .pin_sccb_sda = CAM_PIN_SIOD,
      .pin_sccb_scl = CAM_PIN_SIOC,

      .pin_d7 = CAM_PIN_D7,
      .pin_d6 = CAM_PIN_D6,
      .pin_d5 = CAM_PIN_D5,
      .pin_d4 = CAM_PIN_D4,
      .pin_d3 = CAM_PIN_D3,
      .pin_d2 = CAM_PIN_D2,
      .pin_d1 = CAM_PIN_D1,
      .pin_d0 = CAM_PIN_D0,
      .pin_vsync = CAM_PIN_VSYNC,
      .pin_href = CAM_PIN_HREF,
      .pin_pclk = CAM_PIN_PCLK,

      .xclk_freq_hz = 20000000,
      .ledc_timer = LEDC_TIMER_0,
      .ledc_channel = LEDC_CHANNEL_0,

      .pixel_format = PIXFORMAT_JPEG,
      .frame_size = FRAMESIZE_QVGA,

      .jpeg_quality = 12,
      .fb_count = 1,
      .fb_location = CAMERA_FB_IN_PSRAM,
      .grab_mode = CAMERA_GRAB_LATEST,
  };

  return esp_camera_init(&config) == ESP_OK;
}

void updateOLED() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(20, 10);
  display.print("1 2 3");

  display.setCursor(20, 35);
  display.print(slotStatus[0] + " " + slotStatus[1] + " " + slotStatus[2]);

  display.display();
}

void sendImage() {
  Serial.println("Free heap before capture: " + String(ESP.getFreeHeap()));

  camera_fb_t *fb = esp_camera_fb_get();

  if (!fb) {
    Serial.println("Camera capture failed");
    return;
  }

  Serial.printf("Image captured: %d bytes\n", fb->len);

  HTTPClient http;
  http.setTimeout(30000);

  if (http.begin(BACKEND_URL)) {
    http.addHeader("Content-Type", "image/jpeg");
    http.addHeader("Content-Length", String(fb->len));

    Serial.println("Sending image to backend...");

    int httpCode = http.POST(fb->buf, fb->len);
    esp_camera_fb_return(fb);

    if (httpCode > 0) {
      imageCount++;
      Serial.printf("Response code: %d\n", httpCode);

      String response = http.getString();
      Serial.println("Response: " + response);

      if (httpCode == 200) {
        JsonDocument responseDoc;
        DeserializationError error = deserializeJson(responseDoc, response);

        if (!error) {
          JsonArray slots = responseDoc["slots"].as<JsonArray>();

          if (slots.size() >= 3) {
            slotStatus[0] = slots[0].as<String>();
            slotStatus[1] = slots[1].as<String>();
            slotStatus[2] = slots[2].as<String>();

            updateOLED();
          }

          // Controlar flash según estado del backend
          if (responseDoc.containsKey("flash")) {
            bool flashState = responseDoc["flash"].as<bool>();
            digitalWrite(LED_PIN, flashState ? HIGH : LOW);
            flashEnabled = flashState;
            Serial.printf("Flash: %s\n", flashState ? "ON" : "OFF");
          }
        } else {
          Serial.println("JSON parse error");
          updateOLED();
        }
      } else {
        updateOLED();
      }
    } else {
      Serial.printf("HTTP POST failed: %s\n",
                    http.errorToString(httpCode).c_str());
      updateOLED();
    }
    http.end();
  } else {
    Serial.println("Failed to connect to backend");
    updateOLED();
  }
}

void initOLED() {
  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED init failed");
    return;
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(10, 25);
  display.println("Inicializando...");
  display.display();
  delay(1000);
}

void setup() {
  Serial.begin(115200);

  initOLED();

  if (!initCamera()) {
    Serial.println("Camera init failed");
    return;
  }

  initWifi();
  updateOLED();

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected, reconnecting...");
    WiFi.reconnect();
    delay(5000);
    return;
  }

  sendImage();
}

#include "esp_camera.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <ArduinoIoTCloud.h>
#include <ArduinoJson.h>
#include <Arduino_ConnectionHandler.h>
#include <HTTPClient.h>
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
String slotLabels[3] = {"E1", "E2", "E3"};
String slotStatus[3] = {"?", "?", "?"};
bool wifiConnected = false;

String cloudSlot1, cloudSlot2, cloudSlot3;

WiFiConnectionHandler ArduinoIoTPreferredConnection(WIFI_SSID, WIFI_PASSWORD);

void showConnecting() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 20);
  display.println("Conectando WiFi...");
  display.setCursor(0, 35);
  display.println(WIFI_SSID);
  display.display();
}

void onNetworkConnect() {
  wifiConnected = true;
  Serial.println("WiFi connected");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
}

void onNetworkDisconnect() {
  wifiConnected = false;
  showConnecting();
  Serial.println("WiFi disconnected");
}

void initArduinoCloud() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 20);
  display.println("Conectando WiFi");
  display.setCursor(0, 35);
  display.println(WIFI_SSID);
  display.display();

  ArduinoCloud.setBoardId(DEVICE_ID);
  ArduinoCloud.setSecretDeviceKey(DEVICE_KEY);

  ArduinoCloud.addProperty(cloudSlot1, Permission::Read);
  ArduinoCloud.addProperty(cloudSlot2, Permission::Read);
  ArduinoCloud.addProperty(cloudSlot3, Permission::Read);

  ArduinoCloud.begin(ArduinoIoTPreferredConnection);

  ArduinoIoTPreferredConnection.addCallback(NetworkConnectionEvent::CONNECTED,
                                            onNetworkConnect);
  ArduinoIoTPreferredConnection.addCallback(
      NetworkConnectionEvent::DISCONNECTED, onNetworkDisconnect);
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

      .jpeg_quality = 20,
      .fb_count = 1,
      .fb_location = CAMERA_FB_IN_PSRAM,
      .grab_mode = CAMERA_GRAB_LATEST,
  };

  return esp_camera_init(&config) == ESP_OK;
}

void updateOLED() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(2);

  display.setCursor(20, 10);
  display.print("1 2 3");

  display.setCursor(20, 35);
  for (int i = 0; i < 3; i++) {
    if (i > 0)
      display.print(" ");
    if (slotStatus[i] == "free") {
      display.print("L");
    } else if (slotStatus[i] == "occupied") {
      display.print("O");
    } else if (slotStatus[i] == "reserved") {
      display.print("R");
    } else {
      display.print("?");
    }
  }

  display.display();
}

void sendImage() {
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

    int httpCode = http.POST(fb->buf, fb->len);
    esp_camera_fb_return(fb);

    if (httpCode == 200) {
      imageCount++;
      String response = http.getString();
      Serial.println("Response: " + response);

      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, response);

      if (!error) {
        JsonArray spaces = doc["spaces"].as<JsonArray>();
        Serial.printf("Parsed %d spaces\n", spaces.size());
        for (JsonObject space : spaces) {
          String label = space["label"].as<String>();
          String status = space["status"].as<String>();
          Serial.printf("  %s: %s\n", label.c_str(), status.c_str());

          for (int i = 0; i < 3; i++) {
            if (slotLabels[i] == label) {
              slotStatus[i] = status;
              break;
            }
          }
        }
      } else {
        Serial.printf("JSON parse error: %s\n", error.c_str());
      }
    } else if (httpCode > 0) {
      Serial.printf("Predict response: %d\n", httpCode);
    } else {
      Serial.printf("HTTP POST failed: %s\n",
                    http.errorToString(httpCode).c_str());
    }
    http.end();
  } else {
    esp_camera_fb_return(fb);
    Serial.println("Failed to connect to backend");
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

  initArduinoCloud();

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
}

void loop() {
  ArduinoCloud.update();

  if (!wifiConnected) {
    delay(500);
    return;
  }

  sendImage();

  cloudSlot1 = slotStatus[0];
  cloudSlot2 = slotStatus[1];
  cloudSlot3 = slotStatus[2];

  updateOLED();
}

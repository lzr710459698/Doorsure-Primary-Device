#include "esp_camera.h"
#include <WiFi.h>
#include <EEPROM.h>
#include <WebServer.h>
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

#define EEPROM_SIZE 100
#define RESET_BUTTON 0

// Replace with your WiFi credentials (used only on first boot or after reset)
String ssid, password;
const char* apSSID = "ESP32-CAM_Setup";
const char* apPassword = "12345678";

// Firebase config
#define API_KEY "AIzaSyCjCXahhzsjSH1dmB5F0GGgw6TNTjpwudg"
#define DATABASE_URL "https://esp32-object-detection-d863a-default-rtdb.firebaseio.com"
#define STORAGE_BUCKET_ID "esp32-object-detection-d863a.firebasestorage.app"

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

WebServer server(80);

// CAMERA PINS (ESP32-S3 XIAO style — update if needed)
#define PWDN_GPIO_NUM  -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM  10
#define SIOD_GPIO_NUM  40
#define SIOC_GPIO_NUM  39
#define Y9_GPIO_NUM    48
#define Y8_GPIO_NUM    11
#define Y7_GPIO_NUM    12
#define Y6_GPIO_NUM    14
#define Y5_GPIO_NUM    16
#define Y4_GPIO_NUM    18
#define Y3_GPIO_NUM    17
#define Y2_GPIO_NUM    15
#define VSYNC_GPIO_NUM 38
#define HREF_GPIO_NUM  47
#define PCLK_GPIO_NUM  13

void resetEEPROM() {
  for (int i = 0; i < EEPROM_SIZE; i++) EEPROM.write(i, 0);
  EEPROM.commit();
  ESP.restart();
}

void saveWiFiCredentials(String ssid, String password) {
  for (int i = 0; i < ssid.length(); i++) EEPROM.write(i, ssid[i]);
  EEPROM.write(ssid.length(), '\0');
  for (int i = 0; i < password.length(); i++) EEPROM.write(50 + i, password[i]);
  EEPROM.write(50 + password.length(), '\0');
  EEPROM.commit();
}

void getWiFiCredentials(String &ssid, String &password) {
  char ssidBuffer[50] = {0}, passwordBuffer[50] = {0};
  for (int i = 0; i < 50; i++) {
    ssidBuffer[i] = EEPROM.read(i);
    if (ssidBuffer[i] == '\0') break;
  }
  for (int i = 0; i < 50; i++) {
    passwordBuffer[i] = EEPROM.read(50 + i);
    if (passwordBuffer[i] == '\0') break;
  }
  ssid = String(ssidBuffer);
  password = String(passwordBuffer);
}

void startAPMode() {
  WiFi.softAP(apSSID, apPassword);
  server.on("/", []() {
    server.send(200, "text/html",
      "<form action='/setwifi' method='POST'>"
      "SSID: <input name='ssid'><br>"
      "Password: <input name='password'><br>"
      "<input type='submit'></form>");
  });
  server.on("/setwifi", HTTP_POST, []() {
    if (server.hasArg("ssid") && server.hasArg("password")) {
      ssid = server.arg("ssid");
      password = server.arg("password");
      saveWiFiCredentials(ssid, password);
      server.send(200, "text/html", "Wi-Fi Saved! Rebooting...");
      delay(2000);
      ESP.restart();
    } else {
      server.send(400, "text/plain", "Missing SSID or Password");
    }
  });
  server.begin();
}

void startCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0       = Y2_GPIO_NUM;
  config.pin_d1       = Y3_GPIO_NUM;
  config.pin_d2       = Y4_GPIO_NUM;
  config.pin_d3       = Y5_GPIO_NUM;
  config.pin_d4       = Y6_GPIO_NUM;
  config.pin_d5       = Y7_GPIO_NUM;
  config.pin_d6       = Y8_GPIO_NUM;
  config.pin_d7       = Y9_GPIO_NUM;
  config.pin_xclk     = XCLK_GPIO_NUM;
  config.pin_pclk     = PCLK_GPIO_NUM;
  config.pin_vsync    = VSYNC_GPIO_NUM;
  config.pin_href     = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn     = PWDN_GPIO_NUM;
  config.pin_reset    = RESET_GPIO_NUM;
  config.xclk_freq_hz = 22000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size   = FRAMESIZE_SXGA;
  config.jpeg_quality = 10;
  config.fb_count     = 2;
  config.grab_mode = CAMERA_GRAB_LATEST;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("\u274C Camera init failed with error 0x%x\n", err);
    while (true); // Halt
  }
}

void flushCameraBuffer() {
  for (int i = 0; i < 2; i++) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb) esp_camera_fb_return(fb);
    //delay(30);
  }
}

void handleStream() {
  WiFiClient client = server.client();
  client.setNoDelay(true);
  String response = "HTTP/1.1 200 OK\r\n";
  response += "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n\r\n";
  server.sendContent(response);

  while (1) {
    if (!client.connected()) break;

    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) continue;

    response = "--frame\r\n";
    response += "Content-Type: image/jpeg\r\n\r\n";
    server.sendContent(response);
    client.write(fb->buf, fb->len);
    client.flush();
    server.sendContent("\r\n");

    esp_camera_fb_return(fb);
    //delay(30); // ~30 fps
  }
}



void handleRoot() {
  server.send(200, "text/html", R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0" />
  <title>DoorSure ESP32-CAM</title>
  <style>
    body {
      font-family: Arial, sans-serif;
      margin: 0;
      padding: 0;
      background-color: #f4f4f4;
      text-align: center;
    }
    header {
      display: flex;
      justify-content: space-between;
      align-items: center;
      padding: 10px 20px;
      background-color: #fff;
      box-shadow: 0 2px 4px rgba(0,0,0,0.1);
    }
    .logo {
      height: 50px;
    }
    .button {
      padding: 10px 20px;
      border: none;
      border-radius: 20px;
      background: linear-gradient(to right, #d16ba5, #86a8e7);
      color: white;
      font-weight: bold;
      cursor: pointer;
    }
    h2 {
      margin-top: 20px;
      color: #333;
      font-size: 24px;
    }
    #live {
      width: 90%;
      max-width: 600px;
      margin: 10px auto;
      background: #ccc;
      border-radius: 10px;
      overflow: hidden;
    }
  </style>
</head>
<body>
  <header>
    <a href="https://doorsure-sd25.web.app/" class="button">Return</a>
    <img class="logo" src="https://i.ibb.co/jZfwn188/logo.png" alt="DoorSure Logo" />
  </header>

  <h2>📸 Live Feed</h2>
  <div id="live">
    <img src="/stream" width="100%" />
  </div>
</body>
</html>
  )rawliteral");
}



void setup() {
  Serial.begin(115200);
  EEPROM.begin(EEPROM_SIZE);
  pinMode(RESET_BUTTON, INPUT_PULLUP);

  if (digitalRead(RESET_BUTTON) == LOW) {
    resetEEPROM();
    startAPMode();
    return;
  }

  getWiFiCredentials(ssid, password);
  if (ssid.length() > 0) {
    WiFi.begin(ssid.c_str(), password.c_str());
  } else {
    startAPMode();
    return;
  }

  Serial.print("Connecting to Wi-Fi");
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries++ < 30) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ Failed to connect to Wi-Fi, switching to AP mode...");
    startAPMode();
    Serial.print("📶 Connect to AP and go to http://");
    Serial.println(WiFi.softAPIP());
    return;
  }

  Serial.println("\n✅ Wi-Fi connected.");
  Serial.print("📸 Stream: http://");
  Serial.println(WiFi.localIP());

  Serial.printf("📟 Heap: %u bytes\n", ESP.getFreeHeap());
  Serial.printf("📟 PSRAM: %u bytes free of %u bytes\n", ESP.getFreePsram(), ESP.getPsramSize());


  startCamera();

  // Firebase setup
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  auth.user.email = "lzr710459698@gmail.com";
  auth.user.password = "Woaini1314@";
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  Serial.println("✅ Firebase ready.");

  server.on("/", handleRoot);
  server.on("/stream", handleStream);
  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  server.handleClient();

  static unsigned long lastCheck = 0;
  if (Firebase.ready() && millis() - lastCheck > 3000) {
    lastCheck = millis();

    if (Firebase.RTDB.getInt(&fbdo, "/trigger/takePhoto")) {
      int trigger = fbdo.intData();
      if (trigger == 1) {
        Serial.println("📸 Trigger received! Capturing image...");
        flushCameraBuffer();

        camera_fb_t* fb = esp_camera_fb_get();
        if (fb) {
          String path = "/images/latest.jpg";
          if (Firebase.Storage.upload(&fbdo, STORAGE_BUCKET_ID, fb->buf, fb->len, path, "image/jpeg")) {
            Serial.println("✅ Image uploaded to Firebase Storage!");
            String url = fbdo.downloadURL();
            Firebase.RTDB.setString(&fbdo, "/images/latest/url", url);
          } else {
            Serial.println("❌ Upload failed: " + fbdo.errorReason());
          }
          esp_camera_fb_return(fb);
        }
        Firebase.RTDB.setInt(&fbdo, "/trigger/takePhoto", 0);
      }
    }
  }
}

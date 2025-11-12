#include "esp_camera.h"
#include <WiFi.h>
#include <EEPROM.h>
#include <WebServer.h>
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"
#include <ESPmDNS.h>

// EEPROM + Wi-Fi
#define EEPROM_SIZE 100
#define RESET_BUTTON 0
#define WIFI_SSID        "Felix"
#define WIFI_PASSWORD    "123456789"

// Firebase
#define API_KEY "AIzaSyCjCXahhzsjSH1dmB5F0GGgw6TNTjpwudg"
#define DATABASE_URL "https://esp32-object-detection-d863a-default-rtdb.firebaseio.com"
#define STORAGE_BUCKET_ID "esp32-object-detection-d863a.firebasestorage.app"

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// Pins
#define trigPin 41
#define echoPin 42
#define REED_PIN 45

// Camera (ESP32-S3 XIAO style)
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

WebServer server(80);

// ---------- Shared ISR Variables ----------
volatile uint32_t echoStart = 0;
volatile uint32_t echoEnd = 0;
volatile bool echoDone = false;

volatile uint8_t doorStatusISR = 0;
volatile bool doorChanged = false;

float distance_cm = 0;
float distance_inch = 0;
uint8_t doorStatus = 0;

float maxDistanceInchInWindow = 0;
uint32_t lastRTDBpush = 0;


// ---------- ISRs ----------
void IRAM_ATTR reedISR() {
  doorStatusISR = (digitalRead(REED_PIN) == LOW) ? 0 : 1;
  doorChanged = true;
}

void IRAM_ATTR echoISR() {
  if (digitalRead(echoPin) == HIGH)
    echoStart = micros();
  else {
    echoEnd = micros();
    echoDone = true;
  }
}

// ---------- Camera ----------
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
  config.frame_size   = FRAMESIZE_VGA;
  config.jpeg_quality = 30;
  config.fb_count     = 2;
  config.grab_mode = CAMERA_GRAB_LATEST;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("❌ Camera init failed with error 0x%x\n", err);
    while (true);
  }
}

// ---------- Streaming ----------
void streamImagesLoop() {
  static unsigned long lastFrameTime = 0;
  if (Firebase.ready() && millis() - lastFrameTime >= 1000) {
    lastFrameTime = millis();
    camera_fb_t* fb = esp_camera_fb_get();
    if (fb) {
      String filename = "/images/stream.jpg";
      if (Firebase.Storage.upload(&fbdo, STORAGE_BUCKET_ID, fb->buf, fb->len, filename, "image/jpeg")) {
        Serial.println("✅ Stream uploaded");
      } else {
        Serial.println("❌ Stream upload failed: " + fbdo.errorReason());
      }
      esp_camera_fb_return(fb);
    }
  }
}

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries++ < 30) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ Failed to connect to Wi-Fi");
    return;
  }

  Serial.println("\n✅ Wi-Fi connected.");

  if (MDNS.begin("doorsure")) {
    Serial.println("✅ mDNS responder started: http://doorsure.local/");
  } else {
    Serial.println("❌ mDNS failed to start");
  }

  Serial.printf("📟 Heap: %u bytes\n", ESP.getFreeHeap());
  Serial.printf("📟 PSRAM: %u bytes free of %u bytes\n", ESP.getFreePsram(), ESP.getPsramSize());

  startCamera();

  pinMode(trigPin, OUTPUT);
  digitalWrite(trigPin, LOW);

  pinMode(echoPin, INPUT);
  attachInterrupt(echoPin, echoISR, CHANGE);

  pinMode(REED_PIN, INPUT_PULLUP);
  attachInterrupt(REED_PIN, reedISR, CHANGE);

  // Firebase
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  auth.user.email = "lzr710459698@gmail.com";
  auth.user.password = "Woaini1314@";
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  Serial.println("✅ Firebase ready.");

  server.begin();
  Serial.println("HTTP server started");
}

// ---------- Loop ----------
void loop() {
  static uint32_t lastTrig     = 0;
  static uint32_t lastRTDBpush = 0;
  const uint32_t TRIG_PERIOD   = 500UL;
  const uint32_t PUSH_PERIOD   = 2000UL;

  // Trigger ultrasonic burst
  if (millis() - lastTrig >= TRIG_PERIOD) {
    lastTrig = millis();
    digitalWrite(trigPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(trigPin, LOW);
  }

  // Handle echo timing
  if (echoDone) {
    uint32_t dur;
    noInterrupts();
    dur = echoEnd - echoStart;
    echoDone = false;
    interrupts();

    distance_cm   = dur * 0.0343f / 2.0f;
    distance_inch = distance_cm / 2.54f;

    Serial.printf("📏 Distance: %.2f in\n", distance_inch);
    // Store the max value within this 2s window
  if (distance_inch > maxDistanceInchInWindow) {
    maxDistanceInchInWindow = distance_inch;
  }
  }

  // Handle reed state change
  if (doorChanged) {
    noInterrupts();
    doorStatus = doorStatusISR;
    doorChanged = false;
    interrupts();
    Serial.printf("🚪 Door Status: %s (%d)\n", doorStatus == 0 ? "CLOSED" : "OPEN", doorStatus);
  }

  // Firebase push
  if (!Firebase.RTDB.setInt(&fbdo, "/door-status", doorStatus)) {
    //Serial.println("❌ RTDB push failed (door-status): " + fbdo.errorReason());
  } else {
    //Serial.println("✅ RTDB push success: door-status");
  }

  if (Firebase.ready() && millis() - lastRTDBpush >= 2000) {
    lastRTDBpush = millis();

    if (!Firebase.RTDB.setFloat(&fbdo, "/ultrasonic/distance_inch", maxDistanceInchInWindow)) {
      //Serial.println("❌ RTDB push failed (max_distance_inch): " + fbdo.errorReason());
    } else {
      Serial.printf("✅ Sent max in last 2s: %.2f in\n", maxDistanceInchInWindow);
    }

    // Reset for next 2-second window
    maxDistanceInchInWindow = 0;

  }
      // 📸 Check for snapshot trigger
    if (Firebase.RTDB.getInt(&fbdo, "/trigger/takePhoto") && fbdo.intData() == 1) {
      Serial.println("📸 Trigger received! Capturing image...");
      camera_fb_t* fb = esp_camera_fb_get();
      if (fb) {
        String path = "/images/latest.jpg";
        if (Firebase.Storage.upload(&fbdo, STORAGE_BUCKET_ID, fb->buf, fb->len, path, "image/jpeg")) {
          Serial.println("✅ Snapshot uploaded");
          String url = fbdo.downloadURL();
          Firebase.RTDB.setString(&fbdo, "/images/latest/url", url);
        } else {
          Serial.println("❌ Snapshot upload failed: " + fbdo.errorReason());
        }
        esp_camera_fb_return(fb);
      }
      Firebase.RTDB.setInt(&fbdo, "/trigger/takePhoto", 0);
    }

    // 🔁 Check for streaming toggle
    if (Firebase.RTDB.getBool(&fbdo, "/trigger/streaming")) {
      bool streaming = fbdo.boolData();
      if (streaming) {
        Serial.println("📡 Streaming enabled...");
        streamImagesLoop();  // Start/continue streaming
      } else {
        Serial.println("⛔️ Streaming disabled.");
        // Optional: Do nothing or add delay
      }
    }
  


  delay(5); // Let Wi-Fi/timing tasks breathe
}

# 🚪 DoorSure: ESP32-S3 Smart Door Monitor with Firebase Integration

**DoorSure** is a real-time garage/door monitoring system using an **ESP32-S3** board with a camera, ultrasonic sensor, and reed switch. The system captures distance data, detects door state, streams camera images, and responds to remote triggers using **Firebase Realtime Database** and **Firebase Storage**.

---

## 📸 Features

- **ESP32-S3 Camera Integration** (OV2640)
- **Ultrasonic Distance Sensing** (HC-SR04 or compatible)
- **Door Open/Close Detection** via Reed Switch
- **Firebase Integration**:
  - Live updates of door status and max distance
  - Snapshot upload on command
  - MJPEG-style image streaming every 1s
- **mDNS Access** via `http://doorsure.local/`
- EEPROM support for Wi-Fi credentials
- Minimal latency and efficient use of interrupts

---

## 🛠️ Hardware Requirements

| Component                | Description                                   |
|--------------------------|-----------------------------------------------|
| ESP32-S3 board           | e.g. Seeed XIAO ESP32-S3 or similar           |
| OV2640 Camera Module     | Integrated with ESP32-S3 board                |
| Ultrasonic Sensor        | e.g. HC-SR04, VCC to 5V, GND, Trig, Echo pins |
| Reed Switch              | For detecting door open/close state           |
| Wi-Fi Network            | 2.4GHz recommended                            |

---

## 🔌 Pin Configuration

| Component         | Pin Name   | GPIO |
|------------------|------------|------|
| Ultrasonic TRIG  | `trigPin`  | 41   |
| Ultrasonic ECHO  | `echoPin`  | 42   |
| Reed Switch      | `REED_PIN` | 45   |
| Camera Pins      | See `startCamera()` in `main.cpp` |

---

## 🌐 Firebase Setup

1. Create a Firebase project: https://console.firebase.google.com
2. Enable **Realtime Database** and **Storage**
3. Go to **Project Settings > Service Accounts** and create credentials
4. Enable **Email/Password Authentication**
5. Update these constants in `main.cpp`:

   ```cpp
   #define API_KEY           "<your-firebase-api-key>"
   #define DATABASE_URL      "<your-database-url>"
   #define STORAGE_BUCKET_ID "<your-bucket-id>"
   auth.user.email = "<email>";
   auth.user.password = "<password>";

#include "esp_camera.h"
#include <WiFi.h>
#include <esp_now.h>

// ===========================
// Select camera model
// ===========================
#include "board_config.h"

// ===========================
// WIFI AP SETTINGS
// ===========================
const char *ssid = "NaviSense_Stick";
const char *password = "12345678";

// ===========================
// GLOBAL VARIABLES (Shared with app_httpd.cpp)
// ===========================
bool app_sos = false;
bool app_fall = false;
bool app_cliff = false;

// ===========================
// ESP-NOW DATA STRUCTURE
// ===========================
typedef struct struct_message {
  int distanceL;
  int distanceF;
  int distanceR;
  bool dangerWarning; 
  bool fallDetected; 
  bool cliffDetected; 
  bool sosActive; 
  bool musicActive;
} struct_message;

struct_message incomingData;

// ===========================
// ESP-NOW CALLBACK
// ===========================
void OnDataRecv(const uint8_t * mac, const uint8_t *incomingBytes, int len) {
  memcpy(&incomingData, incomingBytes, sizeof(incomingData));
  
  // Update the global variables that the App will read
  app_sos = incomingData.sosActive;
  app_fall = incomingData.fallDetected;
  app_cliff = incomingData.cliffDetected;
  
  // Debug (Optional - Remove if it spams serial)
  // Serial.printf("SOS: %d | Fall: %d | Cliff: %d\n", app_sos, app_fall, app_cliff);
}

void startCameraServer();
void setupLedFlash();

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.frame_size = FRAMESIZE_UXGA;
  config.pixel_format = PIXFORMAT_JPEG; 
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  if (config.pixel_format == PIXFORMAT_JPEG) {
    if (psramFound()) {
      config.jpeg_quality = 10;
      config.fb_count = 2;
      config.grab_mode = CAMERA_GRAB_LATEST;
    } else {
      config.frame_size = FRAMESIZE_SVGA;
      config.fb_location = CAMERA_FB_IN_DRAM;
    }
  } else {
    config.frame_size = FRAMESIZE_240X240;
#if CONFIG_IDF_TARGET_ESP32S3
    config.fb_count = 2;
#endif
  }

#if defined(CAMERA_MODEL_ESP_EYE)
  pinMode(13, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
#endif

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  sensor_t *s = esp_camera_sensor_get();
  if (s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1); 
    s->set_brightness(s, 1); 
    s->set_saturation(s, -2); 
  }
  if (config.pixel_format == PIXFORMAT_JPEG) {
    s->set_framesize(s, FRAMESIZE_QVGA); 
  }

#if defined(CAMERA_MODEL_M5STACK_WIDE) || defined(CAMERA_MODEL_M5STACK_ESP32CAM)
  s->set_vflip(s, 1);
  s->set_hmirror(s, 1);
#endif
#if defined(CAMERA_MODEL_ESP32S3_EYE)
  s->set_vflip(s, 1);
#endif
#if defined(LED_GPIO_NUM)
  setupLedFlash();
#endif

  // --- 1. START WI-FI AP ---
  WiFi.softAP(ssid, password);
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);

  // --- 2. START ESP-NOW (WIRELESS BRIDGE) ---
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  // Register Callback
  esp_now_register_recv_cb((esp_now_recv_cb_t)OnDataRecv);
  Serial.println("ESP-NOW Receiver Ready");

  // --- 3. START SERVER ---
  startCameraServer();

  Serial.println("-----------------------------------");
  Serial.println("SYSTEM READY");
  Serial.println("1. Connect Phone to WiFi: 'NaviSense_Stick'");
  Serial.println("2. Password: '12345678'");
  Serial.print("3. Stream URL: http://"); Serial.print(IP); Serial.println(":81/stream");
  Serial.print("4. Status API: http://"); Serial.print(IP); Serial.println("/status");
  Serial.println("-----------------------------------");
}

void loop() {
  delay(10000);
}
#include <WiFi.h>
#include <WebServer.h>
#include <DHT.h>
#include <time.h>
#include <SD.h>
#include <SPI.h>
#include "esp_camera.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
//This version has no AI feature. instead, it has slightly higher video stream framerate. I like this one better. operates with less heat


// WiFi
const char* ssid = "SSID"; //      <------change this
const char* password = "PASSWORD";//  <------this too

// NTP Time
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 32400;  // KST
const int daylightOffset_sec = 0;

// DHT11
#define DHTPIN 1
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// SD Card pins for XIAO ESP32S3
#define SD_CS 21

// TB6612FNG Motor Driver Pins
#define LED_AIN1 2
#define LED_AIN2 3
#define LED_PWMA 4
#define PUMP_BIN1 5
#define PUMP_BIN2 6
#define PUMP_PWMB 7
#define STBY 8

// Control states
bool ledState = false;
bool pumpState = false;

// Sensor data storage (last 100 readings for display)
#define MAX_READINGS 100
struct SensorReading {
  float temperature;
  float humidity;
  String timestamp;
};
SensorReading readings[MAX_READINGS];
int readingIndex = 0;
int totalReadings = 0;

WebServer server(80);

// Camera pins for XIAO ESP32S3 Sense
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM     10
#define SIOD_GPIO_NUM     40
#define SIOC_GPIO_NUM     39
#define Y9_GPIO_NUM       48
#define Y8_GPIO_NUM       11
#define Y7_GPIO_NUM       12
#define Y6_GPIO_NUM       14
#define Y5_GPIO_NUM       16
#define Y4_GPIO_NUM       18
#define Y3_GPIO_NUM       17
#define Y2_GPIO_NUM       15
#define VSYNC_GPIO_NUM    38
#define HREF_GPIO_NUM     47
#define PCLK_GPIO_NUM     13

void initCamera() {
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
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_QVGA;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    return;
  }
  
  sensor_t * s = esp_camera_sensor_get();
  s->set_vflip(s, 1);      // flip image
  
  Serial.println("Camera initialized");
}

bool initSDCard() {
  if (!SD.begin(SD_CS)) {
    Serial.println("SD Card initialization failed!");
    return false;
  }
  
  uint8_t cardType = SD.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("No SD card attached");
    return false;
  }
  
  Serial.println("SD Card initialized");
  
  // Create data file if it doesn't exist
  if (!SD.exists("/sensor_data.csv")) {
    File file = SD.open("/sensor_data.csv", FILE_WRITE);
    if (file) {
      file.println("Timestamp,Temperature(C),Humidity(%)");
      file.close();
      Serial.println("Created sensor_data.csv");
    }
  }
  
  return true;
}

void loadHistoricalData() {
  File file = SD.open("/sensor_data.csv", FILE_READ);
  if (!file) {
    Serial.println("No historical data found");
    return;
  }
  
  Serial.println("Loading historical data...");
  
  // Skip header line
  String header = file.readStringUntil('\n');
  
  // Read last MAX_READINGS lines
  String lines[MAX_READINGS];
  int lineCount = 0;
  
  while (file.available() && lineCount < MAX_READINGS) {
    String line = file.readStringUntil('\n');
    if (line.length() > 0) {
      lines[lineCount++] = line;
    }
  }
  file.close();
  
  // Parse and store the last MAX_READINGS entries
  int startIdx = (lineCount > MAX_READINGS) ? (lineCount - MAX_READINGS) : 0;
  for (int i = startIdx; i < lineCount; i++) {
    String line = lines[i];
    
    // Parse CSV: Timestamp,Temperature,Humidity
    int firstComma = line.indexOf(',');
    int secondComma = line.indexOf(',', firstComma + 1);
    
    if (firstComma > 0 && secondComma > 0) {
      readings[totalReadings].timestamp = line.substring(0, firstComma);
      readings[totalReadings].temperature = line.substring(firstComma + 1, secondComma).toFloat();
      readings[totalReadings].humidity = line.substring(secondComma + 1).toFloat();
      
      totalReadings++;
      readingIndex = totalReadings % MAX_READINGS;
      
      if (totalReadings >= MAX_READINGS) break;
    }
  }
  
  Serial.printf("Loaded %d historical readings\n", totalReadings);
}

void initMotorDriver() {
  pinMode(LED_AIN1, OUTPUT);
  pinMode(LED_AIN2, OUTPUT);
  pinMode(LED_PWMA, OUTPUT);
  pinMode(PUMP_BIN1, OUTPUT);
  pinMode(PUMP_BIN2, OUTPUT);
  pinMode(PUMP_PWMB, OUTPUT);
  pinMode(STBY, OUTPUT);
  
  digitalWrite(STBY, HIGH);
  stopLED();
  stopPump();
  Serial.println("Motor driver initialized");
}

void controlLED(bool state) {
  ledState = state;
  if (state) {
    digitalWrite(LED_AIN1, HIGH);
    digitalWrite(LED_AIN2, LOW);
    analogWrite(LED_PWMA, 255); //increase led power
  } else {
    stopLED();
  }
}

void stopLED() {
  digitalWrite(LED_AIN1, LOW);
  digitalWrite(LED_AIN2, LOW);
  analogWrite(LED_PWMA, 0);
}

void controlPump(bool state) {
  pumpState = state;
  if (state) {
    digitalWrite(PUMP_BIN1, HIGH);
    digitalWrite(PUMP_BIN2, LOW);
    analogWrite(PUMP_PWMB, 140); // lower pump power to reduce noise. it's plenty enough
  } else {
    stopPump();
  }
}

void stopPump() {
  digitalWrite(PUMP_BIN1, LOW);
  digitalWrite(PUMP_BIN2, LOW);
  analogWrite(PUMP_PWMB, 0);
}

String getTimestamp() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "N/A";
  }
  char buffer[25];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(buffer);
}

void saveToSD(float temp, float hum, String timestamp) {
  File file = SD.open("/sensor_data.csv", FILE_APPEND);
  if (file) {
    file.printf("%s,%.1f,%.1f\n", timestamp.c_str(), temp, hum);
    file.close();
    Serial.printf("Saved to SD: %s, %.1f°C, %.1f%%\n", timestamp.c_str(), temp, hum);
  } else {
    Serial.println("Failed to open SD card file");
  }
}

void readSensors() {
  float temp = dht.readTemperature();
  float hum = dht.readHumidity();
  
  if (!isnan(temp) && !isnan(hum)) {
    String timestamp = getTimestamp();
    
    // Store in memory array
    readings[readingIndex].temperature = temp;
    readings[readingIndex].humidity = hum;
    readings[readingIndex].timestamp = timestamp;
    readingIndex = (readingIndex + 1) % MAX_READINGS;
    if (totalReadings < MAX_READINGS) totalReadings++;
    
    // Save to SD card
    saveToSD(temp, hum, timestamp);
    
    Serial.printf("Temp: %.1f°C, Humidity: %.1f%%, Time: %s\n", temp, hum, timestamp.c_str());
  } else {
    Serial.println("Failed to read from DHT sensor");
  }
}

String getStatusHTML() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>";
  html += "body { font-family: Arial; margin: 20px; background: #1a1a1a; color: #e0e0e0; }";
  html += ".container { max-width: 1200px; margin: auto; background: #2d2d2d; padding: 20px; border-radius: 10px; box-shadow: 0 0 20px rgba(0,0,0,0.5); }";
  html += "h1 { color: #00d4ff; text-align: center; text-shadow: 0 0 10px rgba(0,212,255,0.5); }";
  html += "h2 { color: #00d4ff; }";
  html += ".section { margin: 20px 0; padding: 15px; border: 1px solid #444; border-radius: 5px; background: #242424; }";
  html += ".camera { text-align: center; }";
  html += ".camera img { max-width: 100%; border: 2px solid #00d4ff; border-radius: 5px; }";
  html += ".controls { display: flex; gap: 20px; justify-content: center; flex-wrap: wrap; }";
  html += ".toggle-btn { padding: 15px 30px; font-size: 16px; border: none; border-radius: 5px; cursor: pointer; transition: all 0.3s; }";
  html += ".on { background: #27ae60; color: white; box-shadow: 0 0 15px rgba(39,174,96,0.5); }";
  html += ".off { background: #e74c3c; color: white; box-shadow: 0 0 15px rgba(231,76,60,0.5); }";
  html += ".toggle-btn:hover { transform: scale(1.05); }";
  html += ".status { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 15px; }";
  html += ".status-box { padding: 15px; background: #1a1a1a; border-radius: 5px; border: 1px solid #444; }";
  html += ".chart-container { position: relative; height: 400px; width: 100%; }";
  html += "</style>";
  html += "<script src='https://cdn.jsdelivr.net/npm/chart.js'></script>";
  html += "</head><body>";
  html += "<div class='container'>";
  html += "<h1>Photobioreactor Monitor</h1>";
  
  // Camera feed
  html += "<div class='section camera'>";
  html += "<h2>Live Camera Feed</h2>";
  html += "<img id='stream' src='/stream' />";
  html += "</div>";
  
  // Controls
  html += "<div class='section'>";
  html += "<h2>Controls</h2>";
  html += "<div class='controls'>";
  html += "<button id='led-btn' class='toggle-btn " + String(ledState ? "on" : "off") + "' onclick='toggleLED()'>";
  html += "LED: <span id='led-status'>" + String(ledState ? "ON" : "OFF") + "</span></button>";
  html += "<button id='pump-btn' class='toggle-btn " + String(pumpState ? "on" : "off") + "' onclick='togglePump()'>";
  html += "Pump: <span id='pump-status'>" + String(pumpState ? "ON" : "OFF") + "</span></button>";
  html += "</div></div>";
  
  // Sensor Data
  html += "<div class='section'>";
  html += "<h2>Environmental Data</h2>";
  int lastIdx = (readingIndex - 1 + MAX_READINGS) % MAX_READINGS;
  float currentTemp = totalReadings > 0 ? readings[lastIdx].temperature : 0;
  float currentHum = totalReadings > 0 ? readings[lastIdx].humidity : 0;
  String currentTime = totalReadings > 0 ? readings[lastIdx].timestamp : "N/A";
  
  html += "<div class='status'>";
  html += "<div class='status-box'><strong>Temperature:</strong> <span id='temp-value'>" + String(currentTemp, 1) + "</span> °C</div>";
  html += "<div class='status-box'><strong>Humidity:</strong> <span id='hum-value'>" + String(currentHum, 1) + "</span> %</div>";
  html += "<div class='status-box'><strong>Last Update:</strong> <span id='time-value'>" + currentTime + "</span></div>";
  html += "</div>";
  html += "<div class='chart-container'><canvas id='chart'></canvas></div>";
  html += "</div>";
  
  html += "</div>";
  
  // JavaScript
  html += "<script>";
  html += "let chart = null;";
  html += "let streamEnabled = true;";
  html += "function toggleLED() { ";
  html += "  fetch('/led/toggle').then(r => r.json()).then(data => {";
  html += "    const btn = document.getElementById('led-btn');";
  html += "    const status = document.getElementById('led-status');";
  html += "    if (data.state) { btn.className = 'toggle-btn on'; status.textContent = 'ON'; }";
  html += "    else { btn.className = 'toggle-btn off'; status.textContent = 'OFF'; }";
  html += "  });";
  html += "}";
  html += "function togglePump() { ";
  html += "  fetch('/pump/toggle').then(r => r.json()).then(data => {";
  html += "    const btn = document.getElementById('pump-btn');";
  html += "    const status = document.getElementById('pump-status');";
  html += "    if (data.state) { btn.className = 'toggle-btn on'; status.textContent = 'ON'; }";
  html += "    else { btn.className = 'toggle-btn off'; status.textContent = 'OFF'; }";
  html += "  });";
  html += "}";
  html += "setInterval(() => { ";
  html += "  if (streamEnabled) document.getElementById('stream').src = '/stream?' + Date.now(); ";
  html += "}, 500);";
  
  // Initial chart data
  html += "const initialTemps = [";
  for (int i = 0; i < totalReadings; i++) {
    int idx = (readingIndex - totalReadings + i + MAX_READINGS) % MAX_READINGS;
    html += String(readings[idx].temperature, 1);
    if (i < totalReadings - 1) html += ",";
  }
  html += "];";
  
  html += "const initialHums = [";
  for (int i = 0; i < totalReadings; i++) {
    int idx = (readingIndex - totalReadings + i + MAX_READINGS) % MAX_READINGS;
    html += String(readings[idx].humidity, 1);
    if (i < totalReadings - 1) html += ",";
  }
  html += "];";
  
  html += "const initialTimes = [";
  for (int i = 0; i < totalReadings; i++) {
    int idx = (readingIndex - totalReadings + i + MAX_READINGS) % MAX_READINGS;
    html += "'" + readings[idx].timestamp + "'";
    if (i < totalReadings - 1) html += ",";
  }
  html += "];";
  
  // Function to convert absolute time to relative time
  html += "function getRelativeTime(timestamp) {";
  html += "  const now = new Date();";
  html += "  const then = new Date(timestamp);";
  html += "  const diffMs = now - then;";
  html += "  const diffMins = Math.floor(diffMs / 60000);";
  html += "  const hours = Math.floor(diffMins / 60);";
  html += "  const mins = diffMins % 60;";
  html += "  return '-' + String(hours).padStart(2, '0') + ':' + String(mins).padStart(2, '0');";
  html += "}";
  
  html += "const initialLabels = initialTimes.map(t => getRelativeTime(t));";
  
  // Initialize chart
  html += "chart = new Chart(document.getElementById('chart'), {";
  html += "type: 'line',";
  html += "data: {";
  html += "labels: initialLabels,";
  html += "datasets: [{";
  html += "label: 'Temperature (°C)',";
  html += "data: initialTemps,";
  html += "borderColor: 'rgb(255, 99, 132)',";
  html += "backgroundColor: 'rgba(255, 99, 132, 0.1)',";
  html += "tension: 0.4,";
  html += "yAxisID: 'y'";
  html += "}, {";
  html += "label: 'Humidity (%)',";
  html += "data: initialHums,";
  html += "borderColor: 'rgb(54, 162, 235)',";
  html += "backgroundColor: 'rgba(54, 162, 235, 0.1)',";
  html += "tension: 0.4,";
  html += "yAxisID: 'y1'";
  html += "}]},";
  html += "options: {";
  html += "responsive: true,";
  html += "maintainAspectRatio: false,";
  html += "interaction: { mode: 'index', intersect: false },";
  html += "plugins: { legend: { labels: { color: '#e0e0e0' } } },";
  html += "scales: {";
  html += "x: { ticks: { maxTicksLimit: 10, color: '#e0e0e0' }, grid: { color: '#444' } },";
  html += "y: { type: 'linear', position: 'left', min: -30, max: 40, ";
  html += "title: { display: true, text: 'Temperature (°C)', color: '#e0e0e0' },";
  html += "ticks: { color: '#e0e0e0' }, grid: { color: '#444' } },";
  html += "y1: { type: 'linear', position: 'right', min: 0, max: 100, ";
  html += "title: { display: true, text: 'Humidity (%)', color: '#e0e0e0' },";
  html += "ticks: { color: '#e0e0e0' }, grid: { drawOnChartArea: false, color: '#444' } }";
  html += "}";
  html += "}";
  html += "});";
  
  // Function to update chart data
  html += "function updateData() {";
  html += "  fetch('/chartdata').then(r => r.json()).then(data => {";
  html += "    const labels = data.times.map(t => getRelativeTime(t));";
  html += "    chart.data.labels = labels;";
  html += "    chart.data.datasets[0].data = data.temps;";
  html += "    chart.data.datasets[1].data = data.hums;";
  html += "    chart.update();";
  html += "    document.getElementById('temp-value').textContent = data.currentTemp.toFixed(1);";
  html += "    document.getElementById('hum-value').textContent = data.currentHum.toFixed(1);";
  html += "    document.getElementById('time-value').textContent = data.currentTime;";
  html += "  });";
  html += "}";
  
  // Update chart data every 10 seconds
  html += "setInterval(updateData, 10000);";
  html += "</script>";
  
  html += "</body></html>";
  return html;
}

void handleRoot() {
  server.send(200, "text/html", getStatusHTML());
}

void handleStream() {
  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) {
    server.send(250, "text/plain", "Camera capture failed");
    return;
  }
  
  server.sendHeader("Content-Type", "image/jpeg");
  server.sendHeader("Content-Length", String(fb->len));
  server.send_P(200, "image/jpeg", (const char *)fb->buf, fb->len);
  
  esp_camera_fb_return(fb);
}

void handleLEDToggle() {
  controlLED(!ledState);
  String json = "{\"state\":" + String(ledState ? "true" : "false") + "}";
  server.send(200, "application/json", json);
}

void handlePumpToggle() {
  controlPump(!pumpState);
  String json = "{\"state\":" + String(pumpState ? "true" : "false") + "}";
  server.send(200, "application/json", json);
}

void handleChartData() {
  String json = "{";
  
  // Current values
  int lastIdx = (readingIndex - 1 + MAX_READINGS) % MAX_READINGS;
  json += "\"currentTemp\":" + String(readings[lastIdx].temperature, 1) + ",";
  json += "\"currentHum\":" + String(readings[lastIdx].humidity, 1) + ",";
  json += "\"currentTime\":\"" + readings[lastIdx].timestamp + "\",";
  
  // Temperature array
  json += "\"temps\":[";
  for (int i = 0; i < totalReadings; i++) {
    int idx = (readingIndex - totalReadings + i + MAX_READINGS) % MAX_READINGS;
    json += String(readings[idx].temperature, 1);
    if (i < totalReadings - 1) json += ",";
  }
  json += "],";
  
  // Humidity array
  json += "\"hums\":[";
  for (int i = 0; i < totalReadings; i++) {
    int idx = (readingIndex - totalReadings + i + MAX_READINGS) % MAX_READINGS;
    json += String(readings[idx].humidity, 1);
    if (i < totalReadings - 1) json += ",";
  }
  json += "],";
  
  // Timestamp array
  json += "\"times\":[";
  for (int i = 0; i < totalReadings; i++) {
    int idx = (readingIndex - totalReadings + i + MAX_READINGS) % MAX_READINGS;
    json += "\"" + readings[idx].timestamp + "\"";
    if (i < totalReadings - 1) json += ",";
  }
  json += "]";
  
  json += "}";
  server.send(200, "application/json", json);
}

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  
  Serial.begin(115200);
  Serial.println("\n\nStarting Photobioreactor Monitor...");
  
  // Initialize DHT
  dht.begin();
  Serial.println("DHT11 initialized");
  
  // Initialize SD Card
  if (initSDCard()) {
    loadHistoricalData();
  }
  
  // Initialize camera
  initCamera();
  
  // Initialize motor driver
  initMotorDriver();
  
  // Connect to WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  
  // Initialize time
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  Serial.println("Getting time from NTP server...");
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    Serial.println("Time synchronized: " + getTimestamp());
  } else {
    Serial.println("Failed to obtain time");
  }
  
  // Setup web server routes
  server.on("/", handleRoot);
  server.on("/stream", handleStream);
  server.on("/led/toggle", handleLEDToggle);
  server.on("/pump/toggle", handlePumpToggle);
  server.on("/chartdata", handleChartData);
  
  server.begin();
  Serial.println("Web server started");
  Serial.println("System ready!");
}

void loop() {
  server.handleClient();
  
  // Read sensors every 1 minute (60000ms)
  static unsigned long lastRead = 0;
  if (millis() - lastRead > 60000) {
    readSensors();
    lastRead = millis();
  }
  
  delay(10);
}
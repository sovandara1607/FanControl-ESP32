#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <LiquidCrystal_I2C.h> 

// ═══════════════════════════════════════════════════
// CONFIGURATION
// ═══════════════════════════════════════════════════
const char* WIFI_SSID = "PARAGONIU-2.4G";
const char* WIFI_PASSWORD = "023996111";
const char* SERVER_URL = "https://www.fanmonitoringiot.systems";
const char* DEVICE_ID = "ESP32-FAN-002";

// ═══════════════════════════════════════════════════
// HARDWARE PINS
// ═══════════════════════════════════════════════════
#define DHT_PIN 4 
#define DHT_TYPE DHT11 
#define MOTOR_ENA 15 
#define MOTOR_IN2 2 

// SONIC SENSOR PINS (HC-SR04)
#define TRIG_PIN 12
#define ECHO_PIN 13

// I2C pins for LCD
#define I2C_SDA 21
#define I2C_SCL 22
#define LCD_ADDRESS 0x27
#define LCD_COLUMNS 16
#define LCD_ROWS 2

// PWM configuration
#define PWM_FREQ 25000
#define PWM_RESOLUTION 8 

// ═══════════════════════════════════════════════════
// TIMING
// ═══════════════════════════════════════════════════
#define POLL_INTERVAL 3000
#define SENSOR_INTERVAL 10000

// ═══════════════════════════════════════════════════
// GLOBALS
// ═══════════════════════════════════════════════════
DHT dht(DHT_PIN, DHT_TYPE);
LiquidCrystal_I2C lcd(LCD_ADDRESS, LCD_COLUMNS, LCD_ROWS);

bool fanState = false;
int fanSpeed = 255;
float lastTemperature = 0;
float lastHumidity = 0;
long lastDistance = 0;

unsigned long lastPollTime = 0;
unsigned long lastSensorTime = 0;

// ═══════════════════════════════════════════════════
// SETUP
// ═══════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);
    Serial.println("\n=== Smart Fan Control System ===");

    // Sonic Sensor setup
    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);

    Wire.begin(I2C_SDA, I2C_SCL);
    lcd.init();          
    lcd.backlight();     
    lcd.setCursor(0, 0);
    lcd.print("System Starting");

    pinMode(MOTOR_IN2, OUTPUT);
    digitalWrite(MOTOR_IN2, LOW);   

    ledcAttach(MOTOR_ENA, PWM_FREQ, PWM_RESOLUTION);
    ledcWrite(MOTOR_ENA, 0);

    dht.begin();
    connectWiFi();

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("System Ready");
    delay(1500);
    lcd.clear();
}

// ═══════════════════════════════════════════════════
// MAIN LOOP
// ═══════════════════════════════════════════════════
void loop() {
    unsigned long now = millis();

    if (WiFi.status() != WL_CONNECTED) {
        connectWiFi();
    }

    if (now - lastPollTime >= POLL_INTERVAL) {
        lastPollTime = now;
        pollFanStatus();
    }

    if (now - lastSensorTime >= SENSOR_INTERVAL) {
        lastSensorTime = now;
        sendSensorData();
    }
}

long readDistance() {
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);
    long duration = pulseIn(ECHO_PIN, HIGH);
    return duration * 0.034 / 2; // Distance in cm
}

void connectWiFi() {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        attempts++;
    }
}

void pollFanStatus() {
    HTTPClient http;
    String url = String(SERVER_URL) + "/api/fan/status";
    http.begin(url);

    int httpCode = http.GET();
    if (httpCode == 200) {
        String payload = http.getString();
        JsonDocument doc;
        deserializeJson(doc, payload);

        const char* status = doc["status"];
        int speed = doc["speed"];
        bool newState = (String(status) == "on");

        if (newState != fanState) {
            fanState = newState;
            applyFanState();
        }

        if (speed != fanSpeed && fanState) {
            fanSpeed = speed;
            applyFanSpeed();
        }
    }
    http.end();
}

void sendSensorData() {
    float temp = dht.readTemperature();
    float hum = dht.readHumidity();
    long dist = readDistance();

    if (isnan(temp) || isnan(hum)) {
        lcd.setCursor(0, 0);
        lcd.print("Sensor Error   ");
        return;
    }

    lastTemperature = temp;
    lastHumidity = hum;
    lastDistance = dist;
    
    updateLCD(temp, hum, dist);

    JsonDocument doc;
    doc["device_identifier"] = DEVICE_ID;
    JsonArray readings = doc["readings"].to<JsonArray>();

    // Temp
    JsonObject t = readings.add<JsonObject>();
    t["sensor_type"] = "temperature"; t["value"] = temp; t["unit"] = "C";

    // Hum
    JsonObject h = readings.add<JsonObject>();
    h["sensor_type"] = "humidity"; h["value"] = hum; h["unit"] = "%";

    // Distance (Sonic)
    JsonObject d = readings.add<JsonObject>();
    d["sensor_type"] = "distance"; d["value"] = dist; d["unit"] = "cm";

    String body;
    serializeJson(doc, body);

    HTTPClient http;
    http.begin(String(SERVER_URL) + "/api/sensor-data");
    http.addHeader("Content-Type", "application/json");
    http.POST(body);
    http.end();
}

void updateLCD(float temp, float hum, long dist) {
    lcd.setCursor(0, 0);
    lcd.print("T:"); lcd.print((int)temp); 
    lcd.print(" H:"); lcd.print((int)hum); 
    lcd.print("% D:"); lcd.print(dist); lcd.print("  ");
    
    lcd.setCursor(0, 1);
    if (fanState) {
        lcd.print("Fan:ON  S:");
        lcd.print(map(fanSpeed, 0, 255, 0, 100));
        lcd.print("%   ");
    } else {
        lcd.print("Fan:OFF         ");
    }
}

void applyFanState() {
    if (fanState) {
        digitalWrite(MOTOR_IN2, HIGH); 
        applyFanSpeed();
    } else {
        digitalWrite(MOTOR_IN2, LOW); 
        ledcWrite(MOTOR_ENA, 0); 
    }
}

void applyFanSpeed() {
    if (fanState) {
        ledcWrite(MOTOR_ENA, fanSpeed); 
    }
}
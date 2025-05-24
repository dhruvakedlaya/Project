#include <Wire.h>
#include <Adafruit_BMP085.h>
#include <DHT.h>
#include <ESP8266WiFi.h>
#include <ThingSpeak.h>

// WiFi Configuration
const char* ssid = "OPPO A31";
const char* password = "smile1234";
WiFiClient client;

// ThingSpeak Configuration
unsigned long myChannelNumber = 2910379;
const char* myWriteAPIKey = "6M18DD6HM2B2A0JU";

// Sensor Pin Configuration
#define DHTPIN D3          // DHT22 data pin
#define DHTTYPE DHT22      // DHT22 sensor type
#define MQ2_PIN A0         // MQ-2 (smoke) analog pin
#define MQ8_PIN A0         // MQ-8 (hydrogen) analog pin
#define MQ8_DIGITAL_PIN D4 // MQ-8 digital threshold pin
#define BUZZER_PIN D5      // Alarm buzzer

// Calibration Constants
#define SEA_LEVEL_PRESSURE 1013.25  // hPa (adjust locally)
#define HYDROGEN_THRESHOLD 50       // ppm (H2 safety limit)
#define SMOKE_THRESHOLD 300         // MQ-2 analog threshold
#define READING_INTERVAL 1000       // 1 second between readings

// MQ Sensor Calibration
float MQ8_R0 = 10.0;  // Hydrogen sensor R0 (kΩ)
float MQ2_R0 = 9.83;  // Smoke sensor R0 (kΩ)

Adafruit_BMP085 bmp;
DHT dht(DHTPIN, DHTTYPE);

// Timing variables
unsigned long previousMillis = 0;
unsigned long thingSpeakPreviousMillis = 0;
const long thingSpeakInterval = 15000; // 15 seconds between ThingSpeak updates

void setup() {
  Serial.begin(115200);
  Wire.begin(D2, D1);  // SDA=D2, SCL=D1 (ESP8266)
  
  // Initialize sensors
  dht.begin();
  pinMode(MQ8_DIGITAL_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  // Connect to WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected to WiFi");
  
  // Initialize ThingSpeak
  ThingSpeak.begin(client);
  
  // Initialize BMP180
  if (!bmp.begin()) {
    Serial.println("BMP180 init failed!");
    while(1);
  }

  Serial.println("\nMulti-Sensor Monitoring System");
  Serial.println("-----------------------------");
  calibrateSensors();
}

void loop() {
  unsigned long currentMillis = millis();
  
  // Take readings every second
  if (currentMillis - previousMillis >= READING_INTERVAL) {
    previousMillis = currentMillis;
    
    // Environmental Sensors
    float pressure = bmp.readPressure() / 100.0;  // hPa
    float altitude = bmp.readAltitude(SEA_LEVEL_PRESSURE);
    float temp = dht.readTemperature();
    float humidity = dht.readHumidity();

    // Gas Sensors
    float smoke = readMQ2(temp, humidity);    // Smoke/LPG in ppm
    float hydrogen = readMQ8(temp, humidity); // Hydrogen in ppm
    bool h2Alert = digitalRead(MQ8_DIGITAL_PIN);

    // Validate readings
    if (isnan(temp) || isnan(humidity)) {
      Serial.println("Failed to read from DHT sensor!");
    } else {
      // Print Data
      printSensorData(pressure, altitude, temp, humidity, hydrogen, smoke);
      
      // Trigger Alerts
      if (hydrogen > HYDROGEN_THRESHOLD || h2Alert) {
        triggerAlarm("HYDROGEN", hydrogen, HYDROGEN_THRESHOLD);
      }
      if (smoke > SMOKE_THRESHOLD) {
        triggerAlarm("SMOKE/LPG", smoke, SMOKE_THRESHOLD);
      }
    }

    // Send to ThingSpeak every 15 seconds
    if (currentMillis - thingSpeakPreviousMillis >= thingSpeakInterval) {
      thingSpeakPreviousMillis = currentMillis;
      sendToThingSpeak(temp, humidity, pressure, hydrogen, smoke);
    }
  }
}

//--------------------- MQ-2 (Smoke/LPG) ---------------------//
float readMQ2(float temp, float humidity) {
  int rawValue = analogRead(MQ2_PIN);
  float voltage = rawValue * (3.3 / 1023.0);
  float RS = (3.3 - voltage) / voltage * 10.0;  // RL=10kΩ for MQ-2

  // Temp/Humidity Compensation
  float comp = 1.0;
  if (!isnan(temp)) comp *= 0.92 + 0.02 * temp;      // Temp correction
  if (!isnan(humidity)) comp *= 0.85 + 0.01 * humidity; // Humidity correction

  float ratio = RS / MQ2_R0 * comp;
  return 1000 * pow(ratio, -1.6);  // Approx ppm for smoke/LPG
}

//--------------------- MQ-8 (Hydrogen) ---------------------//
float readMQ8(float temp, float humidity) {
  int rawValue = analogRead(MQ8_PIN);
  float voltage = rawValue * (3.3 / 1023.0);
  float RS = (3.3 - voltage) / voltage * 10.0;  // RL=10kΩ for MQ-8

  // Validate reading (avoid false positives)
  if (rawValue < 10 || RS < 1.0) return 0.0;  // Invalid reading

  // Temp/Humidity Compensation
  float comp = 1.0;
  if (!isnan(temp)) comp *= 0.8 + 0.02 * temp;       // Temp correction
  if (!isnan(humidity)) comp *= 0.6 + 0.01 * humidity; // Humidity correction

  float ratio = RS / MQ8_R0 * comp;
  float h2ppm = 1000 * pow(ratio, -2.5);  // PPM for H2
  
  // Sanity check
  return (h2ppm >= 0 && h2ppm < 10000) ? h2ppm : 0.0;
}

//--------------------- Calibration ---------------------//
void calibrateSensors() {
  Serial.println("Calibrating MQ sensors in clean air...");
  delay(60000);  // Warm-up (60 sec minimum)
  
  // Calibrate MQ-8 R0
  float avgR0 = 0;
  for (int i = 0; i < 20; i++) {
    float voltage = analogRead(MQ8_PIN) * (3.3 / 1023.0);
    avgR0 += (3.3 - voltage) / voltage * 10.0;  // RS in clean air
    delay(1000);
    Serial.print(".");
  }
  MQ8_R0 = avgR0 / 20.0;
  Serial.printf("\nMQ-8 Calibration Complete. R0 = %.2f kΩ\n", MQ8_R0);
}

//--------------------- Display Data ---------------------//
void printSensorData(float pressure, float altitude, float temp, 
                    float humidity, float hydrogen, float smoke) {
  Serial.println("-----------------------------");
  Serial.printf("| Temperature: %6.1f °C | Humidity: %5.1f %% |\n", temp, humidity);
  Serial.printf("| Pressure: %7.1f hPa | Altitude: %5.1f m |\n", pressure, altitude);
  Serial.printf("| Hydrogen: %6.1f ppm | Smoke: %6.1f ppm |\n", hydrogen, smoke);
  Serial.println("-----------------------------");
}

//--------------------- Alarm System ---------------------//
void triggerAlarm(String gasType, float value, float threshold) {
  Serial.printf("\n!!! %s ALERT !!!\n", gasType.c_str());
  Serial.printf("Value: %.1f ppm (Threshold: %.1f ppm)\n", value, threshold);
  
  // Buzzer pattern (3 quick beeps)
  for (int i = 0; i < 3; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(300);
    digitalWrite(BUZZER_PIN, LOW);
    delay(200);
  }
}

//--------------------- ThingSpeak Integration ---------------------//
void sendToThingSpeak(float temp, float humidity, float pressure, float hydrogen, float smoke) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected. Attempting to reconnect...");
    WiFi.begin(ssid, password);
    delay(5000);
    return;
  }

  // Set fields for ThingSpeak
  ThingSpeak.setField(1, temp);
  ThingSpeak.setField(2, humidity);
  ThingSpeak.setField(3, pressure);
  ThingSpeak.setField(4, hydrogen);
  ThingSpeak.setField(5, smoke);
  

  // Write to ThingSpeak
  int response = ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);
  if (response == 200) {
    Serial.println("Data sent to ThingSpeak successfully");
  } else {
    Serial.printf("ThingSpeak error %d\n", response);
  }
}



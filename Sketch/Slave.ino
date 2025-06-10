#include <dht.h>

dht DHT;
#define DHT22_PIN 8
#define WATER_SENSOR_PIN A1

struct {
    uint32_t total, ok, crc_error, time_out, connect, ack_l, ack_h, unknown;
} stat = {0};

#define NUM_SENSORS 6
#define NUM_PIR_SENSORS 3

const int sensorPins[NUM_SENSORS] = {2, 3, 4, 5, 6, 7};
const int pirPins[NUM_PIR_SENSORS] = {9, 10, 11};

byte sensorValues[NUM_SENSORS];
byte pirValues[NUM_PIR_SENSORS];
int waterSensorValue;

const int gercoPin = 12;
int oldValue = HIGH;

void setup() {
    Serial.begin(115200);
    pinMode(gercoPin, INPUT_PULLUP);
    for (int i = 0; i < NUM_SENSORS; i++) {
        pinMode(sensorPins[i], INPUT);
    }
    for (int i = 0; i < NUM_PIR_SENSORS; i++) {
        pinMode(pirPins[i], INPUT);
    }
}

void loop() {
    checkButtonState();
    readSmokeSensors();
    readPirSensors();
    readDHTSensor();
    readWaterSensor();
    sendDataAsHexString();
    delay(1000);
}

void checkButtonState() {
    int newValue = digitalRead(gercoPin);
    if (newValue != oldValue) {
        oldValue = newValue;
    }
}

void readSmokeSensors() {
    for (int i = 0; i < NUM_SENSORS; i++) {
        sensorValues[i] = (digitalRead(sensorPins[i]) == LOW) ? 1 : 0;
    }
}

void readPirSensors() {
    for (int i = 0; i < NUM_PIR_SENSORS; i++) {
        pirValues[i] = (digitalRead(pirPins[i]) == HIGH) ? 1 : 0;
    }
}

void readDHTSensor() {
    int chk = DHT.read22(DHT22_PIN);
    stat.total++;
    switch (chk) {
        case DHTLIB_OK: stat.ok++; break;
        case DHTLIB_ERROR_CHECKSUM: stat.crc_error++; break;
        case DHTLIB_ERROR_TIMEOUT: stat.time_out++; break;
        case DHTLIB_ERROR_CONNECT: stat.connect++; break;
        case DHTLIB_ERROR_ACK_L: stat.ack_l++; break;
        case DHTLIB_ERROR_ACK_H: stat.ack_h++; break;
        default: stat.unknown++; break;
    }
}

void readWaterSensor() {
    waterSensorValue = analogRead(WATER_SENSOR_PIN);
}

void sendDataAsHexString() {
    String data = "FF";
    data += (oldValue == LOW) ? "01" : "00";

    for (int i = 0; i < NUM_SENSORS; i++) {
        data += (sensorValues[i] == 1) ? "01" : "00";
    }

    for (int i = 0; i < NUM_PIR_SENSORS; i++) {
        data += (pirValues[i] == 1) ? "01" : "00";
    }

    int temperature = static_cast<int>(DHT.temperature);
    String tempHex;
    data += (temperature < 0) ? "00" : "01";

    if (temperature < 0) {
        tempHex = String(abs(temperature), HEX);
    } else {
        tempHex = String(temperature, HEX);
    }

    if (tempHex.length() < 2) tempHex = "0" + tempHex;
    data += tempHex;

    int humidity = static_cast<int>(DHT.humidity);
    String humidityHex = String(humidity, HEX);
    if (humidityHex.length() < 2) humidityHex = "0" + humidityHex;
    data += humidityHex;

    data += (waterSensorValue < 500) ? "00" : "01";
    data += "FF";

    Serial.println(data);
}

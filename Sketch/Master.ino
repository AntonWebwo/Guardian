#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include <EEPROM.h>
#include <SoftwareSerial.h>
#include <DFPlayer_Mini_Mp3.h>

String version = "1.2.2"; // Версия прошивки

LiquidCrystal_I2C lcd(0x27, 16, 2);
uint8_t iconLocked[8] = { 0b01110, 0b10001, 0b10001, 0b11111, 0b11011, 0b11011, 0b11111 };
uint8_t iconUnlocked[8] = { 0b01110, 0b10000, 0b10000, 0b11111, 0b11011, 0b11011, 0b11111 };

const uint8_t ROWS = 4;
const uint8_t COLS = 3;
char keys[ROWS][COLS] = { { '1', '2', '3' }, { '4', '5', '6' }, { '7', '8', '9' }, { '*', '0', '#' } };
uint8_t colPins[COLS] = { 4, 3, 2 };
uint8_t rowPins[ROWS] = { 8, 7, 6, 5 };
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

char secretCode[5] = "";
char phoneOperator[12] = "";
int delay_lock, delay_unlock, delay_pass, pass_timeout, delay_siren;

const int sensorsSize = 6;
const int motionSize = 3;
char sensors[sensorsSize][3];
char motion[motionSize][3];
char gercoState[3];
char waterLevel[3];
int humidity, temperature;

int lastHumidity = -1;
int lastTemperature = -1;

bool Locked = true;
char enterCode[5] = "";
char newCode[5] = "";
char confirmCode[5] = "";

bool isDisplayed = false;
bool isNewCode = false;

const int relayLightPin = A0;
const int relaySirenPin = A1;
const int redLedPin = A2;
const int greenLedPin = A3;

unsigned long sirenStartTime = 0;
unsigned long lightStartTime = 0;

bool sirenActive = false;
bool lightActive = false;
bool alarmActive = false;

const int buzzerPin = 9;
unsigned long alarmStartTime = 0;
const unsigned long alarmInterval = 500;
const unsigned long soundDuration = 300;

unsigned long lastWaterLevelAlertTime = 0;
const unsigned long waterLevelAlertInterval = 30 * 60 * 1000;

const int dfPlayerRX = 11;
const int dfPlayerTX = 10;
const int gsmRX = 13;
const int gsmTX = 12;
SoftwareSerial dfPlayerSerial(dfPlayerRX, dfPlayerTX);
SoftwareSerial gsmSerial(gsmRX, gsmTX);

void setup() {
    Serial.begin(115200);
    dfPlayerSerial.begin(9600);
    mp3_set_serial (dfPlayerSerial);
    mp3_set_volume (25);
    gsmSerial.begin(115200);
    loadSettings();
    lcd.begin(16, 2);
    lcd.createChar(1, iconLocked);
    lcd.createChar(2, iconUnlocked);
    lcd.noBacklight();
    pinMode(redLedPin, OUTPUT);
    pinMode(greenLedPin, OUTPUT);
    pinMode(relayLightPin, OUTPUT);
    pinMode(relaySirenPin, OUTPUT);
    pinMode(buzzerPin, OUTPUT);
    digitalWrite(redLedPin, HIGH);
    digitalWrite(greenLedPin, LOW);
    digitalWrite(relayLightPin, LOW);
    digitalWrite(relaySirenPin, LOW);
    showStartupMessage();
}

void loop() {
    static unsigned long previousMillis = 0;
    const long interval = 100;

    unsigned long currentMillis = millis();
    if (currentMillis - previousMillis >= interval) {
        previousMillis = currentMillis;
        getSerialData();
        if (gsmSerial.available()) {
            char c = gsmSerial.read();
            Serial.print("GSM800L: ");
            Serial.println(c);
        }
        if (Locked) {
            checkSiren();
            monitorAlarmSystem();
            safeLockedLogic();
        } else {
            lcd.backlight();
            safeUnlockedLogic();
        }
        checkAlarm();
    }
}

void loadSettings() {
    EEPROM.get(0, secretCode);
    EEPROM.get(16, phoneOperator);
    EEPROM.get(32, delay_lock);
    EEPROM.get(48, delay_unlock);
    EEPROM.get(64, delay_pass);
    EEPROM.get(80, pass_timeout);
    EEPROM.get(96, delay_siren);

    // Проверка и установка значений по умолчанию
    if (strlen(secretCode) != 4 || strspn(secretCode, "0123456789") != 4) {
        strcpy(secretCode, "0000");
        writeStringToEEPROM(0, secretCode);
        Serial.println("secretCode set to default: 0000");
    }

    if (strlen(phoneOperator) != 11 || strspn(phoneOperator, "0123456789") != 11) {
        strcpy(phoneOperator, "89000000000");
        writeStringToEEPROM(16, phoneOperator);
        Serial.println("phoneOperator set to default: 89000000000");
    }

    if (delay_lock <= 0) {
        delay_lock = 2500;
        EEPROM.put(32, delay_lock);
        Serial.println("delay_lock set to default: 2500");
    }

    if (delay_unlock <= 0) {
        delay_unlock = 500;
        EEPROM.put(48, delay_unlock);
        Serial.println("delay_unlock set to default: 500");
    }

    if (delay_pass <= 0) {
        delay_pass = 1000;
        EEPROM.put(64, delay_pass);
        Serial.println("delay_pass set to default: 1000");
    }

    if (pass_timeout <= 0) {
        pass_timeout = 30000;
        EEPROM.put(80, pass_timeout);
        Serial.println("pass_timeout set to default: 30000");
    }

    if (delay_siren <= 0) {
        delay_siren = 30000;
        EEPROM.put(96, delay_siren);
        Serial.println("delay_siren set to default: 30000");
    }
}

void writeStringToEEPROM(int address, const char* str) {
    for (int i = 0; i < strlen(str); i++) {
        EEPROM.write(address + i, str[i]);
    }
    EEPROM.write(address + strlen(str), '\0');
}

void showStartupMessage() {
    lcd.backlight();
    lcd.setCursor(4, 0);
    lcd.print("Welcome!");
    delay(1000);
    lcd.setCursor(0, 1);
    lcd.write(1);
    String message = "Guardian v" + version;
    for (char c : message) {
        lcd.print(c);
        controlBuzzer(true, 5);
        delay(95);
    }
    delay(500);
    lcd.noBacklight();
}

void getSerialData() {
    if (Serial.available() > 0) {
        String inputData = Serial.readStringUntil('\n');
        inputData.trim();

        if (inputData.startsWith("SET")) {
            handleSetCommand(inputData);
        } else if (inputData.startsWith("GET")) {
            handleGetCommand(inputData);
        } else {
            gercoState[0] = inputData[2];
            gercoState[1] = inputData[3];
            gercoState[2] = '\0';
            for (int i = 0; i < sensorsSize; i++) {
                sensors[i][0] = inputData[4 + i * 2];
                sensors[i][1] = inputData[5 + i * 2];
                sensors[i][2] = '\0';
            }
            for (int i = 0; i < motionSize; i++) {
                motion[i][0] = inputData[16 + i * 2];
                motion[i][1] = inputData[17 + i * 2];
                motion[i][2] = '\0';
            }
            humidity = strtol(inputData.substring(26, 28).c_str(), NULL, 16);
            temperature = strtol(inputData.substring(24, 26).c_str(), NULL, 16);
            if (inputData[23] != '1') temperature = -temperature;
            waterLevel[0] = inputData[28];
            waterLevel[1] = inputData[29];
            waterLevel[2] = '\0';
        }
    }
}

void handleSetCommand(const String& inputData) {
    int equalIndex = inputData.indexOf('=');
    if (equalIndex > 0) {
        String variableName = inputData.substring(4, equalIndex);
        String value = inputData.substring(equalIndex + 1);
        if (variableName == "secretCode") {
            value.toCharArray(secretCode, 5);
            writeStringToEEPROM(0, secretCode);
            Serial.println("writeStringToEEPROM [0]" + String(secretCode));
        } else if (variableName == "phoneOperator") {
            value.toCharArray(phoneOperator, 12);
            writeStringToEEPROM(16, phoneOperator);
            Serial.println("writeStringToEEPROM [16]" + String(phoneOperator));
        } else if (variableName == "delay_lock") {
            delay_lock = value.toInt();
            EEPROM.put(32, delay_lock);
            Serial.println("writeStringToEEPROM [32]" + String(delay_lock));
        } else if (variableName == "delay_unlock") {
            delay_unlock = value.toInt();
            EEPROM.put(48, delay_unlock);
            Serial.println("writeStringToEEPROM [48]" + String(delay_unlock));
        } else if (variableName == "delay_pass") {
            delay_pass = value.toInt();
            EEPROM.put(64, delay_pass);
            Serial.println("writeStringToEEPROM [64]" + String(delay_pass));
        } else if (variableName == "pass_timeout") {
            pass_timeout = value.toInt();
            EEPROM.put(80, pass_timeout);
            Serial.println("writeStringToEEPROM [80]" + String(pass_timeout));
        } else if (variableName == "delay_siren") {
            delay_siren = value.toInt();
            EEPROM.put(96, delay_siren);
            Serial.println("writeStringToEEPROM [96]" + String(delay_siren));
        }
    }
}

void handleGetCommand(const String& inputData) {
    String variableName = inputData.substring(4);
    if (variableName == "info") {
        Serial.print("delay_lock: "); Serial.println(delay_lock);
        Serial.print("delay_unlock: "); Serial.println(delay_unlock);
        Serial.print("delay_pass: "); Serial.println(delay_pass);
        Serial.print("pass_timeout: "); Serial.println(pass_timeout);
        Serial.print("delay_siren: "); Serial.println(delay_siren);
        Serial.print("secretCode: "); Serial.println(secretCode);
        Serial.print("phoneOperator: "); Serial.println(phoneOperator);
    } else if (variableName == "eeprom") {
        printEEPROMTable();
    }
}

void printEEPROMTable() {
  const int bytesPerRow = 16;
  int eepromSize = EEPROM.length();
  Serial.println(F("EEPROM HEX Dump:"));
  Serial.print(F("Addr  "));
  for (int i = 0; i < bytesPerRow; i++) {
    if (i < 16) {
      Serial.print(F(" "));
      if (i < 0x10) Serial.print("0");
      Serial.print(i, HEX);
    }
  }
  Serial.println();
  Serial.println(F("------------------------------------------------"));
  for (int rowStart = 0; rowStart < eepromSize; rowStart += bytesPerRow) {
    Serial.print("0x");
    if (rowStart < 0x10) Serial.print("00");
    else if (rowStart < 0x100) Serial.print("0");
    Serial.print(rowStart, HEX);
    Serial.print(":");
    for (int col=0; col < bytesPerRow; col++) {
      int addr = rowStart + col;
      if (addr < eepromSize) {
        byte val = EEPROM.read(addr);
        Serial.print(" ");
        if (val < 0x10) Serial.print('0');
        Serial.print(val, HEX);
      } else {
        Serial.print("   ");
      }
    }
    Serial.println();
  }
}

void displayTemperature() {
    lcd.setCursor(0, 0);
    lcd.print("TEMP:           ");
    lcd.setCursor(5, 0);
    lcd.print("T:");
    lcd.print(temperature);
    lcd.print(" H:");
    lcd.print(humidity);
}

void safeUnlockedLogic() {
    if (!isDisplayed) {
        lcd.clear();
        lcd.setCursor(0, 1);
        lcd.print("PRESS # TO LOCK");
        isDisplayed = true;
    }

    if (humidity != lastHumidity || temperature != lastTemperature) {
        displayTemperature();
        lastHumidity = humidity;
        lastTemperature = temperature;
    }

    char key = keypad.getKey();
    if (key) {
        if (key == '#') {
            lockSystem();
        } else if (key == '*') {
            enterNewCode();
        }
    }
}

void lockSystem() {
    mp3_play(1); //Взятие под охрану. Покиньте объект!
    lcd.clear();
    lcd.setCursor(4, 0);
    lcd.write(2);
    lcd.print(" ");
    for (int i = 0; i < 4; i++) lcd.write(126);
    lcd.print(" ");
    lcd.write(1);
    Locked = true;
    isDisplayed = false;
    showWaitScreen(delay_lock, greenLedPin);
    digitalWrite(redLedPin, HIGH);
    digitalWrite(greenLedPin, LOW);
}

void enterNewCode() {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Enter new code:");
    lcd.setCursor(5, 1);
    lcd.print("[____]");
    lcd.setCursor(6, 1);
    newCode[0] = '\0';
    isNewCode = true;

    while (isNewCode) {
        char key = keypad.getKey();
        if (key >= '0' && key <= '9' && strlen(newCode) < 4) {
            lcd.print(key);
            strncat(newCode, &key, 1);
            controlBuzzer(true, 25);
            if (strlen(newCode) == 4) {
                lcd.clear();
                lcd.setCursor(0, 0);
                lcd.print("Confirm code:");
                lcd.setCursor(5, 1);
                lcd.print("[____]");
                lcd.setCursor(6, 1);
                confirmCode[0] = '\0';
                isNewCode = false;
            }
        }
    }
    confirmNewCode();
}

void confirmNewCode() {
    while (strlen(confirmCode) < 4) {
        char key = keypad.getKey();
        if (key >= '0' && key <= '9') {
            lcd.print(key);
            strncat(confirmCode, &key, 1);
            controlBuzzer(true, 25);
            if (strlen(confirmCode) == 4) {
                if (strcmp(newCode, confirmCode) == 0) {
                    strcpy(secretCode, newCode);
                    writeStringToEEPROM(0, secretCode);
                    lcd.clear();
                    lcd.setCursor(0, 0);
                    lcd.write(1);
                    lcd.print(" NEW CODE: ");
                    lcd.print(secretCode);
                    digitalWrite(greenLedPin, HIGH);
                    delay(1000);
                    lastHumidity = -1;
                    lastTemperature = -1;
                } else {
                    lcd.clear();
                    lcd.setCursor(0, 0);
                    lcd.print("PASSWORDS ERROR!");
                    delay(1000);
                }
                newCode[0] = '\0';
                confirmCode[0] = '\0';
                isDisplayed = false;
                return;
            }
        }
    }
}

void safeLockedLogic() {
    static bool isVerifying = false;

    if (!isDisplayed) {
        lcd.noBacklight();
        digitalWrite(relayLightPin, LOW);
        mp3_play(2); //Объект установлен под охрану!
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.write(1);
        lcd.print("  ENTER CODE  ");
        lcd.write(1);
        lcd.setCursor(5, 1);
        lcd.print("[____]");
        lcd.setCursor(6, 1);
        isDisplayed = true;
        for (byte i = 0; i < 5; i++) {
            controlBuzzer(true, 100);
            delay(50);
        }
    }

    if (strlen(enterCode) < 4 && !isVerifying) {
        char key = keypad.getKey();
        if (key >= '0' && key <= '9') {
            lcd.print('*');
            strncat(enterCode, &key, 1);
            controlBuzzer(true, 25);
        }
    } else {
        isVerifying = true;
        showWaitScreen(delay_unlock, redLedPin);
        if (strcmp(secretCode, enterCode) == 0) {
            digitalWrite(relaySirenPin, LOW);
            digitalWrite(relayLightPin, LOW);
            sirenActive = false;
            lightActive = false;
            showUnlockMessage();
            enterCode[0] = '\0';
            Locked = false;
            digitalWrite(greenLedPin, HIGH);
            digitalWrite(redLedPin, LOW);
        } else {
            mp3_play(3); //Неправильный пароль! Повторите попытку.
            enterCode[0] = '\0';
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.write(1);
            lcd.print(" ACCESS DENIED!");
            lcd.setCursor(6, 1);
            showWaitScreen(delay_pass, redLedPin);
            digitalWrite(greenLedPin, LOW);
            digitalWrite(redLedPin, HIGH);
            for (byte i = 0; i < 5; i++) {
                controlBuzzer(true, 250);
                delay(250);
            }
        }
        isDisplayed = false;
        isVerifying = false;
    }
}

void showWaitScreen(int delayMillis, int ledPin) {
    lcd.setCursor(2, 1);
    lcd.print("[..........]");
    lcd.setCursor(3, 1);
    for (byte i = 0; i < 10; i++) {
        digitalWrite(ledPin, (i % 2 == 0) ? HIGH : LOW);
        controlBuzzer(true, 50);
        delay(delayMillis);
        lcd.print("=");
    }
    digitalWrite(ledPin, LOW);
}

void showUnlockMessage() {
    deactivateAlarm();
    mp3_play(4); //Внимание! Объект снят с охраны!
    digitalWrite(relaySirenPin, LOW);
    digitalWrite(relayLightPin, HIGH);
    sirenActive = false;
    lightActive = false;
    lastHumidity = -1;
    lastTemperature = -1;
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.write(2);
    lcd.print("   UNLOCKED!  ");
    lcd.write(2);
    for (byte i = 0; i < 5; i++) {
        controlBuzzer(true, 100);
        delay(50);
    }
    delay(1000);
}

void sendAlert(const char* message) {
    lcd.setCursor(0, 0);
    lcd.print(message);
    lcd.setCursor(6, 1);
    //Serial.println(message); Дебаг
    sendDataGSM(message);
}

void monitorAlarmSystem() {
    if (Locked) {
        if (strcmp(gercoState, "01") == 0 && !lightActive) {
            sendAlert("! Door is open !");
            mp3_play(5); //Внимание! Дверь открыта. Введите пароль.
            digitalWrite(relayLightPin, HIGH);
            lightStartTime = millis();
            lightActive = true;
            lcd.backlight();
            for (byte i = 0; i < 5; i++) {
                controlBuzzer(true, 250);
                delay(250);
            }
        }

        if (lightActive && (millis() - lightStartTime >= pass_timeout)) {
            sendAlert("!!!PASS ALARM!!!");
            mp3_play(6); // Тревога! Проникновение на объект!
            digitalWrite(relayLightPin, LOW);
            lightActive = false;
            activateSiren();
            lcd.noBacklight();
        }

        for (int i = 0; i < motionSize; i++) {
            if (strcmp(motion[i], "01") ==  0 && !sirenActive) {
                sendAlert("!!MOTION ALARM!!");
                mp3_play(7); // Тревога! Обнаружено движение!
                activateSiren();
                sirenActive = true;
                sirenStartTime = millis();
            }
        }

        int activeSensors = 0;
        for (int i = 0; i < sensorsSize; i++) {
            if (strcmp(sensors[i], "01") == 0) {
                activeSensors++;
            }
        }

        if (activeSensors >= 2 && !sirenActive) {
            sendAlert("!!!FIRE ALARM!!!");
            mp3_play(8); // Тревога! Задымление помещения!
            activateSiren();
            sirenActive = true;
            sirenStartTime = millis();
        } else if (activeSensors < 2) {
            activeSensors = 0;
        }

        if (strcmp(waterLevel, "01") == 0) {
            unsigned long currentTime = millis();
            if (currentTime - lastWaterLevelAlertTime >= waterLevelAlertInterval) {
                sendAlert("! WATER LEVEL !");
                mp3_play(9); // Внимание! Высокий уровень воды!
                controlBuzzer(true, 250);
                lastWaterLevelAlertTime = currentTime;
            }
        }
    } else {
        lightActive = false;
        lcd.noBacklight();
    }
}

void activateSiren() {
    if (!sirenActive) {
        activateAlarm();
        digitalWrite(relaySirenPin, HIGH);
        sirenStartTime = millis();
        sirenActive = true;
    }
}

void checkSiren() {
    if (sirenActive && (millis() - sirenStartTime >= delay_siren)) {
        digitalWrite(relaySirenPin, LOW);
        deactivateAlarm();
        sirenActive = false;
    }
}

void controlBuzzer(bool turnOn, int soundDur) {
    if (turnOn) {
        digitalWrite(buzzerPin, HIGH);
        delay(soundDur);
        digitalWrite(buzzerPin, LOW);
    } else {
        digitalWrite(buzzerPin, LOW);
    }
}

void activateAlarm() {
    alarmActive = true;
    alarmStartTime = millis();
}

void deactivateAlarm() {
    alarmActive = false;
    digitalWrite(buzzerPin, LOW);
}

void checkAlarm() {
    if (alarmActive) {
        unsigned long currentMillis = millis();
        
        if (currentMillis - alarmStartTime >= alarmInterval) {

            if (digitalRead(buzzerPin) == LOW) {
                digitalWrite(buzzerPin, HIGH);
            } else {
                digitalWrite(buzzerPin, LOW);
            }
            alarmStartTime = currentMillis;
        }
    }
}

void sendDataGSM(const char* message) {
    gsmSerial.print("AT+CMGS=\"");
    gsmSerial.print(phoneOperator);
    gsmSerial.println("\"");
    delay(100);
    gsmSerial.println(message);
    delay(100);
    gsmSerial.write(26);
}

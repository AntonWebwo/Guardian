#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include <EEPROM.h>
#include <SoftwareSerial.h>
#include <DFPlayer_Mini_Mp3.h>

String version = "1.2.4"; // Версия прошивки

LiquidCrystal_I2C lcd(0x27, 16, 2);
uint8_t iconLocked[8] = {0x0e, 0x11, 0x11, 0x1f, 0x1b, 0x1b, 0x1f};
uint8_t iconUnlocked[8] = {0x0e, 0x10, 0x10, 0x1f, 0x1b, 0x1b, 0x1f};

char keys[4][3] = { { '1', '2', '3' }, { '4', '5', '6' }, { '7', '8', '9' }, { '*', '0', '#' } };
uint8_t colPins[3] = { 4, 3, 2 };
uint8_t rowPins[4] = { 8, 7, 6, 5 };
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, 4, 3);

char secretCode[5] = "";
char phoneOperator[12] = "";
int delay_lock, delay_unlock, delay_pass, pass_timeout, delay_siren;

char sensors[6][3];
char motion[3][3];
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

unsigned long sirenStartTime = 0;
unsigned long lightStartTime = 0;

bool sirenActive = false;
bool lightActive = false;
bool alarmActive = false;

unsigned long alarmStartTime = 0;
const unsigned long alarmInterval = 500;
const unsigned long soundDuration = 300;

unsigned long lastWaterLevelAlertTime = 0;
const unsigned long waterLevelAlertInterval = 30 * 60 * 1000;

SoftwareSerial dfPlayerSerial(11, 10);
SoftwareSerial sim800l(13, 12);

void setup() {
    Serial.begin(115200);
    dfPlayerSerial.begin(9600);
    mp3_set_serial (dfPlayerSerial);
    mp3_set_volume (25);
    sim800l.begin(9600);
    sim800l.println("AT+CMGF=1");
    sim800l.println("AT+CNMI=2,2,0,0,0");
    loadSettings();
    lcd.begin(16, 2);
    lcd.createChar(1, iconLocked);
    lcd.createChar(2, iconUnlocked);
    lcd.noBacklight();
    pinMode(A2, OUTPUT);
    pinMode(A3, OUTPUT);
    pinMode(A0, OUTPUT);
    pinMode(A1, OUTPUT);
    pinMode(9, OUTPUT);
    digitalWrite(A2, HIGH);
    digitalWrite(A3, LOW);
    digitalWrite(A0, LOW);
    digitalWrite(A1, LOW);
    showStartupMessage();
}

void loop() {
    static unsigned long previousMillis = 0;
    const long interval = 100;

    unsigned long currentMillis = millis();
    if (currentMillis - previousMillis >= interval) {
        previousMillis = currentMillis;
        getSerialData();
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
    bool settingsChanged = false;

    EEPROM.get(0, secretCode);
    EEPROM.get(16, phoneOperator);
    EEPROM.get(32, delay_lock);
    EEPROM.get(48, delay_unlock);
    EEPROM.get(64, delay_pass);
    EEPROM.get(80, pass_timeout);
    EEPROM.get(96, delay_siren);

    if (strlen(secretCode) != 4 || strspn(secretCode, "0123456789") != 4) {
        strcpy(secretCode, "0000");
        writeStringToEEPROM(0, secretCode);
        settingsChanged = true;
    }

    if (strlen(phoneOperator) != 11 || strspn(phoneOperator, "0123456789") != 11) {
        strcpy(phoneOperator, "89000000000");
        writeStringToEEPROM(16, phoneOperator);
        settingsChanged = true;
    }

    if (delay_lock <= 0) {
        delay_lock = 2500;
        EEPROM.put(32, delay_lock);
        settingsChanged = true;
    }

    if (delay_unlock <= 0) {
        delay_unlock = 500;
        EEPROM.put(48, delay_unlock);
        settingsChanged = true;
    }

    if (delay_pass <= 0) {
        delay_pass = 1000;
        EEPROM.put(64, delay_pass);
        settingsChanged = true;
    }

    if (pass_timeout <= 0) {
        pass_timeout = 30000;
        EEPROM.put(80, pass_timeout);
        settingsChanged = true;
    }

    if (delay_siren <= 0) {
        delay_siren = 30000;
        EEPROM.put(96, delay_siren);
        settingsChanged = true;
    }

    if (settingsChanged) {
        handleGetCommand("GET info");
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

    /* События при входящих SMS */
    // if (sim800l.available()) {
    //     String inputSMS = sim800l.readStringUntil('\n'); // Читаем первую строку
    //     String secondLine = sim800l.readStringUntil('\n'); // Читаем вторую строку
    //     String thirdLine = sim800l.readStringUntil('\n'); // Читаем третью строку

    //     Serial.println(thirdLine); // Выводим третью строку

    //     if (thirdLine.startsWith("UNLOCK")) {
    //         showUnlockMessage();
    //     }
    // }

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
            for (int i = 0; i < 6; i++) {
                sensors[i][0] = inputData[4 + i * 2];
                sensors[i][1] = inputData[5 + i * 2];
                sensors[i][2] = '\0';
            }
            for (int i = 0; i < 3; i++) {
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
        variableName.trim();
        value.trim();
        
        if (variableName == "secretCode") {
            value.toCharArray(secretCode, sizeof(secretCode));
            writeStringToEEPROM(0, secretCode);
        } else if (variableName == "phoneOperator") {
            value.toCharArray(phoneOperator, sizeof(phoneOperator));
            writeStringToEEPROM(16, phoneOperator);
        } else if (variableName == "delay_lock") {
            delay_lock = value.toInt();
            EEPROM.put(32, delay_lock);
        } else if (variableName == "delay_unlock") {
            delay_unlock = value.toInt();
            EEPROM.put(48, delay_unlock);
        } else if (variableName == "delay_pass") {
            delay_pass = value.toInt();
            EEPROM.put(64, delay_pass);
        } else if (variableName == "pass_timeout") {
            pass_timeout = value.toInt();
            EEPROM.put(80, pass_timeout);
        } else if (variableName == "delay_siren") {
            delay_siren = value.toInt();
            EEPROM.put(96, delay_siren);
        } 

        Serial.println("Write to EEPROM: " + variableName + " = " + value);
    } 
}

void handleGetCommand(const String& inputData) {
    String variableName = inputData.substring(4);
    if (variableName == "info") {
        Serial.println("delay_lock: " + String(delay_lock));
        Serial.println("delay_unlock: " + String(delay_unlock));
        Serial.println("delay_pass: " + String(delay_pass));
        Serial.println("pass_timeout: " + String(pass_timeout));
        Serial.println("delay_siren: " + String(delay_siren));
        Serial.println("secretCode: " + String(secretCode));
        Serial.println("phoneOperator: " + String(phoneOperator));
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
    showWaitScreen(delay_lock, A3);
    digitalWrite(A2, HIGH);
    digitalWrite(A3, LOW);
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
                    digitalWrite(A3, HIGH);
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
        digitalWrite(A0, LOW);
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
        showWaitScreen(delay_unlock, 12);
        if (strcmp(secretCode, enterCode) == 0) {
            digitalWrite(A1, LOW);
            digitalWrite(A0, LOW);
            sirenActive = false;
            lightActive = false;
            showUnlockMessage();
            enterCode[0] = '\0';
            Locked = false;
            digitalWrite(A3, HIGH);
            digitalWrite(A2, LOW);
        } else {
            mp3_play(3); //Неправильный пароль! Повторите попытку.
            enterCode[0] = '\0';
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.write(1);
            lcd.print(" ACCESS DENIED!");
            lcd.setCursor(6, 1);
            showWaitScreen(delay_pass, 12);
            digitalWrite(A3, LOW);
            digitalWrite(A2, HIGH);
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
    digitalWrite(A1, LOW);
    digitalWrite(A0, HIGH);
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
    SendSMS(phoneOperator, message);
    
}

void monitorAlarmSystem() {
    if (Locked) {
        if (strcmp(gercoState, "01") == 0 && !lightActive) {
            sendAlert("! Door is open !");
            mp3_play(5); //Внимание! Дверь открыта. Введите пароль.
            digitalWrite(A0, HIGH);
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
            digitalWrite(A0, LOW);
            lightActive = false;
            activateSiren();
            lcd.noBacklight();
        }

        for (int i = 0; i < 3; i++) {
            if (strcmp(motion[i], "01") ==  0 && !sirenActive) {
                sendAlert("!!MOTION ALARM!!");
                mp3_play(7); // Тревога! Обнаружено движение!
                activateSiren();
                sirenActive = true;
                sirenStartTime = millis();
            }
        }

        int activeSensors = 0;
        for (int i = 0; i < 6; i++) {
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
        digitalWrite(A1, HIGH);
        sirenStartTime = millis();
        sirenActive = true;
    }
}

void checkSiren() {
    if (sirenActive && (millis() - sirenStartTime >= delay_siren)) {
        digitalWrite(A1, LOW);
        deactivateAlarm();
        sirenActive = false;
    }
}

void controlBuzzer(bool turnOn, int soundDur) {
    if (turnOn) {
        digitalWrite(9, HIGH);
        delay(soundDur);
        digitalWrite(9, LOW);
    } else {
        digitalWrite(9, LOW);
    }
}

void activateAlarm() {
    alarmActive = true;
    alarmStartTime = millis();
}

void deactivateAlarm() {
    alarmActive = false;
    digitalWrite(9, LOW);
}

void checkAlarm() {
    if (alarmActive) {
        unsigned long currentMillis = millis();
        
        if (currentMillis - alarmStartTime >= alarmInterval) {

            if (digitalRead(9) == LOW) {
                digitalWrite(9, HIGH);
            } else {
                digitalWrite(9, LOW);
            }
            alarmStartTime = currentMillis;
        }
    }
}

void SendSMS(const char* phone, String message) {
  sim800l.print("AT+CMGS=\"");
  sim800l.print(phone);
  sim800l.print("\"\r");
  delay(300);
  sim800l.print(message);
  delay(300);
  sim800l.print((char)26);
  delay(300);
  sim800l.println();
}

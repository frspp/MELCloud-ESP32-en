#include <Arduino.h>
#include <WiFi.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

HTTPClient http;

// WiFi
const char* ssid = "WIFI_NAME";
const char* password = "WIFI_PASSWORD";

// Mel Cloud
String contextKey = "YOUR_KEY";
int buildingId = 163047;
int splitIds[] = { 238535, 238531, 238517 };
String splitNames[] = { "Bureau", "Chambre", "Salon" };
double roomTemperature[] = { 0.0, 0.0, 0.0 };
double setTemperature[] = { 0.0, 0.0, 0.0 };
boolean power[] = { false, false, false };
int setFanSpeed[] = { 0, 0, 0 };
int numberOfFanSpeeds[] = { 0, 0, 0 };
int vaneHorizontal[] = { 0, 0, 0 };
int vaneVertical[] = { 0, 0, 0 };

// Mel Cloud Action EffectiveFlags
const int FLAG_POWER = 1;
const int FLAG_OPERATION_MODE = 2;
const int FLAG_DESIRED_TEMPERATURE = 4;
const int FLAG_FAN_SPEED = 8;
const int FLAG_VANES_VERTICAL = 16;
const int FLAG_VANES_HORIZONTAL = 256;

// GPIOs
const int BUTTON_UP = 13;
const int BUTTON_DOWN = 12;
const int BUTTON_RIGHT = 32;
const int BUTTON_LEFT = 33;
const int BUTTON_OK = 14;
const int BUTTON_SET = 26;
const int BUTTON_CLEAR = 25;
const int LED_RED = 2;
const int LED_GREEN = 15;
const int BUZZER = 4;

enum Action { NONE, TEST, ERROR, REFRESH_SCREEN, GET_DEVICES, SET_DEVICE, SET_ALL_DEVICES };

int flag = 0;

unsigned long lastDevicesRefresh = millis();
unsigned long lastMillisButton = 0;
const unsigned long debounceTime = 250;
boolean isBuzy = false;
const int REFRESH_DEVICES_TIME_IN_MILLI = 10 * 60 * 1000;

const int FUNCTION_VANE_VERTICAL = 2;
const int FUNCTION_VANE_HORIZONTAL = 3;
const int FUNCTION_FAN_SPEED = 4;
const int FUNCTION_TEMPERATURE = 5;
const int FUNCTION_POWER = 6;
const int FUNCTION_ALL = 7;

int selectedDevice = 0;
int selectedFunction = FUNCTION_POWER;
Action action = GET_DEVICES;



void bip() {
	digitalWrite(BUZZER, LOW);
    delay(10);
    digitalWrite(BUZZER, HIGH);
}

void setBuzy(boolean buzy) {
	isBuzy = buzy;
	digitalWrite(LED_GREEN, buzy ? LOW : HIGH);
	digitalWrite(LED_RED, buzy ? HIGH : LOW);
}

void initWifi() {
	// Connexion au Wifi
	Serial.print("Connecting to WiFi...");
	WiFi.begin(ssid, password);
		while (WiFi.status() != WL_CONNECTED) {
		delay(500);
	}
	Serial.println("ok !");
	Serial.printf("SSID: %s, IP: %s\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
}

void initButtonsAndLeds() {
	pinMode(BUTTON_UP, INPUT_PULLUP);
	pinMode(BUTTON_DOWN, INPUT_PULLUP);
	pinMode(BUTTON_RIGHT, INPUT_PULLUP);
	pinMode(BUTTON_LEFT, INPUT_PULLUP);
	pinMode(BUTTON_OK, INPUT_PULLUP);
	pinMode(BUTTON_SET, INPUT_PULLUP);
	pinMode(BUTTON_CLEAR, INPUT_PULLUP);

	attachInterrupt(BUTTON_UP, buttonUpInterrupt, FALLING);
	attachInterrupt(BUTTON_DOWN, buttonDownInterrupt, FALLING);
	attachInterrupt(BUTTON_RIGHT, buttonRightInterrupt, FALLING);
	attachInterrupt(BUTTON_LEFT, buttonLeftInterrupt, FALLING);
	attachInterrupt(BUTTON_OK, buttonOkInterrupt, FALLING);
	attachInterrupt(BUTTON_SET, buttonSetInterrupt, FALLING);
	attachInterrupt(BUTTON_CLEAR, buttonClearInterrupt, FALLING);

	pinMode(LED_RED, OUTPUT);
	pinMode(LED_GREEN, OUTPUT);
}

void initOled() {
	// OLED Display SSD1306 (SCL -> GPIO22, SDA -> GPIO21, Vin -> 3.3V, GND -> GND)
	// https://randomnerdtutorials.com/esp32-ssd1306-oled-display-arduino-ide/
	if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
		Serial.println(F("SSD1306 allocation failed"));
		for(;;);
	}
	display.clearDisplay();
	display.setTextSize(1);
	display.cp437(true); // https://en.wikipedia.org/wiki/Code_page_437
	display.display();
}

void setup() {
	Serial.begin(115200);
	while (!Serial) {
		; // wait for serial port to connect. Needed for native USB port only
	}
	
	initButtonsAndLeds();

	setBuzy(true);

	pinMode(BUZZER, OUTPUT);
	digitalWrite(BUZZER, HIGH);

	initOled();

	initWifi();

	setBuzy(false);
}

void getDevice(int device) {
	String deviceId = String(splitIds[device]);

	String url = "https://app.melcloud.com/Mitsubishi.Wifi.Client/Device/Get?id=" + deviceId + "&buildingID=" + buildingId;
	http.begin(url);
	http.addHeader("Accept", "application/json");
	http.addHeader("Content-Type", "application/json");
	http.addHeader("X-MitsContextKey", contextKey);
	int httpCode = http.GET();
 
	if (httpCode == 200) {
		String payload = http.getString();
		// Serial.println(payload);

		DynamicJsonDocument doc(3000);
		deserializeJson(doc, payload);

		roomTemperature[device] = doc["RoomTemperature"];
		setTemperature[device] = doc["SetTemperature"];
		power[device] = doc["Power"];
		setFanSpeed[device] = doc["SetFanSpeed"];
		numberOfFanSpeeds[device] = doc["NumberOfFanSpeeds"];
		vaneHorizontal[device] = doc["VaneHorizontal"];
		vaneVertical[device] = doc["VaneVertical"];
		// nextCommunication[device] = doc["NextCommunication"];
		// lastCommunication[device] = doc["LastCommunication"];
		// operationMode[device] = doc["OperationMode"];			// 1=HEATING  3=COOLING  8=AUTOMATIC

		action = REFRESH_SCREEN;
	} else {
		Serial.println("Error on HTTP request");
		action = ERROR;
	}
	http.end();
}

void setDevice(int device) {
	DynamicJsonDocument doc(1024);
	
	doc["DeviceID"] = splitIds[device];
	doc["EffectiveFlags"] = flag;
	doc["Power"] = power[device];
	doc["RoomTemperature"] = roomTemperature[device];
	doc["SetTemperature"] = setTemperature[device];
	doc["SetFanSpeed"] = setFanSpeed[device];
	doc["NumberOfFanSpeeds"] = numberOfFanSpeeds[device];
	doc["VaneHorizontal"] = vaneHorizontal[device];
	doc["VaneVertical"] = vaneVertical[device];
	doc["HasPendingCommand"] = true;

	String payload;
	serializeJson(doc, payload);
	Serial.println(payload);

	String url = "https://app.melcloud.com/Mitsubishi.Wifi.Client/Device/SetAta";
	http.begin(url);
	http.addHeader("Accept", "application/json");
	http.addHeader("Content-Type", "application/json");
	http.addHeader("X-MitsContextKey", contextKey);
	int httpCode = http.POST(payload);
	
	if (httpCode == 200) {
		action = GET_DEVICES;
	} else {
		Serial.println("Error on HTTP request");
		action = ERROR;
	}
}

void setSelectedDevice() {
	setBuzy(true);
	setDevice(selectedDevice);
	flag = 0;
	setBuzy(false);
}

void setAllDevices() {
	setBuzy(true);
	for (int device=0; device <= 2; device++) {
		setDevice(device);
	}
	flag = 0;
	setBuzy(false);
}

void getAllDevices() {
	setBuzy(true);
	Serial.print("getAllDevices...");
	for (int device=0; device <= 2; device++) {
		getDevice(device);
	}
	Serial.println("done !");
	action = REFRESH_SCREEN;
	setBuzy(false);
}

void test() {
	bip();
	action = GET_DEVICES;
}

void loop() {

	// TODO calculé le flag par ajout de bit pour cumuler les commandes
	// TODO décalé l'envoi du setDevice pour passer plusieur commande en meme temps

	switch (action) {
		case REFRESH_SCREEN:
			displayDevices();
			break;
		case GET_DEVICES:
			getAllDevices();
			break;
		case SET_DEVICE:
			setSelectedDevice();
			break;
		case SET_ALL_DEVICES:
			setAllDevices();
			break;
		case ERROR:
			bip();
			break;
		case TEST:
			test();
			break;
		case NONE:
			// Ne rien faire !
			break;
	}

	// Check pour un refresh tout les 10 minutes
	if (millis() - lastDevicesRefresh > REFRESH_DEVICES_TIME_IN_MILLI && action == NONE) {
		lastDevicesRefresh = millis();
		action = GET_DEVICES;
	}

  	delay(debounceTime);
}

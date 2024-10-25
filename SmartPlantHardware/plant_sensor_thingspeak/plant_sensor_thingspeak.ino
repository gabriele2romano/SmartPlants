#include <WiFi.h>
//#include <PubSubClient.h>
#include <DHT11.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_TSL2561_U.h>
#include "esp_system.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>  //for saving data built in
#include <ArduinoJson.h>
#include "Keys.h"
//#include "ThingSpeak.h"  // always include thingspeak header file after other header files and custom macros
#include <math.h>
#include <Firebase_ESP_Client.h>
#include <addons/TokenHelper.h>  // Provide the token generation process info.
#include <addons/RTDBHelper.h>   // Provide the RTDB payload printing info and other helper functions.
#include <WebServer.h>
#include <time.h>  //for time stamp
/* #include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <WebSerial.h> */

/* NTP Timestamp */
// NTP server to get time
const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 3600;      // Adjust this according to your timezone
const int daylightOffset_sec = 3600;  // No daylight saving adjustment
/* END NTP Timestamp */

Preferences preferences;  //to save in non volatile memory
String pref_namespace = "credentials";

const char *ssid = "";
const char *password = "";
String user_id = "";

#define ID_NUMBER "00000002"
#define TYPE "plant_sensor" //type of device

int room_number = 1;
/* WebServer */
// Create an instance of the server
WebServer server(80);
// Define base SSID
String ssid_ap_base = "ESP32_SmartPlants_AP_";
String ssid_ap;
/* END WebServer */

#define soil_moisture_pin 35  //0//
#define solenoid_pin 27       //2// //This is the output pin on the Arduino we are using
#define dht11_pin 26          //10//
//#define LED LED_BUILTIN
//#define delay_readings 3600000  //reading window sensor
#define delay_firebase 15000  //60000
int delay_readings = 3600000;

//#define delay_moist_read 1800000   //reading window moisture
int delay_moist_read = 3600000;
//#define watering_time_cost 300000  //max watering time
int watering_time_cost = 3600000;

#define DHT_delay 500
#define uS_TO_S_FACTOR 1000000 /* Conversion factor for micro seconds to seconds */
#define mS_TO_S_FACTOR 1000    /* Conversion factor for micro seconds to seconds */
#define TIME_TO_SLEEP 20       /* Time ESP32 will go to sleep (in seconds) */

/* Firebase Credentials */
const char *myDatabaseUrl = FIREBASE_URL;
const char *myFirebaseKey = FIREBASE_KEY;

// Insert Authorized Email and Corresponding Password
const char *user_mail = "esp@smartplants.com";
const char *user_password = "espdefault";

// Define Firebase Data object
FirebaseData fbdo;

FirebaseAuth auth;
FirebaseConfig config;
/* END Firebase Credentials */

// Define the gravitational constant (m/s^2)
#define GRAVITY 9.81
#define DIAMETER 0.003  // - diameter: Diameter of the tube in meters
#define LENGHT 1.0      // - length: Length of the tube in meters

String plant = "mimosa pudica";
String server_c = "https://open.plantbook.io/api/v1/plant/detail/";

/* ThingSpeak Credentials*/
unsigned long myWaterChannelNumber = SECRET_WATER_CH_ID;
unsigned long myChannelNumber = SECRET_CH_ID;
const char *myWriteAPIKey = SECRET_WRITE_APIKEY;
const char *myWaterWriteAPIKey = SECRET_WATER_WRITE_APIKEY;
/* END ThingSpeak Credentials*/

/* Plant API data */
String pid;
String display_pid;
String alias;
String category;
int max_light_mmol;
int min_light_mmol;
int max_light_lux;
int min_light_lux;
int max_temp;
int min_temp;
int max_env_humid;
int min_env_humid;
int max_soil_moist;
int min_soil_moist;
int max_soil_ec;
int min_soil_ec;
String image_url;
/* END Plant API data */

/* Sensors */
int watering_time = 0;
unsigned long lastWatering = 0;
unsigned long watering_for = 0;

unsigned long last_moist_read = 0;

DHT11 dht11(dht11_pin);
Adafruit_TSL2561_Unified tsl = Adafruit_TSL2561_Unified(TSL2561_ADDR_FLOAT, 12345);

void displaySensorDetails(void) {
  sensor_t sensor;
  tsl.getSensor(&sensor);
  Serial.println("------------------------------------");
  Serial.print("Sensor:       ");
  Serial.println(sensor.name);
  Serial.print("Driver Ver:   ");
  Serial.println(sensor.version);
  Serial.print("Unique ID:    ");
  Serial.println(sensor.sensor_id);
  Serial.print("Max Value:    ");
  Serial.print(sensor.max_value);
  Serial.println(" lux");
  Serial.print("Min Value:    ");
  Serial.print(sensor.min_value);
  Serial.println(" lux");
  Serial.print("Resolution:   ");
  Serial.print(sensor.resolution);
  Serial.println(" lux");
  Serial.println("------------------------------------");
  Serial.println("");
  delay(500);
}

void configureSensor(void) {
  /* You can also manually set the gain or enable auto-gain support */
  // tsl.setGain(TSL2561_GAIN_1X);      /* No gain ... use in bright light to avoid sensor saturation */
  // tsl.setGain(TSL2561_GAIN_16X);     /* 16x gain ... use in low light to boost sensitivity */
  tsl.enableAutoRange(true); /* Auto-gain ... switches automatically between 1x and 16x */

  /* Changing the integration time gives you better sensor resolution (402ms = 16-bit data) */
  tsl.setIntegrationTime(TSL2561_INTEGRATIONTIME_13MS); /* fast but low resolution */
  // tsl.setIntegrationTime(TSL2561_INTEGRATIONTIME_101MS);  /* medium resolution and speed   */
  // tsl.setIntegrationTime(TSL2561_INTEGRATIONTIME_402MS);  /* 16-bit data but slowest conversions */

  /* Update these values depending on what you've set above! */
  Serial.println("------------------------------------");
  Serial.print("Gain:         ");
  Serial.println("Auto");
  Serial.print("Timing:       ");
  Serial.println("13 ms");
  Serial.println("------------------------------------");
}
/* END Sensors */

const int sensor_number = 3;

/* Wifi */
/* const char *mqtt_server = "mqtt-dashboard.com";

String topic;                               //topic used to send sensors data
String debug_topic = "smart_plants_debug";  // topic used for debug messages. Ex: sensors not working properly. */
//String status_topic;                        //topic used to send which parameters are not well for the plant

String tmp;
String tmp1;
String tmp2;
const int maxTries = 50;

/* WiFiClient espClient;
WiFiClient thingSpeakClient;
PubSubClient client(espClient); */
unsigned long lastMsg = 0;
unsigned long sendDataPrevMillis = 0;
#define MSG_BUFFER_SIZE (512)
char msg[MSG_BUFFER_SIZE];
/* END Wifi */

/* MQTT Messages */
const char *messages_friendly[sensor_number][sensor_number][sensor_number] = {
  { // status[0] == 0 (temperature too low)
    { "Brrr... it\'s too cold and dry here. Can you move me to a warmer spot with more sun?",
      "Brrr... it\'s too cold and dry here. Can you move me somewhere warmer with more sun?",
      "Brrr... it\'s too cold and dry here. Can you move me to a warmer spot? Also, it\'s too bright!" },
    { "Brrr... it\'s too cold here. Can you move me to a warmer place with more sun?",
      "Brrr... it\'s cold here, but it\'s bright. Can we find a warmer place?",
      "Brrr... it\'s cold, dry, and too bright here. Can you move me to a more comfortable place?" },
    { "Brrr... it\'s cold and too humid here. Can we find a warmer spot with better conditions?",
      "Brrr... it\'s cold and humid, but it\'s bright. Can we move to a cozier spot?",
      "Brrr... it\'s cold, humid, and very bright here. Can you move me to a warmer and more comfortable place?" } },
  { // status[0] == 1 (temperature normal)
    { "The temperature is nice, but it\'s too dry. Could you use a humidifier?",
      "The temperature is nice, but it\'s too dry and dark. Could you add some light and humidity?",
      "The temperature is nice, but it\'s too dry and very bright here. Could you use a humidifier?" },
    { "The temperature is perfect, and also humidity. Could you give me more sun?",
      "Yay! Everything feels just right! I\'m feeling good.",
      "The temperature is perfect, but it\'s too bright here. Maybe adjust the lighting?" },
    { "The temperature is nice, but it\'s too humid. Could you improve the ventilation?",
      "The temperature is nice, but it\'s humid and bright. Could you improve the ventilation?",
      "The temperature is nice, but it\'s too humid and very bright. Could you improve the ventilation and adjust the lighting?" } },
  { // status[0] == 2 (temperature too high)
    { "Phew... it\'s too hot and dry here. Can you move me to a cooler and more humid place?",
      "Phew... it\'s too hot and dry here. Also, it\'s bright. Can you find a cooler spot?",
      "Phew... it\'s too hot and bright here. Can you find a cooler place?" },
    { "Phew... it\'s too hot here, but the humidity is fine. Can you find a cooler spot?",
      "Phew... it\'s too hot and bright here, but the humidity is okay. Can you find a cooler place?",
      "Phew... it\'s too hot and very bright here. Can you move me to a cooler place?" },
    { "Phew... it\'s too hot and humid here. Can you find a cooler and less humid spot?",
      "Phew... it\'s too hot and humid here, but it\'s bright. Can we move to a cooler spot?",
      "Phew... it\'s too hot, humid, and very bright here. Can you find a cooler and more comfortable environment?" } }
};
/* END MQTT Messages*/

/* WebSerial */
/* AsyncWebServer server(80); */
/* END WebSerial */

/* void callback_webserial(uint8_t *data, size_t len){
  WebSerial.println("Received Data...");
  String d = "";
  for(int i=0; i < len; i++){
    d += char(data[i]);
  }
  WebSerial.println(d);
  //if (d == "test"){}
}
*/

void printWiFiStatus() {
  switch (WiFi.status()) {
    case WL_IDLE_STATUS:
      Serial.println("WiFi status: IDLE");
      break;
    case WL_NO_SSID_AVAIL:
      Serial.println("WiFi status: NO SSID AVAILABLE");
      break;
    case WL_SCAN_COMPLETED:
      Serial.println("WiFi status: SCAN COMPLETED");
      break;
    case WL_CONNECTED:
      Serial.println("WiFi status: CONNECTED");
      break;
    case WL_CONNECT_FAILED:
      Serial.println("WiFi status: CONNECTION FAILED");
      break;
    case WL_CONNECTION_LOST:
      Serial.println("WiFi status: CONNECTION LOST");
      break;
    case WL_DISCONNECTED:
      Serial.println("WiFi status: DISCONNECTED");
      break;
    default:
      Serial.println("WiFi status: UNKNOWN");
      break;
  }
}

void handleRoot() {
  String html = "<html><body>";
  html += "<h1>ESP32 Config</h1>";
  html += "<form action='/submit' method='POST'>";
  html += "SSID: <input type='text' name='ssid'><br>";
  html += "Password: <input type='password' name='password'><br>";
  html += "User ID: <input type='text' name='user_id'><br>";
  html += "<input type='submit' value='Submit'>";
  html += "</form></body></html>";
  server.send(200, "text/html", html);
}

void handleSubmit() {
  if (server.hasArg("ssid") && server.hasArg("password") && server.hasArg("user_id")) {
    String _ssid = server.arg("ssid");
    String _password = server.arg("password");
    String _user_id = server.arg("user_id");

    // Store Wi-Fi credentials and user ID for further use
    Serial.println("Received Wi-Fi credentials:");
    Serial.println("SSID: " + _ssid);
    Serial.println("Password: " + _password);
    Serial.println("User ID: " + _user_id);

    // Save these credentials and try to connect to the Wi-Fi network
    WiFi.begin(_ssid.c_str(), _password.c_str());

    int count = 0;
    while (WiFi.status() != WL_CONNECTED && count < 20) {
      delay(500);
      Serial.print(".");
      count++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nWiFi connected successfully!");
      preferences.putString("ssid", _ssid);
      preferences.putString("password", _password);
      preferences.putString("user_id", _user_id);
      if (WiFi.getSleep() == true) {
        WiFi.setSleep(false);
      }
      server.send(200, "text/html", "Connected successfully! ESP32 is now online.");
    } else {
      Serial.println("\nWiFi connection failed!");
      server.send(200, "text/html", "Wi-Fi connection failed. Please try again.");
    }
  } else {
    server.send(400, "text/html", "Invalid input.");
  }
}

void setupWebServer() {
  // Seed the random generator
  randomSeed(analogRead(0));

  // Generate a random number and append it to the base SSID
  int randomNumber = random(1000, 9999);  // Random 4-digit number
  ssid_ap = ssid_ap_base + String(randomNumber);

  // Set up the ESP32 as an Access Point
  WiFi.softAP(ssid_ap);
  Serial.println("Access Point started");

  // Print the IP address for the AP
  Serial.println(WiFi.softAPIP());

  // Handle web server routes
  server.on("/", handleRoot);
  server.on("/submit", HTTP_POST, handleSubmit);

  // Start the server
  server.begin();
  Serial.println("Server started");

  while (WiFi.status() != WL_CONNECTED) {
    // Handle incoming client requests
    server.handleClient();
  }
  server.stop();
  Serial.println("Server stopped");
}

/* void smartconfig_setup_wifi() {
  delay(10);

  //Init WiFi as Station, start SmartConfig
  WiFi.mode(WIFI_AP_STA);
  WiFi.beginSmartConfig();

  //Wait for SmartConfig packet from mobile
  Serial.println("Waiting for SmartConfig.");
  while (!WiFi.smartConfigDone()) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("SmartConfig received.");

  //Wait for WiFi to connect to AP
  Serial.println("Waiting for WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("WiFi Connected.");



  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());
  preferences.putString("ssid", WiFi.SSID());
  //Serial.println("Saved "+preferences.getString("ssid","nada"));

  Serial.print("Password: ");
  Serial.println(WiFi.psk());
  preferences.putString("password", WiFi.psk());

  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  if (WiFi.getSleep() == true) {
    WiFi.setSleep(false);
  }
} */

unsigned int setup_wifi() {

  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid, password);

  unsigned int cc = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    cc++;
    Serial.print(".");
    if (cc >= 50) {
      return 0;
    }
  }

  randomSeed(micros());

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  if (WiFi.getSleep() == true) {
    WiFi.setSleep(false);
  }
  return 1;
}

void setup_firebase() {

  Serial.printf("Firebase Client v%s\n\n", FIREBASE_CLIENT_VERSION);

  /* Assign the api key (required) */
  config.api_key = myFirebaseKey;

  /* Assign the user sign in credentials */
  auth.user.email = user_mail;
  auth.user.password = user_password;

  /* Assign the RTDB URL (required) */
  config.database_url = myDatabaseUrl;

  /* Assign the callback function for the long running token generation task */
  config.token_status_callback = tokenStatusCallback;  // see addons/TokenHelper.h

  // Comment or pass false value when WiFi reconnection will control by your code or third party library e.g. WiFiManager
  Firebase.reconnectNetwork(true);

  // Since v4.4.x, BearSSL engine was used, the SSL buffer need to be set.
  // Large data transmission may require larger RX buffer, otherwise connection issue or data read time out can be occurred.
  fbdo.setBSSLBufferSize(4096 /* Rx buffer size in bytes from 512 - 16384 */, 1024 /* Tx buffer size in bytes from 512 - 16384 */);

  // Limit the size of response payload to be collected in FirebaseData
  //fbdo.setResponseSize(2048);

  Firebase.begin(&config, &auth);

  Firebase.setDoubleDigits(5);

  config.timeout.serverResponse = 10 * 1000;

  // You can use TCP KeepAlive in FirebaseData object and tracking the server connection status, please read this for detail.
  // https://github.com/mobizt/Firebase-ESP-Client#about-firebasedata-object
  //fbdo.keepAlive(5, 5, 1);

  //Network reconnect timeout (interval) in ms (10 sec - 5 min) when network or WiFi disconnected.
  //config.timeout.networkReconnect = 15 * 1000;

  //Socket connection and SSL handshake timeout in ms (1 sec - 1 min).
  //config.timeout.socketConnection = 15 * 1000;

  //Server response read timeout in ms (1 sec - 1 min).
  //config.timeout.serverResponse = 15 * 1000;

  //RTDB Stream keep-alive timeout in ms (20 sec - 2 min) when no server's keep-alive event data received.
  //config.timeout.rtdbKeepAlive = 45 * 1000;

  //RTDB Stream reconnect timeout (interval) in ms (1 sec - 1 min) when RTDB Stream closed and want to resume.
  //config.timeout.rtdbStreamReconnect = 1 * 1000;

  //RTDB Stream error notification timeout (interval) in ms (3 sec - 30 sec). It determines how often the readStream
  //will return false (error) when it called repeatedly in loop.
  //config.timeout.rtdbStreamError = 3 * 1000;
}

/* void callback(char *topic, byte *payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");

  // Convert payload to a string
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.println(message);

  // Check if the message is "1-shut_down"
  if (message == String(ID_NUMBER) + "-restart") {
    // Reset the ESP32-C3
    tmp = "Resetting now...";
    Serial.println(tmp);
    client.publish(debug_topic.c_str(), tmp.c_str());
    esp_restart();
  } else if (message == String(ID_NUMBER) + "-open") {
    // Reset the ESP32-C3
    tmp = "Opening Valve";
    openPump();
    client.publish(debug_topic.c_str(), tmp.c_str());
  } else if (message == String(ID_NUMBER) + "-close") {
    // Reset the ESP32-C3
    tmp = "Closing Valve";
    closePump();
    client.publish(debug_topic.c_str(), tmp.c_str());
  } else if (message == String(ID_NUMBER) + "-clear_preferences") {
    Serial.print("Preferences cleared");
    preferences.begin(pref_namespace.c_str(), false);
    preferences.clear();
    preferences.end();
  }
}

void reconnect() {
  // Loop until we're reconnected
  Serial.println("Reconnection..to MQTT Server");
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "Plant-" + String(ID_NUMBER);
    // Attempt to connect
    client.setKeepAlive(90);  // setting keep alive to 90 seconds
    client.setBufferSize(512);
    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
      client.subscribe(debug_topic.c_str());
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
} */

// Function to calculate the volume of water used
// Parameters:
// - diameter: Diameter of the tube in meters
// - length: Length of the tube in meters
// - time_elapsed: Time elapsed with valve open in seconds
// Returns: Volume of water in liters
double calculateWaterVolume(double diameter, double length, double time_elapsed) {
  // Calculate the cross-sectional area of the tube (m^2)
  double radius = diameter / 2.0;
  double area = M_PI * radius * radius;

  // Calculate the velocity of water using gravity (m/s)
  // This is a simple approximation: v = sqrt(2 * g * h)
  // Assuming the height of the water column is the tube length for simplicity
  double velocity = sqrt(2 * GRAVITY * length);

  // Calculate the flow rate (m^3/s)
  double flow_rate = area * velocity;

  // Calculate the volume of water used (m^3)
  double volume_m3 = flow_rate * time_elapsed;

  // Convert volume from cubic meters to liters
  double volume_liters = volume_m3 * 1000.0;

  return volume_liters;
}

// LED control
/* void ledON() {
  Serial.println("LED ON");
  digitalWrite(LED, LOW);
}

void ledOFF() {
  Serial.println("LED OFF");
  digitalWrite(LED, HIGH);
} */

void openPump() {
  Serial.println("Opened Pump");
  digitalWrite(solenoid_pin, HIGH);  //Switch Solenoid ON
}

void closePump() {
  Serial.println("Closed Pump");
  digitalWrite(solenoid_pin, LOW);  //Switch Solenoid OFF
}

void followRedirect(HTTPClient &http) {
  String newLocation = http.header("Location");
  Serial.println("Redirecting to: " + newLocation);

  // Close previous connection
  http.end();

  // Follow the redirect
  WiFiClientSecure client_s;
  client_s.setInsecure();
  if (http.begin(client_s, newLocation)) {
    http.addHeader("Authorization", "Token " + String(PLANTBOOK_API_KEY));
    int httpCode = http.GET();
    Serial.println("HTTP Code: " + String(httpCode));

    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      Serial.println("Received payload:");
      Serial.println(payload);
    } else {
      Serial.printf("GET request failed after redirect, error: %s\n", http.errorToString(httpCode).c_str());
    }

    http.end();
  } else {
    Serial.println("Unable to connect to redirected URL");
  }
}

void makeGetRequest() {
  WiFiClientSecure client_s;
  client_s.setInsecure();  // Use this only if you don't need SSL verification
  // Alternatively, use client.setCACert() to set a specific root CA certificate.

  HTTPClient http;

  if (http.begin(client_s, server_c)) {
    http.addHeader("Authorization", "Token " + String(PLANTBOOK_API_KEY));
    //Serial.println("Added header: Authorization: " + String("Token " + apiKey));

    int httpCode = http.GET();
    if (httpCode > 0) {
      if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        Serial.println("Received payload:");
        Serial.println(payload);
        http.end();

        // Parse the JSON response
        DynamicJsonDocument doc(1024);
        DeserializationError error = deserializeJson(doc, payload);

        if (error) {
          Serial.print("deserializeJson() failed: ");
          Serial.println(error.c_str());
          return;
        }

        // Access and print each element in the JSON response
        pid = String(doc["pid"]);
        display_pid = String(doc["display_pid"]);
        alias = String(doc["alias"]);
        category = String(doc["category"]);
        max_light_mmol = doc["max_light_mmol"];
        min_light_mmol = doc["min_light_mmol"];
        max_light_lux = doc["max_light_lux"];
        min_light_lux = doc["min_light_lux"];
        max_temp = doc["max_temp"];
        min_temp = doc["min_temp"];
        max_env_humid = doc["max_env_humid"];
        min_env_humid = doc["min_env_humid"];
        max_soil_moist = doc["max_soil_moist"];
        min_soil_moist = doc["min_soil_moist"];
        max_soil_ec = doc["max_soil_ec"];
        min_soil_ec = doc["min_soil_ec"];
        image_url = String(doc["image_url"]);

        // Print each element
        Serial.println("Parsed JSON:");
        Serial.println("pid: " + String(pid));
        Serial.println("display_pid: " + String(display_pid));
        Serial.println("alias: " + String(alias));
        Serial.println("category: " + String(category));
        Serial.println("max_light_mmol: " + String(max_light_mmol));
        Serial.println("min_light_mmol: " + String(min_light_mmol));
        Serial.println("max_light_lux: " + String(max_light_lux));
        Serial.println("min_light_lux: " + String(min_light_lux));
        Serial.println("max_temp: " + String(max_temp));
        Serial.println("min_temp: " + String(min_temp));
        Serial.println("max_env_humid: " + String(max_env_humid));
        Serial.println("min_env_humid: " + String(min_env_humid));
        Serial.println("max_soil_moist: " + String(max_soil_moist));
        Serial.println("min_soil_moist: " + String(min_soil_moist));
        Serial.println("max_soil_ec: " + String(max_soil_ec));
        Serial.println("min_soil_ec: " + String(min_soil_ec));
        Serial.println("image_url: " + String(image_url));
      } else if (httpCode == HTTP_CODE_MOVED_PERMANENTLY || httpCode == HTTP_CODE_FOUND) {
        if (http.header("Location") != NULL)
          followRedirect(http);
        else
          Serial.println(String(httpCode) + " No redirect header");
      } else {
        Serial.println("HTTP Code: " + String(httpCode));
      }
    } else {
      Serial.printf("GET request failed, error: %s\n", http.errorToString(httpCode).c_str());
    }
  } else {
    Serial.println("Unable to connect");
  }
}

// Function to return the formatted time as a string
String getFormattedTime() {
  struct tm timeinfo;

  // Get the local time
  if (!getLocalTime(&timeinfo)) {
    return "Failed to obtain time";
  }

  // Create formatted time string in "YYYY-MM-DD_HH-MM-SS"
  char timeString[20];
  strftime(timeString, sizeof(timeString), "%Y-%m-%d_%H-%M-%S", &timeinfo);

  // Return the formatted string
  return String(timeString);
}

int temperature = 0;
int humidity = 0;
void setup() {
  Serial.begin(115200);
  //pinMode(LED, OUTPUT);           // Initialize the BUILTIN_LED pin as an output
  //digitalWrite(LED, HIGH);        // turn off led
  pinMode(solenoid_pin, OUTPUT);  //Sets the solenoid pin as an output
  //topic = "smart_mirror";         //"smart_plants/" + String(ID_NUMBER);

  /* Check flash memory */
  preferences.begin(pref_namespace.c_str(), false);
  tmp = preferences.getString("ssid", "");
  ssid = tmp.c_str();
  tmp1 = preferences.getString("password", "");
  password = tmp1.c_str();
  tmp2 = preferences.getString("user_id", "");
  user_id = tmp2.c_str();
  Serial.println("Retrieved SSID: " + String(ssid));
  if (strcmp(ssid, "") == 0 || strcmp(password, "") == 0) {
    setupWebServer();
  } else {
    if (!setup_wifi()) {
      Serial.println("Failed connecting to " + String(ssid));
      setupWebServer();
    }
  }
  /* END Check Flash Memory */

  /* NTP */
  // Initialize NTP
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  /* NTP */

  setup_firebase();
  if (Firebase.ready()) fetchFirebaseVariables();

  /* ThingSpeak Setup */
  /* ThingSpeak.begin(thingSpeakClient);  // Initialize ThingSpeak

  client.setServer(mqtt_server, 1883);  //Initialize server mqtt
  client.setCallback(callback);         //set how to handle messages */
  /* END ThingSpeak Setup */

  /* DB */
  plant.replace(" ", "%20");
  server_c += plant + "/";
  Serial.println(server_c);

  makeGetRequest();
  /* END DB */

  /* Sensor setup */
  //dht11.setDelay(delay_readings);  // Set this to the desired reading delay. Default is 500ms.
  if (!tsl.begin()) {
    /* There was a problem detecting the TSL2561 ... check your connections */
    String err_tsl = "Ooops, no TSL2561 detected ... Check your wiring or I2C ADDR!";
    Serial.print(err_tsl);
    //client.publish(debug_topic.c_str(), err_tsl.c_str());
    //while (1);
  }
  /* Display some basic information on this sensor */
  displaySensorDetails();

  /* Setup the sensor gain and integration time */
  configureSensor();

  tmp = "ALL SET!";
  Serial.println(tmp);
  //client.publish(debug_topic.c_str(), tmp.c_str());
  /* END Sensor setup */

  /* sleep setup */
  //not useful for now
  /* end sleep setup */

  /* reconnect();
  int soil_moisture = analogRead(soil_moisture_pin);
  unsigned long now = millis();  //esp_timer_get_time(); may work better han millis */
  /* First Cycle Moisture*/
  /* if (watering_time == 0 && soil_moisture > max_soil_ec) {
    lastWatering = now;
    watering_time = watering_time_cost;  //120sec
    Serial.print(String(soil_moisture) + " ");
    openPump();
  } else if (watering_time != 0 && (soil_moisture < (max_soil_ec - 100) || now - lastWatering > watering_time)) {
    Serial.print(String(soil_moisture) + " ");
    closePump();
    watering_time = 0;
    watering_for = now - lastWatering;
    Serial.println("Watered for: " + String(watering_for / 1000) + "seconds");
  }
  if (soil_moisture < min_soil_ec) {
    watering_for = -1;
    Serial.println("Expose to Sun");
  } */
  /* END First Cycle Moisture*/
  /* First Cycle Sensors*/
  // Attempt to read the temperature and humidity values from the DHT11 sensor.
  /*  int result_dht11 = dht11.readTemperatureHumidity(temperature, humidity);
  if (result_dht11 != 0) {
    // Print error message based on the error code.

    tmp = DHT11::getErrorString(result_dht11);
    Serial.println(tmp);
    client.publish(debug_topic.c_str(), tmp.c_str());
  } */

  // Get a new sensor event
  /* sensors_event_t event;
  tsl.getEvent(&event);
 */
  /*
        0: Too Low
        1: Ok
        2: Too High
      */
  /* unsigned int status_light = 1;
  unsigned int status_temp = 1;
  unsigned int status_hum = 1;
  if (max_light_lux - event.light < 0) status_light = 2;
  else if (min_light_lux - event.light > 0) status_light = 0;

  if (max_temp - temperature < 0) status_temp = 2;
  else if (min_temp - temperature > 0) status_temp = 0;

  if (max_env_humid - humidity < 0) status_hum = 2;
  else if (min_env_humid - humidity > 0) status_hum = 0;
  String mess = "";  //"From " + plant + ": ";

  const char *message_m = messages_friendly[status_temp][status_hum][status_light];
  mess += message_m;


  if (watering_for == -1 && status_hum == 2 && status_light == 0) {
    mess += " My moisture is too wet!";
  }

  tmp = "{\"plant\": \"" + display_pid + "\",\"plant_img\": \"" + image_url.c_str() + "\", \"watering_time\": \"" + String(int(watering_for / 1000)) + "\",\"sensors\": {\"soil_moisture\":\"" + String(soil_moisture) + "\", \"temperature\": \"" + String(temperature) + "\", \"humidity\": \"" + String(humidity) + "\", \"light\": \"" + String(event.light) + "\"},\"message\": \"" + mess + "\"}";


  Serial.print("Publish message: ");
  Serial.println(tmp.c_str());
  client.publish(topic.c_str(), tmp.c_str()); */

  /* ThingSpeak */
  // set the fields with the values
  /* if (watering_for != 0) {
    double volume = calculateWaterVolume(DIAMETER, LENGHT, int(watering_for / 1000));
    ThingSpeak.setField(1, int(volume));
  }
  ThingSpeak.setField(2, temperature);
  ThingSpeak.setField(3, humidity);
  ThingSpeak.setField(4, int(event.light));
  ThingSpeak.setField(5, soil_moisture);
  // write to the ThingSpeak channel
  int x = ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);
  if (x == 200) {
    Serial.println("Channel update successful.");
  } else {
    Serial.println("Problem updating channel. HTTP error code " + String(x));
  }

  if (watering_for != 0) {
    double volume = calculateWaterVolume(DIAMETER, LENGHT, int(watering_for / 1000));
    ThingSpeak.setField(1, int(volume));
    int water_channel = ThingSpeak.writeFields(myWaterChannelNumber, myWaterWriteAPIKey);
    if (water_channel == 200) {
      Serial.println("Water Channel update successful.");
    } else {
      Serial.println("Problem updating water channel. HTTP error code " + String(water_channel));
    }
    watering_for = 0;
  } */
  /* END ThingSpeak */
  /* END First Cycle Sensors*/
}

void manage_serial_commands() {
  if (Serial.available() > 0) {                     // Check if data is available to read
    String command = Serial.readStringUntil('\n');  // Read the command until newline
    command.trim();                                 // Remove any leading/trailing whitespace

    // Execute the command
    if (command == "clear_preferences") {
      Serial.print("Preferences cleared");
      preferences.begin(pref_namespace.c_str(), false);
      preferences.clear();
      preferences.end();
    } else {
      Serial.println("Unknown command");
    }
  }
}


void fetchFirebaseVariables() {
  if (Firebase.RTDB.getInt(&fbdo, "/" + user_id + "/devices/" + String(ID_NUMBER) + "/interval")) {
    //Serial.print("Data type: ");
    //Serial.println(fbdo.dataType());
    //Serial.println("/" + user_id + "/devices/" + String(ID_NUMBER) + "/interval");
    // Check if the data type is integer or number (Firebase can return "number" as well)
    if (fbdo.dataType() == "int" || fbdo.dataType() == "number") {
      //Serial.println("FB Data Fetched Successfully");
      // Use the fetched integer value and calculate delay_readings
      delay_readings = fbdo.intData() * mS_TO_S_FACTOR;
      //Serial.println(delay_readings);
    } else {
      // Handle unexpected data types
      Serial.println("Unexpected data type. Expected an integer.");
    }
  } else {
    Serial.println("FB interval Data Fetching failed");
    Serial.println("ERROR: " + fbdo.errorReason());
  }
  if (Firebase.RTDB.getString(&fbdo, "/" + user_id + "/devices/" + String(ID_NUMBER) + "/plant")) {
    //Serial.print("Data type: ");
    //Serial.println(fbdo.dataType());
    // Check if the data type is integer or number (Firebase can return "number" as well)
    if (fbdo.dataType() == "string") {
      //Serial.println("FB Data Fetched Successfully");
      // Use the fetched integer value and calculate delay_readings
      plant = fbdo.stringData();
      //Serial.println(delay_readings);
    } else {
      // Handle unexpected data types
      Serial.println("Unexpected data type. Expected an string.");
    }
  } else {
    Serial.println("FB plant Data Fetching failed");
    Serial.println("ERROR: " + fbdo.errorReason());
  }
  if (Firebase.RTDB.getInt(&fbdo, "/" + user_id + "/devices/" + String(ID_NUMBER) + "/interval_moisture")) {
    //Serial.print("Data type: ");
    //Serial.println(fbdo.dataType());

    // Check if the data type is integer or number (Firebase can return "number" as well)
    if (fbdo.dataType() == "int" || fbdo.dataType() == "number") {
      //Serial.println("FB Data Fetched Successfully");
      // Use the fetched integer value and calculate delay_readings
      delay_moist_read = fbdo.intData() * mS_TO_S_FACTOR;
      //Serial.println(delay_readings);
    } else {
      // Handle unexpected data types
      Serial.println("Unexpected data type. Expected an integer.");
    }
  } else {
    Serial.println("FB moisture interval Data Fetching failed");
    Serial.println("ERROR: " + fbdo.errorReason());
  }
  if (Firebase.RTDB.getInt(&fbdo, "/" + user_id + "/devices/" + String(ID_NUMBER) + "/max_watering_time")) {
    //Serial.print("Data type: ");
    //Serial.println(fbdo.dataType());

    // Check if the data type is integer or number (Firebase can return "number" as well)
    if (fbdo.dataType() == "int" || fbdo.dataType() == "number") {
      //Serial.println("FB Data Fetched Successfully");
      // Use the fetched integer value and calculate delay_readings
      watering_time_cost = fbdo.intData() * mS_TO_S_FACTOR;
      //Serial.println(delay_readings);
    } else {
      // Handle unexpected data types
      Serial.println("Unexpected data type. Expected an integer.");
    }
  } else {
    Serial.println("FB max watering time Data Fetching failed");
    Serial.println("ERROR: " + fbdo.errorReason());
  }
  if (Firebase.RTDB.getInt(&fbdo, "/" + user_id + "/devices/" + String(ID_NUMBER) + "/room")) {
    //Serial.print("Data type: ");
    //Serial.println(fbdo.dataType());

    // Check if the data type is integer or number (Firebase can return "number" as well)
    if (fbdo.dataType() == "int" || fbdo.dataType() == "number") {
      //Serial.println("FB Data Fetched Successfully");
      // Use the fetched integer value and calculate delay_readings
      room_number = fbdo.intData();
      //Serial.println(delay_readings);
    } else {
      // Handle unexpected data types
      Serial.println("Unexpected data type. Expected an integer.");
    }
  } else {
    Serial.println("FB room number Data Fetching failed");
    Serial.println("ERROR: " + fbdo.errorReason());
  }
}

void loop() {
  if (Firebase.isTokenExpired()) {
    Serial.println("Refreshed Firebase Token");
    Firebase.refreshToken(&config);
  }
  manage_serial_commands();
  if (WiFi.status() == WL_CONNECTED) {  //Connected to WiFi
    /* if (!client.connected()) {
      reconnect();
    }
    client.loop(); */

    if (Firebase.ready()) {
      unsigned long now = millis();  //esp_timer_get_time(); may work better han millis
      if (now - sendDataPrevMillis > delay_firebase || sendDataPrevMillis == 0) {
        Serial.println("Heap:");
        Serial.println(ESP.getFreeHeap());  //handle auth task firebase
        sendDataPrevMillis = millis();
        fetchFirebaseVariables();
      }

      if (now - last_moist_read > delay_moist_read || last_moist_read == 0) {
        last_moist_read = now;
        int soil_moisture = analogRead(soil_moisture_pin);
        if (watering_time == 0 && soil_moisture > max_soil_ec) {
          lastWatering = now;
          watering_time = watering_time_cost;  //120sec
          Serial.print(String(soil_moisture) + " ");
          openPump();
        } else if (watering_time != 0 && (soil_moisture < (max_soil_ec - 100) || now - lastWatering > watering_time)) {
          Serial.print(String(soil_moisture) + " ");
          closePump();
          watering_time = 0;
          watering_for = now - lastWatering;
          Serial.println("Watered for: " + String(watering_for / 1000) + "seconds");
        }
        if (soil_moisture < min_soil_ec) {
          watering_for = -1;
          Serial.println("Expose to Sun");
        }
      }

      if (now - lastMsg > (delay_readings - DHT_delay) || lastMsg == 0) {
        lastMsg = now; /* 
      int temperature = -1;
      int humidity = -1; */
        int soil_moisture = analogRead(soil_moisture_pin);
        // Attempt to read the temperature and humidity values from the DHT11 sensor.
        int result_dht11 = dht11.readTemperatureHumidity(temperature, humidity);
        if (result_dht11 != 0) {
          // Print error message based on the error code.

          tmp = DHT11::getErrorString(result_dht11);
          Serial.println(tmp);
          //client.publish(debug_topic.c_str(), tmp.c_str());
        }

        // Get a new sensor event
        sensors_event_t event;
        tsl.getEvent(&event);

        /*
        0: Too Low
        1: Ok
        2: Too High
      */
        unsigned int status_light = 1;
        unsigned int status_temp = 1;
        unsigned int status_hum = 1;
        if (max_light_lux - event.light < 0) status_light = 2;
        else if (min_light_lux - event.light > 0) status_light = 0;

        if (max_temp - temperature < 0) status_temp = 2;
        else if (min_temp - temperature > 0) status_temp = 0;

        if (max_env_humid - humidity < 0) status_hum = 2;
        else if (min_env_humid - humidity > 0) status_hum = 0;

        String mess = "";  //"From " + plant + ": ";

        const char *message_m = messages_friendly[status_temp][status_hum][status_light];
        mess += message_m;


        if (watering_for == -1 && status_hum == 2 && status_light == 0) {
          mess += " My moisture is too wet!";
        }

        tmp = "{\"plant\": \"" + display_pid + "\",\"plant_img\": \"" + image_url.c_str() + "\", \"watering_time\": \"" + String(int(watering_for / 1000)) + "\",\"sensors\": {\"soil_moisture\":\"" + String(soil_moisture) + "\", \"temperature\": \"" + String(temperature) + "\", \"humidity\": \"" + String(humidity) + "\", \"light\": \"" + String(event.light) + "\"},\"message\": \"" + mess + "\"}";


        Serial.print("Publish message: ");
        Serial.println(tmp.c_str());
        //client.publish(topic.c_str(), tmp.c_str());
        // Write the string to the specified path in Firebase
        if (Firebase.RTDB.setString(&fbdo, "/" + user_id + "/devices/" + String(ID_NUMBER) + "/last_message", mess)) {
          Serial.println("Last Message stored successfully in Firebase");
        } else {
          // Handle errors
          Serial.println("Failed to store last message string in Firebase");
          Serial.println("ERROR: " + fbdo.errorReason());
        }

        String timestamp = getFormattedTime();  // Get the current timestamp
        // Prepare the JSON object for storing sensor data
        FirebaseJson json;
        if (watering_for != 0) {
          double volume = calculateWaterVolume(DIAMETER, LENGHT, int(watering_for / 1000));
          json.set("/water", volume);
          json.set("/watering_time", watering_for / 1000);
        }
        json.set("/plant", plant);
        json.set("/room", room_number);
        json.set("/light", event.light);
        json.set("/soil_moisture", soil_moisture);
        json.set("/temperature", temperature);
        json.set("/humidity", humidity);
        // Print the JSON data to verify its structure
        /* String jsonString;
        json.toString(jsonString, true);
        Serial.println("JSON Data: " + jsonString); */

        // Store the data sensor in Firebase
        if (Firebase.RTDB.setJSON(&fbdo, "/" + user_id + "/devices/" + String(ID_NUMBER) + "/sensors/" + timestamp, &json)) {
          Serial.println("Sensor data stored successfully in Firebase");
        } else {
          // Handle errors
          Serial.println("Failed to store sensor data in Firebase");
          Serial.println("ERROR: " + fbdo.errorReason());
        }

        /* ThingSpeak */
        // set the fields with the values
        /* if (watering_for != 0) {
        double volume = calculateWaterVolume(DIAMETER, LENGHT, int(watering_for / 1000));
        ThingSpeak.setField(1, int(volume));
      }
      ThingSpeak.setField(2, temperature);
      ThingSpeak.setField(3, humidity);
      ThingSpeak.setField(4, int(event.light));
      ThingSpeak.setField(5, soil_moisture);
      // write to the ThingSpeak channel
      int x = ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);
      if (x == 200) {
        Serial.println("Channel update successful.");
      } else {
        Serial.println("Problem updating channel. HTTP error code " + String(x));
      }

      if (watering_for != 0) {
        double volume = calculateWaterVolume(DIAMETER, LENGHT, int(watering_for / 1000));
        ThingSpeak.setField(1, int(volume));
        int water_channel = ThingSpeak.writeFields(myWaterChannelNumber, myWaterWriteAPIKey);
        if (water_channel == 200) {
          Serial.println("Water Channel update successful.");
        } else {
          Serial.println("Problem updating water channel. HTTP error code " + String(water_channel));
        }
        watering_for = 0;
      } */
        /* END ThingSpeak */
      }
    }
  } else {
    //WiFi.disconnect();
    //setup_wifi();

    Serial.println("A: No. Trying to reconnect now . . . ");
    WiFi.reconnect();
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
      tries++;
      if (tries >= 50) {

        Serial.println("Impossible reconnecting to WiFi, rebooting device!");
        ESP.restart();
      }
    }
    Serial.println("");
    Serial.print("Reconnected to wifi with Local IP:");
    Serial.println(WiFi.localIP());
  }

  if (!Firebase.ready()) {

    Serial.println("Reconnecting to Firebase");
    Firebase.begin(&config, &auth);  // Reinitialize Firebase connection
  }
}

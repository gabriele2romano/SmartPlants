#include <WiFi.h>
#include <PubSubClient.h>
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
#include <WebServer.h>

Preferences preferences;  //to save in non volatile memory
String pref_namespace = "credentials";

const char *ssid = "";
const char *password = "";
String user_id = "";

#define ID_NUMBER "20000000"
#define TYPE "plant_sensor"  //type of device

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

String plant = "";              //"mimosa pudica";
int delay_readings = 120000;    //3600000;
int delay_moist_read = 240000;  //3600000;
int watering_time_cost = 3600000;
int room_number = -1;

#define DHT_delay 500
#define uS_TO_S_FACTOR 1000000 /* Conversion factor for micro seconds to seconds */
#define mS_TO_S_FACTOR 1000    /* Conversion factor for micro seconds to seconds */
#define TIME_TO_SLEEP 20       /* Time ESP32 will go to sleep (in seconds) */

// Define the gravitational constant (m/s^2)
#define GRAVITY 9.81
#define DIAMETER 0.003  // - diameter: Diameter of the tube in meters
#define LENGHT 1.0      // - length: Length of the tube in meters

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
const char *mqtt_server = "3350af7c3849438980ed8035e0dca9a9.s1.eu.hivemq.cloud";  //"mqtt-dashboard.com";

String topic;                               //topic used to send sensors data
String debug_topic = "smart_plants_debug";  // topic used for debug messages. Ex: sensors not working properly.
//String status_topic;                        //topic used to send which parameters are not well for the plant

String tmp;
String tmp1;
String tmp2;
int tmp3;
const int maxTries = 50;

WiFiClientSecure espClient;  //WiFiClient espClient;
//WiFiClient thingSpeakClient;
PubSubClient client(espClient);
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

bool wifi_set = false;
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

    //WiFi.mode(WIFI_STA); //added
    //WiFi.setAutoReconnect(true); //added
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
      user_id = _user_id;
      if (WiFi.getSleep() == true) {
        WiFi.setSleep(false);
      }
      server.send(200, "text/json", "{\"id_number\":\"" + String(ID_NUMBER) + "\",\"type\":\"" + String(TYPE) + "\",\"msg\":\"Connected successfully! ESP32 is now online.\"}");
      wifi_set = true;
    } else {
      Serial.println("\nWiFi connection failed!");
      server.send(200, "text/html", "Wi-Fi connection failed. Please try again.");
    }
  } else {
    server.send(400, "text/html", "Invalid input.");
  }
}


/* void handleReset() {
  Serial.println("Resetting now");
  server.send(200, "text/html", "Esp32 is resetting now");
  esp_restart();
} */

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
  //server.on("/reset", HTTP_POST, handleReset);

  // Start the server
  server.begin();
  Serial.println("Server started");

  while (!wifi_set) {  //WiFi.status() != WL_CONNECTED) {
    // Handle incoming client requests
    server.handleClient();
  }

  server.stop();
  Serial.println("Server stopped");
}

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

void callback(char *s_topic, byte *payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(s_topic);
  Serial.print("] ");

  // Convert payload to a string
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.println(message);

  if (strcmp(s_topic, debug_topic.c_str()) == 0) {
    // Parse JSON
    StaticJsonDocument<256> jsonDoc;  // Adjust size based on expected JSON payload
    DeserializationError error = deserializeJson(jsonDoc, message);

    if (error) {
      Serial.print("Failed to parse JSON: ");
      Serial.println(error.c_str());
      return;
    }
    // Extract values from the JSON
    const char *id = jsonDoc["id"];

    Serial.println("Extracted values:");
    Serial.print("ID: ");
    Serial.println(id);

    // Compare ID with ID_NUMBER
    if (strcmp(id, ID_NUMBER) == 0) {
      Serial.println("ID matches. Saving values to preferences.");

      display_pid = String(jsonDoc["plant"]);
      String tmp_plant = display_pid;
      tmp_plant.replace(" ", "%20");
      Serial.println(tmp_plant);
      Serial.println(plant);
      if (tmp_plant != plant) {
        makeGetRequest(server_c + tmp_plant + "/");
      }

      delay_readings = jsonDoc["delay_readings"];
      delay_moist_read = jsonDoc["delay_moist_read"];
      watering_time_cost = jsonDoc["watering_time_cost"];
      room_number = jsonDoc["room_number"];
      // Save values to preferences
      preferences.begin(pref_namespace.c_str(), false);  // Namespace: "plant_data"
      preferences.putString("plant", display_pid);
      preferences.putInt("delay_readings", delay_readings);
      preferences.putInt("delay_moist_read", delay_moist_read);
      preferences.putInt("watering_time_cost", watering_time_cost);
      preferences.putInt("room_number", room_number);
      preferences.end();

      Serial.print("Plant: ");
      Serial.println(display_pid);
      Serial.print("Delay readings: ");
      Serial.println(delay_readings);
      Serial.print("Delay moist readings: ");
      Serial.println(delay_moist_read);
      Serial.print("Watering time cost: ");
      Serial.println(watering_time_cost);
      Serial.print("Room Number: ");
      Serial.println(room_number);
      Serial.println("Values saved successfully.");
    } else {
      Serial.println("ID does not match.");
    }
  } else if (strcmp(s_topic, topic.c_str()) == 0) {

    // Check if the message is "1-shut_down"
    if (message == String(ID_NUMBER) + "-restart") {
      // Reset the ESP32-C3
      tmp = "Resetting now...";
      Serial.println(tmp);
      //client.publish(debug_topic.c_str(), tmp.c_str());
      esp_restart();
    } else if (message == String(ID_NUMBER) + "-open") {
      // Reset the ESP32-C3
      tmp = "Opening Valve";
      openPump();
      //client.publish(debug_topic.c_str(), tmp.c_str());
    } else if (message == String(ID_NUMBER) + "-close") {
      // Reset the ESP32-C3
      tmp = "Closing Valve";
      closePump();
      //client.publish(debug_topic.c_str(), tmp.c_str());
    } else if (message == String(ID_NUMBER) + "-clear_preferences") {
      Serial.print("Preferences cleared");
      preferences.begin(pref_namespace.c_str(), false);
      preferences.clear();
      preferences.end();
    } else if (message == String(ID_NUMBER) + "-clear_data_preferences") {
      Serial.print("Data Preferences cleared");
      preferences.begin(pref_namespace.c_str(), false);
      preferences.remove("plant");
      preferences.remove("interval");
      preferences.end();
    }
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
    
    String willMessage = String(ID_NUMBER) + ":offline";
    if (client.connect(clientId.c_str(), MQTT_USER, MQTT_PASSWORD,debug_topic.c_str(),1,false,willMessage.c_str())) {
      Serial.println("connected");
      client.subscribe(debug_topic.c_str(),1);
      String on_mess = String(ID_NUMBER) + ":online";
      client.publish(debug_topic.c_str(), on_mess.c_str());
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

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

bool open_p = false;
void openPump() {
  if (!open_p) {
    Serial.println("Opened Pump");
    //WebSerial.println("Opened Pump");
    digitalWrite(solenoid_pin, HIGH);  //Switch Solenoid ON
    open_p = true;
  } else {
    Serial.println("Pump already opened");
    //WebSerial.println("Pump already opened");
  }
}

void closePump() {
  if (open_p) {
    Serial.println("Closed Pump");
    //WebSerial.println("Closed Pump");
    digitalWrite(solenoid_pin, LOW);  //Switch Solenoid OFF
    open_p = false;
  } else {
    Serial.println("Pump already closed");
    //WebSerial.println("Pump already closed");
  }
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

void makeGetRequest(String s) {
  WiFiClientSecure client_s;
  client_s.setInsecure();  // Use this only if you don't need SSL verification
  // Alternatively, use client.setCACert() to set a specific root CA certificate.

  HTTPClient http;

  if (http.begin(client_s, s)) {
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

int temperature = 0;
int humidity = 0;
void setup() {
  Serial.begin(115200);
  //pinMode(LED, OUTPUT);           // Initialize the BUILTIN_LED pin as an output
  //digitalWrite(LED, HIGH);        // turn off led
  pinMode(solenoid_pin, OUTPUT);  //Sets the solenoid pin as an output
  topic = "smart_plants";         //"smart_plants/" + String(ID_NUMBER);

  /* Check flash memory Wifi */
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
  preferences.end();
  /* END Check Flash Memory */

  /* ThingSpeak Setup */
  //ThingSpeak.begin(thingSpeakClient);  // Initialize ThingSpeak
  /* END ThingSpeak Setup */

  client.setServer(mqtt_server, 8883);  //Initialize server mqtt
  client.setCallback(callback);         //set how to handle messages
  espClient.setInsecure();

  /* Check flash memory Data  */
  preferences.begin(pref_namespace.c_str(), false);
  tmp = preferences.getString("plant", "");
  display_pid = tmp.c_str();
  tmp3 = preferences.getInt("delay_readings", delay_readings);
  delay_readings = tmp3;
  tmp3 = preferences.getInt("delay_moist_read", delay_moist_read);
  delay_moist_read = tmp3;
  tmp3 = preferences.getInt("watering_time_cost", watering_time_cost);
  watering_time_cost = tmp3;
  tmp3 = preferences.getInt("room_number", room_number);
  room_number = tmp3;
  Serial.println("Waiting for mqtt config message");

  //wait for mqtt message

  while (display_pid == "") {
    if (WiFi.status() == WL_CONNECTED) {  //Connected to WiFi
      if (!client.connected()) {
        reconnect();
      }
      client.loop();
    }
  }
  Serial.println("Retrieved plant: " + String(display_pid));

  preferences.end();
  /* Check flash memory Data END */

  /* DB */
  plant = display_pid;
  plant.replace(" ", "%20");
  String server_c_temp = server_c + plant + "/";
  Serial.println(server_c_temp);

  makeGetRequest(server_c_temp);
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
    } else if (command == "clear_data_preferences") {
      Serial.print("Data Preferences cleared");
      preferences.begin(pref_namespace.c_str(), false);
      preferences.remove("plant");
      preferences.remove("delay_readings");
      preferences.remove("delay_moist_read");
      preferences.remove("watering_time_cost");
      preferences.remove("room_number");
      preferences.end();
    } else {
      Serial.println("Unknown command");
    }
  }
}

void loop() {
  manage_serial_commands();
  if (WiFi.status() == WL_CONNECTED) {  //Connected to WiFi
    if (!client.connected()) {
      reconnect();
    }
    client.loop();

    //if (Firebase.ready()) {
    unsigned long now = millis();  //esp_timer_get_time(); may work better han millis
    /* if (now - sendDataPrevMillis > delay_firebase || sendDataPrevMillis == 0) {
        Serial.println("Heap:");
        Serial.println(ESP.getFreeHeap());  //handle auth task firebase
        sendDataPrevMillis = millis();
        fetchFirebaseVariables();
      } */

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

      tmp = "{\"user_id\":\"" + user_id + "\",\"device_id\":\"" + ID_NUMBER + "\",\"plant\": \"" + display_pid + "\",\"room\":\"" + room_number + "\",\"plant_img\": \"" + image_url.c_str() + "\", \"watering_time\": \"" + String(int(watering_for / 1000)) + "\",\"sensors\": {\"soil_moisture\":\"" + String(soil_moisture) + "\", \"temperature\": \"" + String(temperature) + "\", \"humidity\": \"" + String(humidity) + "\", \"light\": \"" + String(event.light) + "\"},\"message\": \"" + mess + "\"}";


      Serial.print("Publish message: ");
      Serial.println(tmp.c_str());
      client.publish(topic.c_str(), tmp.c_str());

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
    //}
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
}

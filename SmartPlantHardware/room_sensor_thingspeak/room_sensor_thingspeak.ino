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
#include <WebServer.h>

Preferences preferences;  //to save in non volatile memory
String pref_namespace = "credentials";

const char *ssid = "";
const char *password = "";
String user_id = "";


#define ID_NUMBER "30000001"  //act like serial id
#define TYPE "room_sensor"    //type of device

/* WebServer */
// Create an instance of the server
WebServer server(80);
// Define base SSID
String ssid_ap_base = "ESP32_SmartPlants_AP_";
String ssid_ap;
/* END WebServer */

#define dht11_pin 26  //10//
#define solenoid_pin 21
//#define LED 2

int delay_readings = 120000;  //3600000;
int room_number = -1;

#define DHT_delay 500
#define uS_TO_S_FACTOR 1000000 /* Conversion factor for micro seconds to seconds */
#define mS_TO_S_FACTOR 1000    /* Conversion factor for micro seconds to seconds */
#define TIME_TO_SLEEP 20       /* Time ESP32 will go to sleep (in seconds) */

/* ThingSpeak Credentials*/
unsigned long myChannelNumber = SECRET_CH_ID;
const char *myWriteAPIKey = SECRET_WRITE_APIKEY;
/* END ThingSpeak Credentials*/

/* Sensors */
DHT11 dht11(dht11_pin);
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
/* END Sensors */

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

/* Useful Vars*/
/* END Useful Vars*/

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

      delay_readings = jsonDoc["delay_readings"];
      room_number = jsonDoc["room_number"];
      // Save values to preferences
      preferences.begin(pref_namespace.c_str(), false);  // Namespace: "plant_data"
      preferences.putInt("delay_readings", delay_readings);
      preferences.putInt("room_number", room_number);
      preferences.end();

      Serial.print("Delay readings: ");
      Serial.println(delay_readings);
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
    if (client.connect(clientId.c_str(), MQTT_USER, MQTT_PASSWORD)) {
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
}

//LED control
/* void ledON() {
  Serial.println("LED ON");
  digitalWrite(LED, LOW);
}

void ledOFF() {
  Serial.println("LED OFF");
  digitalWrite(LED, HIGH);
} */
//END LED control



int temperature = 0;
int humidity = 0;
void setup() {
  Serial.begin(115200);
  topic = "smart_plants";         //"smart_plants/#" + String(randNumber);
  pinMode(solenoid_pin, OUTPUT);  //Sets the solenoid pin as an output

  /* Check flash memory Wifi */
  preferences.begin(pref_namespace.c_str(), false);
  tmp = preferences.getString("ssid", "");
  ssid = tmp.c_str();
  tmp1 = preferences.getString("password", "");
  password = tmp1.c_str();
  tmp2 = preferences.getString("user_id", "");
  user_id = tmp2.c_str();
  Serial.println("Retrieved SSID: " + String(ssid));
  Serial.println("Retrieved USER ID: " + String(user_id));
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
  /* ThingSpeak.begin(thingSpeakClient);  // Initialize ThingSpeak*/
  /* END ThingSpeak Setup */

  client.setServer(mqtt_server, 8883);  //Initialize server mqtt
  client.setCallback(callback);         //set how to handle messages
  espClient.setInsecure();


  /* Check flash memory Data  */
  preferences.begin(pref_namespace.c_str(), false);
  tmp3 = preferences.getInt("delay_readings", delay_readings);
  delay_readings = tmp3;
  tmp3 = preferences.getInt("room_number", room_number);
  room_number = tmp3;
  Serial.println("Waiting for mqtt config message");

  //wait for mqtt message

  while (room_number == -1) {
    if (WiFi.status() == WL_CONNECTED) {  //Connected to WiFi
      if (!client.connected()) {
        reconnect();
      }
      client.loop();
    }
  }

  preferences.end();
  /* Check flash memory Data END */

  /* Sensor setup */
  //dht11.setDelay(delay_readings);  // Set this to the desired reading delay. Default is 500ms.
  tmp = "ALL SET!";
  Serial.println(tmp);
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
      preferences.remove("delay_readings");
      preferences.remove("room_number");
      preferences.end();
    } else if (command == "open") {
      openPump();
    } else if (command == "close") {
      closePump();
    } else {
      Serial.println("Unknown command");
    }
  }
}

void loop() {
  manage_serial_commands();
  //Serial.println("WORKING");

  if (WiFi.status() == WL_CONNECTED) {  //Connected to WiFi
    if (!client.connected()) {
      reconnect();
    }
    client.loop();

    //if (Firebase.ready()) {  //handle auth task firebase

      unsigned long now = millis();
      /* if (now - sendDataPrevMillis > delay_firebase || sendDataPrevMillis == 0) {
        Serial.println("Heap:");
        Serial.println(ESP.getFreeHeap());  //handle auth task firebase
        sendDataPrevMillis = millis();
        fetchFirebaseVariables();
      } */

      if (now - lastMsg > (delay_readings - DHT_delay) || lastMsg == 0) {
        //check firebase
        //fetchFirebaseVariables();

        //Serial.println("WORKING TO PUBLUSH");
        lastMsg = now;
        // Attempt to read the temperature and humidity values from the DHT11 sensor.
        int result = dht11.readTemperatureHumidity(temperature, humidity);
        if (result != 0) {
          // Print error message based on the error code.
          Serial.println(DHT11::getErrorString(result));
        }

        snprintf(msg, MSG_BUFFER_SIZE, "{\"user_id\":\"%s\",\"device_id\":\"%s\",\"room\": \"%ld\",\"sensors\": {\"temperature\": \"%ld\", \"humidity\": \"%ld\"}}", user_id.c_str(),String(ID_NUMBER),room_number, temperature, humidity);
        Serial.print("Publish message: ");
        Serial.println(msg);
        //client.publish(smart_mirror_topic.c_str(), msg);

        /* ThingSpeak */
        /* ThingSpeak.setField(2, temperature);
        ThingSpeak.setField(3, humidity);
        // write to the ThingSpeak channel
        int x = ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);
        if (x == 200) {
          Serial.println("Channel update successful.");
        } else {
          Serial.println("Problem updating channel. HTTP error code " + String(x));
        } */
        /* END ThingSpeak */
      //}
    }
  } else {
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
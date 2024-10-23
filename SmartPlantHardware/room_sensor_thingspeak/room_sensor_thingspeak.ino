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
#include "ThingSpeak.h"  // always include thingspeak header file after other header files and custom macros
#include <Firebase_ESP_Client.h>
#include <addons/TokenHelper.h>  // Provide the token generation process info.
#include <addons/RTDBHelper.h>   // Provide the RTDB payload printing info and other helper functions.
#include <WebServer.h>

Preferences preferences;  //to save in non volatile memory
String pref_namespace = "credentials";

const char* ssid = "";
const char* password = "";

//#define LED 2
#define delay_readings 5000  //60000

#define DHT_delay 500
#define uS_TO_S_FACTOR 1000000 /* Conversion factor for micro seconds to seconds */
#define mS_TO_S_FACTOR 1000    /* Conversion factor for micro seconds to seconds */
#define TIME_TO_SLEEP 20       /* Time ESP32 will go to sleep (in seconds) */

/* Firebase Credentials */
const char* myDatabaseUrl = FIREBASE_URL;
const char* myFirebaseKey = FIREBASE_KEY;

// Insert Authorized Email and Corresponding Password
const char* user_mail = "esp@smartplants.com";
const char* user_password = "espdefault";

String user_id = "";

// Define Firebase Data object
FirebaseData fbdo;

FirebaseAuth auth;
FirebaseConfig config;
/* END Firebase Credentials */

/* ThingSpeak Credentials*/
unsigned long myChannelNumber = SECRET_CH_ID;
const char* myWriteAPIKey = SECRET_WRITE_APIKEY;
/* END ThingSpeak Credentials*/


const int sensor_number = 3;

String server_c = "https://open.plantbook.io/api/v1/plant/detail/";

/* Sensors */
DHT11 dht11(26);
/* END Sensors */

/* Wifi */

// Define the web server on port 80
/* WebServer server(80);

// Flag to check if a packet has been received
bool packetReceived = false; */

const char* mqtt_server = "mqtt-dashboard.com";

String id_number = "00000001";
int room_number = 1;
String topic;
String debug_topic = "smart_plants_debug";
String smart_mirror_topic = "smart_mirror";

String tmp;
String tmp1;
const int maxTries = 50;

String ip="";

WiFiClient espClient;
WiFiClient thingSpeakClient;
PubSubClient client(espClient);
unsigned long lastMsg = 0;
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

/* void handleCredentials() {
  if (server.hasArg("user_id")) {
    user_id = server.arg("user_id");
    Serial.println("Received User ID" + user_id);

    preferences.putString("user_id", user_id);

    // Here you can save these credentials to memory or use them immediately
    server.send(200, "text/plain", "Credentials received");
  } else {
    server.send(400, "text/plain", "Missing credentials");
  }
  server.stop();
} */
// Function to handle incoming request
/* void handleRequest() {
  if (!packetReceived) {
    // Respond to the client
    server.send(200, "text/plain", "Packet received!");
    Serial.println("Packet received");

    // Mark that a packet has been received
    packetReceived = true;

    // Stop the server to ensure only one packet is received
    server.stop();
    Serial.println("Server stopped after receiving one packet.");
  } else {
    // If another request is made, respond with an error
    server.send(403, "text/plain", "Server is no longer accepting requests.");
  }
} */


void smartconfig_setup_wifi() {
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

  Serial.print("Password: ");
  Serial.println(WiFi.psk());
  preferences.putString("password", WiFi.psk());

  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  ip = String(WiFi.localIP());
  if (WiFi.getSleep() == true) {
    WiFi.setSleep(false);
  }
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
  ip = String(WiFi.localIP());
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
  fbdo.setResponseSize(2048);

  Firebase.begin(&config, &auth);

  Firebase.setDoubleDigits(5);

  config.timeout.serverResponse = 10 * 1000;

  // You can use TCP KeepAlive in FirebaseData object and tracking the server connection status, please read this for detail.
  // https://github.com/mobizt/Firebase-ESP-Client#about-firebasedata-object
  fbdo.keepAlive(5, 5, 1);

  //Network reconnect timeout (interval) in ms (10 sec - 5 min) when network or WiFi disconnected.
  config.timeout.networkReconnect = 10 * 1000;

  //Network reconnect timeout (interval) in ms (10 sec - 5 min) when network or WiFi disconnected.
  //config.timeout.networkReconnect = 10 * 1000;

  //Socket connection and SSL handshake timeout in ms (1 sec - 1 min).
  //config.timeout.socketConnection = 10 * 1000;

  //Server response read timeout in ms (1 sec - 1 min).
  //config.timeout.serverResponse = 10 * 1000;

  //RTDB Stream keep-alive timeout in ms (20 sec - 2 min) when no server's keep-alive event data received.
  //config.timeout.rtdbKeepAlive = 45 * 1000;

  //RTDB Stream reconnect timeout (interval) in ms (1 sec - 1 min) when RTDB Stream closed and want to resume.
  //config.timeout.rtdbStreamReconnect = 1 * 1000;

  //RTDB Stream error notification timeout (interval) in ms (3 sec - 30 sec). It determines how often the readStream
  //will return false (error) when it called repeatedly in loop.
  //config.timeout.rtdbStreamError = 3 * 1000;
}

void callback(char* topic, byte* payload, unsigned int length) {
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
  if (message == id_number + "-restart") {
    // Reset the ESP32-C3
    tmp = "Resetting now...";
    Serial.println(tmp);
    client.publish(debug_topic.c_str(), tmp.c_str());
    esp_restart();
  } else if (message == id_number + "-clear_preferences") {
    Serial.print("Preferences cleared");
    preferences.begin(pref_namespace.c_str(), false);
    preferences.clear();
    preferences.end();
  } else if (message.indexOf(ip) != -1) {
    user_id = message.substring(message.indexOf(ip) + 1);
    Serial.print("Received uid:" +user_id);
  }
}

void reconnect() {
  // Loop until we're reconnected
  Serial.println("Reconnection..to MQTT Server");
  unsigned int c = 0;
  while (!client.connected() && c < 10) {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "Room-" + String(room_number);
    // Attempt to connect
    client.setKeepAlive(90);  // setting keep alive to 90 seconds
    client.setBufferSize(512);
    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
      client.subscribe(debug_topic.c_str());
      client.subscribe(topic.c_str());
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
    c++;
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
  //pinMode(LED, OUTPUT);         // Initialize the BUILTIN_LED pin as an output
  //digitalWrite(LED, HIGH);      //turn off led
  topic = "smart_plants/#";  // + String(randNumber);

  /* Check flash memory */
  preferences.begin(pref_namespace.c_str(), false);
  tmp = preferences.getString("ssid", "");
  ssid = tmp.c_str();
  tmp1 = preferences.getString("password", "");
  password = tmp1.c_str();
  Serial.println("Retrieved SSID: " + String(ssid));
  if (strcmp(ssid, "") == 0 || strcmp(password, "") == 0) {
    smartconfig_setup_wifi();
  } else {
    if (!setup_wifi()) {
      Serial.println("Failed connecting to " + String(ssid));
      smartconfig_setup_wifi();
    }
  }
  /* END Check Flash Memory */

  //setup_firebase();

  /* Web Server */
  // Start web server and define route to receive credentials
  /* server.on("/send-credentials", HTTP_POST, handleCredentials);
  server.begin(); */
  //server.handleClient();

  // Start the web server and set the route
  /* server.on("/", HTTP_GET, handleRequest);
  server.begin();
  Serial.println("Server started. Waiting for packet..."); */
  /* END Web Server */

  /* ThingSpeak Setup */
  ThingSpeak.begin(thingSpeakClient);  // Initialize ThingSpeak

  client.setServer(mqtt_server, 1883);  //Initialize server mqtt
  client.setCallback(callback);         //set how to handle messages
  /* END ThingSpeak Setup */

  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  /* Sensor setup */
  //dht11.setDelay(delay_readings); // Set this to the desired reading delay. Default is 500ms.

  /* First Cycle Sensors*/
  reconnect();
  // Attempt to read the temperature and humidity values from the DHT11 sensor.
  int result = dht11.readTemperatureHumidity(temperature, humidity);
  if (result != 0) {
    // Print error message based on the error code.
    Serial.println(DHT11::getErrorString(result));
  }


  snprintf(msg, MSG_BUFFER_SIZE, "{\"room\": 1,\"sensors\": {\"temperature\": \"%ld\", \"humidity\": \"%ld\"}}", temperature, humidity);
  Serial.print("Publish message: ");
  Serial.println(msg);
  client.publish(smart_mirror_topic.c_str(), msg);

  /* ThingSpeak */
  ThingSpeak.setField(2, temperature);
  ThingSpeak.setField(3, humidity);
  // write to the ThingSpeak channel
  int x = ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);
  if (x == 200) {
    Serial.println("Channel update successful.");
  } else {
    Serial.println("Problem updating channel. HTTP error code " + String(x));
  }
  /* END ThingSpeak */
  /* END First Cycle Sensors*/

  tmp = "ALL SET!";
  Serial.println(tmp);
  client.publish(debug_topic.c_str(), tmp.c_str());
  /* END Sensor setup */
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

void loop() {
  manage_serial_commands();
  //Serial.println("WORKING");
  if (WiFi.status() == WL_CONNECTED) {  //Connected to WiFi
    if (!client.connected()) {
      reconnect();
    }
    client.loop();

    /* if (user_id == "") {
      server.handleClient();  // Listen for HTTP requests
    } */
    /* if (!packetReceived) {
      server.handleClient();  // Listen for incoming clients
    } */

    if (Firebase.ready()) {  //handle auth task firebase

      unsigned long now = millis();
      if (now - lastMsg > (delay_readings - DHT_delay)) {
        lastMsg = now;
        // Attempt to read the temperature and humidity values from the DHT11 sensor.
        int result = dht11.readTemperatureHumidity(temperature, humidity);
        if (result != 0) {
          // Print error message based on the error code.
          Serial.println(DHT11::getErrorString(result));
        }


        snprintf(msg, MSG_BUFFER_SIZE, "{\"room\": 1,\"sensors\": {\"temperature\": \"%ld\", \"humidity\": \"%ld\"}}", temperature, humidity);
        Serial.print("Publish message: ");
        Serial.println(msg);
        client.publish(smart_mirror_topic.c_str(), msg);

        /* ThingSpeak */
        ThingSpeak.setField(2, temperature);
        ThingSpeak.setField(3, humidity);
        // write to the ThingSpeak channel
        int x = ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);
        if (x == 200) {
          Serial.println("Channel update successful.");
        } else {
          Serial.println("Problem updating channel. HTTP error code " + String(x));
        }
        /* END ThingSpeak */
      }
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
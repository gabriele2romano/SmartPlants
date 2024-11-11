#include <WiFi.h>
//#include <PubSubClient.h>
#include <DHT11.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_TSL2561_U.h>
#include "esp_system.h"
//#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>  //for saving data built in
#include <ArduinoJson.h>
#include "Keys.h"
//#include "ThingSpeak.h"  // always include thingspeak header file after other header files and custom macros
#include <Firebase_ESP_Client.h>
#include <addons/TokenHelper.h>  // Provide the token generation process info.
#include <addons/RTDBHelper.h>   // Provide the RTDB payload printing info and other helper functions.
#include <WebServer.h>
#include <time.h>  //for time stamp

/* NTP Timestamp */
// NTP server to get time
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 3600;      // Adjust this according to your timezone
const int daylightOffset_sec = 3600;  // No daylight saving adjustment
/* END NTP Timestamp */

Preferences preferences;  //to save in non volatile memory
String pref_namespace = "credentials";

const char* ssid = "";
const char* password = "";
String user_id = "";


#define ID_NUMBER "00000001"  //act like serial id
#define TYPE "room_sensor"    //type of device

int room_number = 1;
/* WebServer */
// Create an instance of the server
WebServer server(80);
// Define base SSID
String ssid_ap_base = "ESP32_SmartPlants_AP_";
String ssid_ap;
/* END WebServer */

//#define LED 2
//#define delay_readings 10000  //60000
#define delay_firebase 30000  //60000
int delay_readings = 3600000;

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

/* Sensors */
DHT11 dht11(26);
#define solenoid_pin 21
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

// Define the web server on port 80
/* WebServer server(80);

// Flag to check if a packet has been received
bool packetReceived = false; */

/* const char* mqtt_server = "mqtt-dashboard.com";
String topic;
String debug_topic = "smart_plants_debug";
String smart_mirror_topic = "smart_mirror"; */

String tmp;
String tmp1;
String tmp2;
const int maxTries = 50;

//WiFiClient espClient;
//WiFiClient thingSpeakClient;
//PubSubClient client(espClient);
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
      server.send(200, "text/html", "{\"id_number\":\"" + String(ID_NUMBER) + "\",\"type\":\"" + String(TYPE) + "\"}");  //"Connected successfully! ESP32 is now online.");
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

  Serial.print("Password: ");
  Serial.println(WiFi.psk());
  preferences.putString("password", WiFi.psk());

  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  ip = String(WiFi.localIP());
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
  //config.timeout.networkReconnect = 10 * 1000;

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

/* void callback(char* topic, byte* payload, unsigned int length) {
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
  } else if (message == String(ID_NUMBER) + "-clear_preferences") {
    Serial.print("Preferences cleared");
    preferences.begin(pref_namespace.c_str(), false);
    preferences.clear();
    preferences.end();
  } else if (message.indexOf(ip) != -1) {
    user_id = message.substring(message.indexOf(ip) + 1);
    Serial.print("Received uid:" + user_id);
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
} */

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
  //topic = "smart_plants/#";  // + String(randNumber);
  pinMode(solenoid_pin, OUTPUT);  //Sets the solenoid pin as an output

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
  Serial.println("Retrieved User Id: " + String(user_id));
  /* END Check Flash Memory */

  /* NTP */
  // Initialize NTP
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  /* NTP */

  setup_firebase();
  //if (Firebase.ready()) fetchFirebaseVariables();

  /* ThingSpeak Setup */
  /* ThingSpeak.begin(thingSpeakClient);  // Initialize ThingSpeak

  client.setServer(mqtt_server, 1883);  //Initialize server mqtt
  client.setCallback(callback);         //set how to handle messages */
  /* END ThingSpeak Setup */

  /* client.setServer(mqtt_server, 1883);
  client.setCallback(callback); */

  tmp = "ALL SET!";
  Serial.println(tmp);
  /* Sensor setup */
  //dht11.setDelay(delay_readings); // Set this to the desired reading delay. Default is 500ms.

  /* First Cycle Sensors*/
  //reconnect();
  // Attempt to read the temperature and humidity values from the DHT11 sensor.
  /* int result = dht11.readTemperatureHumidity(temperature, humidity);
  if (result != 0) {
    // Print error message based on the error code.
    Serial.println(DHT11::getErrorString(result));
  }


  snprintf(msg, MSG_BUFFER_SIZE, "{\"room\": 1,\"sensors\": {\"temperature\": \"%ld\", \"humidity\": \"%ld\"}}", temperature, humidity);
  Serial.print("Publish message: ");
  Serial.println(msg); */
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
  /* END First Cycle Sensors*/

  //client.publish(debug_topic.c_str(), tmp.c_str());
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
    } else if (command == "open") {
      openPump();
    } else if (command == "close") {
      closePump();
    } else {
      Serial.println("Unknown command");
    }
  }
}

void fetchFirebaseVariables() {
  if (Firebase.RTDB.getInt(&fbdo, "/" + user_id + "/devices/" + String(ID_NUMBER) + "/interval")) {
    //Serial.print("Data type: ");
    //Serial.println(fbdo.dataType());

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
    Serial.println("FB Data Fetching failed");
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
  //Serial.println("WORKING");

  if (WiFi.status() == WL_CONNECTED) {  //Connected to WiFi
    /* if (!client.connected()) {
      reconnect();
    }
    client.loop(); */

    /* if (user_id == "") {
      server.handleClient();  // Listen for HTTP requests
    } */
    /* if (!packetReceived) {
      server.handleClient();  // Listen for incoming clients
    } */

    if (Firebase.ready()) {  //handle auth task firebase

      unsigned long now = millis();
      if (now - sendDataPrevMillis > delay_firebase || sendDataPrevMillis == 0) {
        Serial.println("Heap:");
        Serial.println(ESP.getFreeHeap());  //handle auth task firebase
        sendDataPrevMillis = millis();
        fetchFirebaseVariables();
      }

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


        snprintf(msg, MSG_BUFFER_SIZE, "{\"room\": \"%ld\",\"sensors\": {\"temperature\": \"%ld\", \"humidity\": \"%ld\"}}", room_number, temperature, humidity);
        Serial.print("Publish message: ");
        Serial.println(msg);
        //client.publish(smart_mirror_topic.c_str(), msg);


        String timestamp = getFormattedTime();  // Get the current timestamp
        // Prepare the JSON object for storing sensor data
        FirebaseJson json;
        json.set("/room", room_number);
        json.set("/temperature", temperature);
        json.set("/humidity", humidity);


        // Store the data in Firebase
        if (Firebase.RTDB.setJSON(&fbdo, "/" + user_id + "/devices/" + String(ID_NUMBER) + "/sensors/" + timestamp, &json)) {
          Serial.println("Sensor data stored successfully in Firebase");
        } else {
          // Handle errors
          Serial.println("Failed to store sensor data in Firebase");
          Serial.println("ERROR: " + fbdo.errorReason());
        }
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
  if (!Firebase.ready()) {

    Serial.println("Reconnecting to Firebase");
    Firebase.begin(&config, &auth);  // Reinitialize Firebase connection
  }
}
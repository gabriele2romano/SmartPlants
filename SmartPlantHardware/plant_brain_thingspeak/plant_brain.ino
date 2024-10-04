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
//#include "../WifiCredentials.h"
#include <Preferences.h>

Preferences preferences;  //to save in non volatile memory
const char* ssid = "";
const char* password = "";

#define LED 2
#define delay_readings 30000  //60000

#define DHT_delay 500
#define uS_TO_S_FACTOR 1000000 /* Conversion factor for micro seconds to seconds */
#define mS_TO_S_FACTOR 1000    /* Conversion factor for micro seconds to seconds */
#define TIME_TO_SLEEP 20       /* Time ESP32 will go to sleep (in seconds) */

const int sensor_number = 3;


String server_c = "https://open.plantbook.io/api/v1/plant/detail/";

/* Sensors */
DHT11 dht11(26);
String status_plant[sensor_number];
String param[sensor_number] = { "Temperature", "Humidity", "Sunlight" };
String status_description[sensor_number] = { " is too low.", " is normal.", " is too high." };
/* END Sensors */

/* Wifi */
const char* mqtt_server = "mqtt-dashboard.com";

int randNumber;
String topic;
String debug_topic = "smart_plants_debug";
String smart_mirror_topic = "smart_mirror";

String tmp;
int wifi_cell = 1;
const int maxTries = 50;

WiFiClient espClient;
PubSubClient client(espClient);
unsigned long lastMsg = 0;
#define MSG_BUFFER_SIZE (512)
char msg[MSG_BUFFER_SIZE];
/* END Wifi */

/* Useful Vars*/
DynamicJsonDocument doc(3000);
const char* messages_formal[sensor_number][sensor_number][sensor_number] = {
  { // status[0] == 0 (temperature too low)
    { "The temperature is too low and the environment is dry. Please consider relocating me to a warmer location with increased sunlight.",
      "The temperature is too low and the surroundings are dry. It would be beneficial to move me to a warmer area with more sunlight.",
      "The temperature is too low and the environment is dry. Additionally, it is overly bright. A relocation to a warmer and less bright location is recommended." },
    { "The temperature is low, and the environment is dry. Please move me to a location with higher temperatures and increased sunlight.",
      "The temperature is low, and while it is bright, the conditions are not ideal. A warmer location with more light would be preferable.",
      "The temperature is low, and the environment is both dry and excessively bright. Consider relocating me to a more suitable environment." },
    { "The temperature is low and the humidity is excessive. Please find a warmer location with more balanced conditions.",
      "The temperature is low and the environment is humid, though bright. A more comfortable and warmer location would be beneficial.",
      "The temperature is low, the environment is humid, and it is excessively bright. A move to a warmer and more balanced environment is recommended." } },
  { // status[0] == 1 (temperature normal)
    { "The temperature is ideal; however, the environment is too dry. Please consider using a humidifier.",
      "The temperature is ideal, but the conditions are too dry and insufficiently illuminated. Increasing both humidity and light would be advantageous.",
      "The temperature is ideal, but the environment is too dry and excessively bright. Consider using a humidifier to address the dryness." },
    { "The temperature and humidity levels are optimal. However, additional sunlight would be beneficial.",
      "All conditions are satisfactory. I am currently thriving in this environment.",
      "The temperature is ideal, but the brightness is excessive. Adjusting the lighting might enhance my comfort." },
    { "The temperature is ideal, but the humidity level is too high. Improving ventilation would be beneficial.",
      "The temperature is appropriate, but the environment is both humid and excessively bright. Enhancing ventilation and adjusting the lighting would be advantageous.",
      "The temperature is ideal, but the high humidity and excessive brightness suggest that improving ventilation and adjusting the lighting could enhance conditions." } },
  { // status[0] == 2 (temperature too high)
    { "The temperature is too high and the environment is dry. Relocating me to a cooler and more humid location would be ideal.",
      "The temperature is excessively high and the environment is dry. Additionally, it is bright. A move to a cooler location is recommended.",
      "The temperature is too high and the environment is overly bright. A cooler location would be preferable." },
    { "The temperature is too high; however, the humidity level is acceptable. Please find a cooler environment.",
      "The temperature is high and the surroundings are bright, though the humidity is adequate. A cooler environment would improve conditions.",
      "The temperature is too high and the brightness is excessive. A relocation to a cooler environment would be beneficial." },
    { "The temperature is too high and the humidity is also excessive. A move to a cooler and less humid environment is recommended.",
      "The temperature is high and the environment is humid, though bright. Relocating to a cooler environment would improve conditions.",
      "The temperature is too high, and the environment is excessively bright and humid. A transition to a cooler and more comfortable environment is advised." } }
};

const char* messages_friendly[sensor_number][sensor_number][sensor_number] = {
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
  preferences.putString("ssid",String(WiFi.SSID()));
  Serial.println("Saved "+preferences.getString("ssid","nada"));

  Serial.print("Password: ");
  Serial.println(WiFi.psk());
  preferences.putString("password",String(WiFi.psk()));

  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  if (WiFi.getSleep() == true) {
    WiFi.setSleep(false);
  }
}

void setup_wifi() {

  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  randomSeed(micros());

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  if (WiFi.getSleep() == true) {
    WiFi.setSleep(false);
  }
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
  if (message == "Brain-restart") {
    // Reset the ESP32-C3
    tmp = "Resetting now...";
    Serial.println(tmp);
    client.publish(debug_topic.c_str(), tmp.c_str());
    esp_restart();
  } else {
    // Parse the JSON response
    DeserializationError error = deserializeJson(doc, message);

    if (error) {
      Serial.print("deserializeJson() failed: ");
      Serial.println(error.c_str());
      return;
    }

    float light = doc["sensors"]["light"];
    int temperature = doc["sensors"]["temperature"];
    int humidity = doc["sensors"]["humidity"];
    int soil_moisture = doc["sensors"]["soil_moisture"];
    unsigned int status[sensor_number] = { doc["status"]["temperature"], doc["status"]["humidity"], doc["status"]["light"] };
    String plant = String(doc["plant"]);
    String plant_img = String(doc["plant_img"]);
    int watering_for = doc["watering_time"];

    /* Serial.println("Soil Moisture:"+String(soil_moisture));
    Serial.println("humidity:"+String(humidity));
    Serial.println("temperature:"+String(temperature));
    Serial.println("light:"+String(light));
    Serial.println("Watering for:"+String(watering_for)); */


    /* Handle Plant */
    String mess = "";  //"From " + plant + ": ";

    const char* message_m = messages_friendly[status[0]][status[1]][status[2]];
    mess += message_m;


    if (watering_for == -1 && status[1] == 2 && status[2] == 0) {
      mess += " My moisture is too wet!";
    }

    tmp = "{\"plant\": \"" + plant + "\",\"plant_img\": \"" + plant_img + "\", \"watering_time\": \"" + String(watering_for) + "\",\"sensors\": {\"soil_moisture\":\"" + String(soil_moisture) + "\", \"temperature\": \"" + String(temperature) + "\", \"humidity\": \"" + String(humidity) + "\", \"light\": \"" + String(light) + "\"},\"message\": \"" + mess + "\"}";

    /* END Handle Plant */

    client.publish(smart_mirror_topic.c_str(), tmp.c_str());
    //client.publish(smart_mirror_topic.c_str(), msg);
    Serial.print("Publish message: ");
    Serial.println(tmp);

    // Check the publish result
    int publishStatus = client.state();
    if (publishStatus != MQTT_CONNECTED) {
      Serial.print("Publish failed, error code: ");
      Serial.println(publishStatus);
    } /* else {
    Serial.println("Publish succeeded.");
} */
  }
}

void reconnect() {
  // Loop until we're reconnected
  Serial.println("Reconnection..to MQTT Server");
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "Brain-";
    clientId += String(randNumber, HEX);
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
  }
}

//LED control
void ledON() {
  Serial.println("LED ON");
  digitalWrite(LED, LOW);
}

void ledOFF() {
  Serial.println("LED OFF");
  digitalWrite(LED, HIGH);
}
//END LED control

void setup() {
  Serial.begin(115200);
  pinMode(LED, OUTPUT);         // Initialize the BUILTIN_LED pin as an output
  digitalWrite(LED, HIGH);      //turn off led
  randNumber = random(0xffff);  // random(256); //0 to 255
  topic = "smart_plants/#";     // + String(randNumber);

  //check nvm
  preferences.begin("credentials", false);
  ssid = preferences.getString("ssid", "").c_str();
  password = preferences.getString("password", "").c_str();
  Serial.println("Retrieved SSID"+String(ssid));
  if (strcmp(ssid,"")==0 || strcmp(password,"")==0) {
    smartconfig_setup_wifi();
  }
  else {
    setup_wifi();
  }
  //end check

  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  /* Sensor setup */
  //dht11.setDelay(delay_readings); // Set this to the desired reading delay. Default is 500ms.

  tmp = "ALL SET!";
  Serial.println(tmp);
  client.publish(debug_topic.c_str(), tmp.c_str());
  /* END Sensor setup */

  preferences.end();
}


int temperature = 0;
int humidity = 0;
void loop() {
  //Serial.println("WORKING");
  if (WiFi.status() == WL_CONNECTED) {  //Connected to WiFi
    if (!client.connected()) {
      reconnect();
    }
    client.loop();

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
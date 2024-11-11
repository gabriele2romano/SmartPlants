/***********Notice and Trouble shooting***************
1.Connection and Diagram can be found here
2.This code is tested on Arduino Uno.
****************************************************/
const int AirValue = 2505;    //you need to change this value that you had recordedintheair
const int WaterValue = 926;  //you need to change this value that you had recordedinthewater
int intervals = (AirValue - WaterValue) / 3;
int soilMoistureValue = 0;
void setup() {
  Serial.begin(115200);  // open serial port, set the baud rate to 9600 bps
}
void loop() {
  soilMoistureValue = analogRead(35);  //put Sensor insert into soil
  if (soilMoistureValue > WaterValue && soilMoistureValue < (WaterValue + intervals)) {
    Serial.println("Very Wet");
  } else if (soilMoistureValue > (WaterValue + intervals) && soilMoistureValue < (AirValue - intervals)) {
    Serial.println("Wet");
  } else if (soilMoistureValue < AirValue && soilMoistureValue > (AirValue - intervals)) {
    Serial.println("Dry");
  }
  delay(100);
}

/* void setup() {
Serial.begin(115200); // open serial port, set the baud rate to 9600 bps
}
void loop() {
int val;
val = analogRead(35); //connect sensor to Analog 0
Serial.println(val); //print the value to serial
delay(1000);
} */

//sm1 --2505 ---926

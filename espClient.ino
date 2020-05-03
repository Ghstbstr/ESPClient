/*
 * Author: Aaron Sowa
 * Version 1.0
 * Created on 05.8.2018
 * 
 */
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <FS.h>
#include <PubSubClient.h>
#include "arduino_secrets.h"
#include <ESP8266httpUpdate.h>

//Global Variables:
bool usbmode=true;
bool initializedRelayState=false;
String deviceName;
//WiFi-AP
const char* pass_AP = SECRET_PASS;    // Accesspoint passwort
bool deviceConfigurated;              //Check if device is configurated
//String configSSID, configPassword, configRoom, configName, configServerIP;
ESP8266WebServer server(80);
const char* mDNSAdress="ESP8266";

//firmwareUpdate

//WiFi-HomeNetwork
String ssid_net;                      // HomeNetwork SSID
String pass_net;                      // HomeNetwork passwort
String mqtt_server;                   // MQTT-Server on PI
String topic_Relais;
WiFiClient espClient;                 // Initializes 
PubSubClient client(espClient);

//GPIO
const uint16_t relay_pin=12;          //Power-Relais
const uint16_t debugLED_pin=LED_BUILTIN;//DebugLED
const uint16_t resetButton_pin=0;     //Reset-Button
//Modify for installed modules
//const uint16_t sensor1_pin=4;
//const uint16_t sensor2_pin;

//Timer
long startTimeISR, startTimeDebugLED;

//Sensors (Modify for installed modules)
//#define DHTTYPE DHT22
// Timers auxiliar variables
//uint32_t now = millis();
//uint32_t lastMeasure = 0;
//const uint16_t meassurmentTimer=30000;  //Mess every 30seks


//Setup
void setup() {
  if(usbmode){
    Serial.begin(115200);
    Serial.println("START"); 
  }  
  //get Device ID as a char*
  int deviceID=ESP.getChipId();
  deviceName="Aaron-ESP"+String(deviceID);
  deviceName.trim();
  Serial.println(deviceName);
    
  // Start the SPI Flash Files System 
  SPIFFS.begin();  
  
  //PinModes
  pinMode(debugLED_pin,OUTPUT);
  pinMode(resetButton_pin,INPUT_PULLUP);
  pinMode(relay_pin, OUTPUT);  
  delay(100);
  digitalWrite(debugLED_pin, HIGH);

  //Config Flag 
  EEPROM.begin(4);
  deviceConfigurated=EEPROM.read(0)==1;
  initializedRelayState=EEPROM.read(1)==1;  
  EEPROM.end();
  if(initializedRelayState){
    digitalWrite(relay_pin, HIGH);
  }  
  attachInterrupt(digitalPinToInterrupt(resetButton_pin),ISR_ResetButton,CHANGE);
  //Check if device is configurated
  if(!deviceConfigurated){
    createAP();
    waitForConfigOnAP();
  }  

  //if device got or is configurated
  loadConfig();
  connectToHomeNetwork();
  init_MQTT();  
  detachInterrupt(digitalPinToInterrupt(resetButton_pin));
  
}

//Run only in MQTT-Client Mode
void loop() {
  //Reconnect if nessesary
  if(WiFi.status()!= WL_CONNECTED){
    connectToHomeNetwork();
  }
  else if (!client.connected()) {
    init_MQTT();
  }  
  client.loop(); 
}

void loadConfig(){
  //Read Configuration
  File file=SPIFFS.open("/config.txt","r");
  ssid_net=file.readStringUntil('\n');
  ssid_net.trim();  
  pass_net=file.readStringUntil('\n');
  pass_net.trim();
  topic_Relais=file.readStringUntil('\n');
  topic_Relais.trim();
  mqtt_server=file.readStringUntil('\n');
  mqtt_server.trim();    
  file.close();
}

void connectToHomeNetwork(){  
  //Connect to HomeNetwork
  WiFi.begin(ssid_net.c_str(), pass_net.c_str());
  while (WiFi.status() != WL_CONNECTED){
    // Wait 1 seconds before retrying
    delay(1000);
  }
 
  for(int i=0;i<2;i++){
    digitalWrite(debugLED_pin, LOW);
    delay(250);
    digitalWrite(debugLED_pin, HIGH);    
    delay(250);
  }

  //MQTT-Client
  client.setServer(mqtt_server.c_str(), 1883);
  client.setCallback(callback);
}

void init_MQTT(){
  //MQTT  
  while (!client.connected()) {
    // Attempt to connect //Name for multiple devices
    if (client.connect(deviceName.c_str())) {
      //Relaiscontroll:
      client.subscribe(topic_Relais.c_str());
      //First Acknowledgment
      String temp=topic_Relais+"/ack"; 
      if(initializedRelayState) client.publish(temp.c_str(), "on");
      else  client.publish(temp.c_str(), "off");
      //Debug Message for Server
      temp="Device: "+topic_Relais+" is connected";
      client.publish("debugOutServer",temp.c_str());
      //Configuration Topic
      temp=topic_Relais+"/config";
      client.subscribe(temp.c_str());
      //subscribe to common Topic
      client.subscribe("common"); 
    } else {
      // Wait 1 seconds before retrying
      delay(1000); 
    }
  } 
  for(int i=0;i<2;i++){
    digitalWrite(debugLED_pin, LOW);
    delay(250);
    digitalWrite(debugLED_pin, HIGH);    
    delay(250);
  }  
}

//MQTT-Handler
void callback(char* topic_input, byte* message_input, unsigned int length) {
  yield();
  //parse byte-message to String
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)message_input[i];
  }  
  //Controll Relais
  String topicAcknowledgment=topic_Relais+"/ack";
  String topicConfig=topic_Relais+"/config";
  if((String)topic_input==topic_Relais){
      if(message == "on"){
        digitalWrite(debugLED_pin, LOW);
        digitalWrite(relay_pin, HIGH);
        client.publish(topicAcknowledgment.c_str(), "on");
      }
      else if(message == "off"){
        digitalWrite(debugLED_pin, HIGH);
        digitalWrite(relay_pin, LOW);
        client.publish(topicAcknowledgment.c_str(), "off");
      }
  }
  //Configurate Device
  else if((String)topic_input==topicConfig){
    if(message=="reset"){
      resetConfig();
    }
    else if(message=="patch"){
      otaUpdate();
    }
    else if(message=="initializedRelayStateOn"){
      initializedRelayState=true;
      setConfigFlag(1,1);
    }
    else if(message=="initializedRelayStateOff"){
      initializedRelayState=false;
      setConfigFlag(1,0);
    }
    else{
      configFile(ssid_net,pass_net,message,mqtt_server);
      ESP.restart();
    }
  }
  else if((String)topic_input=="common"){
    if(message=="summary"){      
      client.publish("serverSummary", (topic_Relais+"@"+deviceName+" with initializedRelayState="+initializedRelayState).c_str());
    }
    else if(message=="patchAll"){
      otaUpdate();
    }
  }     
}

void createAP(){
  // Create open network
  Serial.println(deviceName);
  WiFi.softAP(deviceName.c_str(), pass_AP);  
  server.on("/reset", HTTP_POST, handleReset);
  server.on("/config", HTTP_POST, handleConfig);
  server.onNotFound(handleRoot);
  // start the web server on port 80
  server.begin(); 
  // Start the mDNS responder
  MDNS.begin(mDNSAdress);
}

void handleRoot(){
  if (!handleFileRead(server.uri())){                  // send it if it exists
      server.send(404, "text/plain", "404: Not Found");// otherwise, respond with a 404 (Not Found)
  }
}

void handleReset(){
  server.send(200, "text/plain", "Configuration reseted");
  resetConfig();
}

// send the right file to the client (if it exists)
bool handleFileRead(String path){ 
  Serial.println("handleFileRead: " + path);
  if (path.endsWith("/")){
    path += "index.html";
    String temp;
    File file = SPIFFS.open(path, "r");
    temp=file.readString();
    file.close();
    file=SPIFFS.open("/config.txt", "r");
    String configSSID=file.readStringUntil('\n');
    configSSID.trim();
    String configPassword=file.readStringUntil('\n');
    configPassword.trim();
    String configRoom=file.readStringUntil('/');
    String configName=file.readStringUntil('\n');
    configName.trim();
    String configServerIP=file.readStringUntil('\n');
    configServerIP.trim();    
    file.close();
    temp.replace("SSID value=","SSID value=\""+configSSID+"\"");  
    temp.replace("Password value=","Password value=\""+configPassword+"\"");
    temp.replace("Room value=","Room value=\""+configRoom+"\"");
    temp.replace("Lamp value=","Lamp value=\""+configName+"\"");
    temp.replace("192.168.0.1 value=","192.168.0.1 value=\""+configServerIP+"\"");
    server.send(200, "text/html", temp);
    return true;
  }              
  else if (SPIFFS.exists(path)) { 
    // Get the MIME type      
    String contentType = getContentType(path);  
    File file = SPIFFS.open(path, "r");         
    size_t sent = server.streamFile(file, contentType); 
    file.close();                                       
    return true;    
  }
  // If the file doesn't exist, return false
  return false;                                         
}

void handleConfig(){
  if( ! server.hasArg("SSID") || ! server.hasArg("password") || ! server.hasArg("room") || ! server.hasArg("device-name") || ! server.hasArg("serverIP")
    || server.arg("SSID") == NULL || server.arg("password") == NULL || server.arg("room") == NULL || server.arg("device-name") == NULL || server.arg("serverIP") == NULL){
      server.send(400, "text/plain", "400: Invalid Request"); // The request is invalid, so send HTTP status 400
  }
  else{
    server.send(200, "text/plain", "Configurated");  
    delay(500);
    setConfigFlag(0,1);
    deviceConfigurated=true;
    
    String configSSID=server.arg("SSID");
    String configPassword=server.arg("password");
    String configRoom=server.arg("room");
    String configName=server.arg("device-name");
    String configServerIP=server.arg("serverIP");
    WiFi.softAPdisconnect();
    configFile(configSSID,configPassword,configRoom+"/"+configName,configServerIP);    
  }
}


//SPIFFS
String getContentType(String filename){ // convert the file extension to the MIME type
  if (filename.endsWith(".html")) return "text/html";
  else if (filename.endsWith(".css")) return "text/css";
  return "text/plain";
}

void waitForConfigOnAP(){
  startTimeDebugLED=millis();
  while(!deviceConfigurated){
    server.handleClient();    
    if(millis()-startTimeDebugLED>1000){
      if(digitalRead(debugLED_pin))digitalWrite(debugLED_pin, LOW);
      else digitalWrite(debugLED_pin, HIGH);
      startTimeDebugLED=millis();
    }
  }
}

void configFile(String configSSID, String configPassword, String configTopic, String configServerIP){
  File file=SPIFFS.open("/config.txt","w"); 
  file.println(configSSID);
  file.println(configPassword);
  file.println(configTopic);
  file.println(configServerIP);
  file.close();
}

void setConfigFlag(int index, int val){
  EEPROM.begin(4);
  if(EEPROM.read(index)!=val)EEPROM.write(index, val);
  EEPROM.commit();
  EEPROM.end();
}

void resetConfig(){
  setConfigFlag(0,0);
  setConfigFlag(1,0);
  File file=SPIFFS.open("/config.txt","w"); 
  file.println();
  file.close();
  ESP.restart();
}

void otaUpdate(){
  while(true){
    String URL="http://"+mqtt_server+":1880/update";
    t_httpUpdate_return ret = ESPhttpUpdate.update(URL);  
    switch (ret) {
        case HTTP_UPDATE_FAILED:
          Serial.printf("HTTP_UPDATE_FAILD Error (%d): %s", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
          break;
  
        case HTTP_UPDATE_NO_UPDATES:
          Serial.println("HTTP_UPDATE_NO_UPDATES");
          break;
  
        case HTTP_UPDATE_OK:
          Serial.println("HTTP_UPDATE_OK");
          break;
    }
    delay(100);
  }
}




void ISR_ResetButton(){
  if(!digitalRead(resetButton_pin)){
    startTimeISR=millis();
  }
  else{
    if(millis()-startTimeISR>3000){
      resetConfig();
    }
  }
}


//Sensorbibliothec (Modify for installed modules)
/*
void sensor_DHT_Init(){
  
}

void sensor_DHT_Mess(){
  
}
*/


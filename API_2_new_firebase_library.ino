#include "FirebaseESP8266.h"
//#include <FirebaseESP8266HTTPClient.h>
#include <FirebaseFS.h>
#include "FirebaseJson.h"

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <FS.h>
#include <Wire.h>
#include "SparkFunCCS811.h"
#include "SparkFunBME280.h"
#include <NTPClient.h>
#include <WiFiUdp.h>

#define CCS811_ADDR 0x5B
#define FIREBASE_HOST "biosense-android-version.firebaseio.com"
#define FIREBASE_AUTH "j5wY7VRmUaSVesMI77XZfzrz1obMAe2nux2UuqjV"

ESP8266WebServer server(80);
BME280 tempSens;
CCS811 co2Sens(CCS811_ADDR);

//Definitions for for Firebase elements

FirebaseData fbdo;
FirebaseJson co2Json, tvocJson, tempJson, humidJson, pressJson, totalJson;

// Declarations for time

#define NTP_OFFSET  2*60*60

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP,"europe.pool.ntp.org", NTP_OFFSET);

// Network SSID
const char* ssid = "TP-LINK_0DA2";
const char* password = "94007565";

//Global variables obtained from the two sensors
long timestamp;
float tempValue;
int humidValue;
float pressValue;
int tvocValue;
int co2Value;

//Variables for timing the GetData function

unsigned int previousMillis = 0;
const long interval = 60000; // interval of time for getting data - 1 min

//Variables for average values of sensors and number of measurements

float tempAvg = 0;
float pressAvg = 0;
float tvocAvg = 0;
float humidAvg = 0;
float co2Avg = 0;

float totalTemp = 0;
float totalPressure = 0;
int totalHumidity = 0;
int totalCO2 = 0;
int totalTVOC = 0;

int NumberOfMeasurementsFromDatabase;
long currentDay;

// ------------------------------------ SETUP ------------------------------------ //


void handleRoot();              // function prototypes for HTTP handlers
void handleWebRequests();

void setup() {

  //Enable Serial and SPIFFS
  
  Serial.begin(115200);
  Serial.println();
  Serial.println("BioSense API");
  SPIFFS.begin();
  delay(10);
  
  //Enable I2C
  Wire.begin();
  
// Connect WiFi

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  //Start server
  
  server.on("/",handleRoot);
  server.onNotFound(handleWebRequests); //Set setver all paths are not found so we can handle as per URI
  server.begin();  

  //Enable CCS811
  
  co2Sens.begin();
  CCS811Core::status returnCode = co2Sens.begin();
  if (returnCode != CCS811Core::SENSOR_SUCCESS)
  {
    Serial.println("Problem with CCS811");
    //printDriverError(returnCode);
    Serial.println();
  }
  else
  {
    Serial.println("CCS811 online");
  }

  //Setup the BME280 for basic readings
  tempSens.settings.commInterface = I2C_MODE;
  tempSens.settings.I2CAddress = 0x77;
  tempSens.settings.runMode = 3; //  3, Normal mode
  tempSens.settings.tStandby = 0; //  0, 0.5ms
  tempSens.settings.filter = 0; //  0, filter off
  tempSens.settings.tempOverSample = 1;
  tempSens.settings.pressOverSample = 1;
  tempSens.settings.humidOverSample = 1;

  delay(10); //Give BME280 time to come on
  //Calling .begin() causes the settings to be loaded
  byte id = tempSens.begin(); //Returns ID of 0x60 if successful
  if (id != 0x60)
  {
    Serial.println("Problem with BME280");
  }
  else
  {
    Serial.println("BME280 online");
  }

  //Connecting to Firebase
  
  Firebase.begin(FIREBASE_HOST, FIREBASE_AUTH);  
  Firebase.reconnectWiFi(true);
  fbdo.setBSSLBufferSize(1024, 1024);
  fbdo.setResponseSize(1024);

  //Start and update timeClient

  timeClient.begin();
  timeClient.update();

  //Get the number of measurations and total values from Firebase Database
  getTotal();

  
}

// ------------------------------------ LOOP ------------------------------------ // 



void loop() {

  server.handleClient();
  

  // call getData() function in every minute.
  
  unsigned long currentMillis = millis();

  if(currentMillis - previousMillis >= interval)
   {  
        previousMillis = currentMillis;
        getData();  
   }

   // workaround to have precise time for pushing the hour
   timeClient.update();
   if(timeClient.getHours() == 13 && timeClient.getMinutes() == 48 )
   {
      tempAvg = totalTemp / NumberOfMeasurementsFromDatabase;
      pressAvg = totalPressure / NumberOfMeasurementsFromDatabase;
      humidAvg = totalHumidity / NumberOfMeasurementsFromDatabase;
      co2Avg = totalCO2 / NumberOfMeasurementsFromDatabase;
      tvocAvg = totalTVOC / NumberOfMeasurementsFromDatabase;

      totalJson.add("Temperature", float(tempAvg));
      totalJson.add("CO2", float(co2Avg));
      totalJson.add("Pressure", float(pressAvg));
      totalJson.add("Humidity", float(humidAvg));
      totalJson.add("TVOC", float(tvocAvg));
      totalJson.add("Timestamp",int(currentDay));
      

      if (Firebase.pushJSON(fbdo, "/Sensors/HistoricMeasurements/", totalJson))
        {
            Serial.println("Data has been pushed to HistoricMeasurements");
            timeClient.update();
            currentDay = timeClient.getEpochTime();
            totalTemp = 0;
            totalPressure = 0;
            totalHumidity = 0;
            totalCO2 = 0;
            totalTVOC = 0;
            NumberOfMeasurementsFromDatabase = 0;
            if(Firebase.deleteNode(fbdo, "/Sensors/DailyMeasurements/"))
            {
              Serial.println("Deleted measurements from yesterday")  ;
            }
            else
              Serial.println("Failed to delete measurements from yesterday");
        }
      else
      {
            Serial.println("Failed to push data HistoricMeasurements");
      }

      delay(60000); // stops program for 1 minute
   }
}

// ------------------------------------ FUNCTIONS ------------------------------------ // 



void handleWebRequests(){
  if(loadFromSpiffs(server.uri())) return;
  String message = "File Not Detected\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET)?"GET":"POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i=0; i<server.args(); i++){
    message += " NAME:"+server.argName(i) + "\n VALUE:" + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
  Serial.println(message);
}

bool loadFromSpiffs(String path){
  String dataType = "text/plain";
  if(path.endsWith("/")) path += "index.html";
 
  if(path.endsWith(".src")) path = path.substring(0, path.lastIndexOf("."));
  else if(path.endsWith(".html")) dataType = "text/html";
  else if(path.endsWith(".htm")) dataType = "text/html";
  else if(path.endsWith(".css")) dataType = "text/css";
  else if(path.endsWith(".js")) dataType = "application/javascript";
  else if(path.endsWith(".png")) dataType = "image/png";
  else if(path.endsWith(".gif")) dataType = "image/gif";
  else if(path.endsWith(".jpg")) dataType = "image/jpeg";
  else if(path.endsWith(".ico")) dataType = "image/x-icon";
  else if(path.endsWith(".xml")) dataType = "text/xml";
  else if(path.endsWith(".pdf")) dataType = "application/pdf";
  else if(path.endsWith(".zip")) dataType = "application/zip";
  File dataFile = SPIFFS.open(path.c_str(), "r");
  if (server.hasArg("download")) dataType = "application/octet-stream";
  if (server.streamFile(dataFile, dataType) != dataFile.size()) {
  }
 
  dataFile.close();
  return true;
}

// Function for getting data and uploading it on Firebase Database

void getData()
{
   if (co2Sens.dataAvailable()) //Check to see if CCS811 has new data (it's the slowest sensor)
  {
    co2Sens.readAlgorithmResults(); //Read latest from CCS811 and update tVOC and CO2 variables
    RetrieveDataFromSensors();
    printData(); //Pretty print all the data
    sendToFirebase();
  }
  else if (co2Sens.checkForStatusError()) //Check to see if CCS811 has thrown an error
  {
    Serial.println(co2Sens.getErrorRegister()); //Prints whatever CSS811 error flags are detected
  }

}


//Function for getting the number of measurements and Total

void getTotal()
{
  if(Firebase.getInt(fbdo, "/Sensors/DailyMeasurements/Total/NumberOfMeasurements"))
  {
    NumberOfMeasurementsFromDatabase = fbdo.intData();
    Serial.print("Measurements from Firebase: ");
    Serial.println(NumberOfMeasurementsFromDatabase);
  }
  else {    
    Serial.println("Failed to get NumberOfMeasurements");
  }

  if(Firebase.getFloat(fbdo, "/Sensors/DailyMeasurements/Total/TotalTemp"))
  {
    totalTemp = fbdo.intData();
    Serial.print("TotalTemp from Firebase: ");
    Serial.println(totalTemp);
  }
  else {    
    Serial.println("Failed to get TotalTemp");
  }

  if(Firebase.getFloat(fbdo, "/Sensors/DailyMeasurements/Total/TotalPressure"))
  {
    totalPressure = fbdo.intData();
    Serial.print("Total Pressure from Firebase: ");
    Serial.println(totalPressure);
  }
  else {    
    Serial.println("Failed to get Total Pressure");
  }

  if(Firebase.getInt(fbdo, "/Sensors/DailyMeasurements/Total/TotalCO2"))
  {
    totalCO2 = fbdo.intData();
    Serial.print("TotalCO2 from Firebase: ");
    Serial.println(totalCO2);
  }
  else {    
    Serial.println("Failed to get TotalCO2");
  }

  if(Firebase.getInt(fbdo, "/Sensors/DailyMeasurements/Total/TotalHumidity"))
  {
    totalHumidity = fbdo.intData();
    Serial.print("TotalHumidity from Firebase: ");
    Serial.println(totalHumidity);
  }
  else {    
    Serial.println("Failed to get TotalHumdity");
  }

  if(Firebase.getInt(fbdo, "/Sensors/DailyMeasurements/Total/TotalTVOC"))
  {
    totalTVOC = fbdo.intData();
    Serial.print("TotalTVOC from Firebase: ");
    Serial.println(totalTVOC);
  }
  else {    
    Serial.println("Failed to get TotalTVOC");
  }
  currentDay = timeClient.getEpochTime();
  Serial.print("Timestamp (currentDay): ");
  Serial.println(currentDay);
}

void RetrieveDataFromSensors()
{
     timestamp = timeClient.getEpochTime();
     tempValue = tempSens.readTempC();
     humidValue = tempSens.readFloatHumidity();
     pressValue = tempSens.readFloatPressure();
     tvocValue = co2Sens.getTVOC();
     co2Value = co2Sens.getCO2();
}

//Print CO2, TVOC, Humidity, Pressure and Temp
void printData()
{
  
  Serial.print("CO2[");
  Serial.print(co2Value);
  Serial.print("]ppm");

  Serial.print(" TVOC[");
  Serial.print(tvocValue);
  Serial.print("]ppb");

  Serial.print(" temp[");
  Serial.print(tempValue);
  Serial.print("]C");

  Serial.print(" pressure[");
  Serial.print(pressValue);
  Serial.print("]Pa");

  Serial.print(" humidity[");
  Serial.print(humidValue);
  Serial.print("]%");

  Serial.println();
 }

void sendToFirebase()
{

        timeClient.update(); // -> reach the ndp server and update the time
       
        co2Json.add("value", float(co2Value));
        co2Json.add("measure_unit", "ppm");
        co2Json.add("timestamp", int(timestamp));
        
        tempJson.add("value", float(tempValue));
        tempJson.add("measure_unit", "°C");
        tempJson.add("timestamp", int(timestamp));

        tvocJson.add("value", float(tvocValue));
        tvocJson.add("measure_unit", "ppb");
        tvocJson.add("timestamp", int(timestamp));

        humidJson.add("value", float(humidValue));
        humidJson.add("measure_unit", "%");
        humidJson.add("timestamp", int(timestamp));

        pressJson.add("value", float(pressValue));
        pressJson.add("measure_unit", "Pa");
        pressJson.add("timestamp", int(timestamp));
        
        if (Firebase.pushJSON(fbdo, "/Sensors/DailyMeasurements/CO2", co2Json) &&
            Firebase.pushJSON(fbdo, "/Sensors/DailyMeasurements/Temperature", tempJson)&&
            Firebase.pushJSON(fbdo, "/Sensors/DailyMeasurements/TVOC", tvocJson)&&
            Firebase.pushJSON(fbdo, "/Sensors/DailyMeasurements/Pressure", pressJson)&&
            Firebase.pushJSON(fbdo, "/Sensors/DailyMeasurements/Humidity", humidJson)) {
        
          Serial.println("Firebase pushed data!");
          
          NumberOfMeasurementsFromDatabase=NumberOfMeasurementsFromDatabase+1;
          Firebase.setInt(fbdo, "/Sensors/DailyMeasurements/Total/NumberOfMeasurements", NumberOfMeasurementsFromDatabase);
          
          Serial.print("Number of measurements: ");
          Serial.println(NumberOfMeasurementsFromDatabase);

          totalTemp = totalTemp + tempValue;
          totalCO2 = totalCO2 + co2Value;
          totalPressure = totalPressure + pressValue;
          totalTVOC = totalTVOC + tvocValue;
          totalHumidity = totalHumidity + humidValue;

          Firebase.setFloat(fbdo, "/Sensors/DailyMeasurements/Total/TotalTemp", totalTemp);
          Firebase.setInt(fbdo, "/Sensors/DailyMeasurements/Total/TotalCO2", totalCO2);
          Firebase.setFloat(fbdo, "/Sensors/DailyMeasurements/Total/TotalPressure", totalPressure);
          Firebase.setInt(fbdo, "/Sensors/DailyMeasurements/Total/TotalTVOC", totalTVOC);
          Firebase.setInt(fbdo, "/Sensors/DailyMeasurements/Total/TotalHumidity", totalHumidity);
          
          Serial.print("totalTemp: ");
          Serial.println(totalTemp);

          Serial.print("totalCO2: ");
          Serial.println(totalCO2);

          Serial.print("totalPressure: ");
          Serial.println(totalPressure);

          Serial.print("totalTVOC: ");
          Serial.println(totalTVOC);

          Serial.print("totalHumidity: ");
          Serial.println(totalHumidity);
          
        } 
        else {
          Serial.println(fbdo.errorReason());
        }
}



void handleRoot(){
  server.sendHeader("Location", "/index.html",true);   //Redirect to our html web page
  server.send(302, "text/plane","");
}

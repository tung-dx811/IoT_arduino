/*
#if CONFIG_FREERTOS_UNICORE
#define ARDUINO_RUNNING_CORE 0
#else
#define ARDUINO_RUNNING_CORE 1
#endif
*/
#define ARDUINOJSON_USE_LONG_LONG 1
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

#define SENSOR  02
String sensors[5] = {"SENSOR1","SENSOR2","SENSOR3","SENSOR4","SENSOR5"};

const char* ssid = "DXT1";
const char* pass = "tung0811";
const char* broker = "broker.hivemq.com";
const int port=1883;

/*Topic*/
const char* authTopicPub = "iot/authentication";
const char* authTopicSub = "iot/authentication_result_7";
const char* getTokenTopicPub = "iot/getTokenCollectData";
const char* getTokenTopicSub = "iot/getTokenCollectData_result_7";
const char* collectTopic = "iot/collectdata";//topic to collect data
const char* keepAliveTopic = "iot/keepAlive";

long deviceID = 7;
String activeToken = "eyJhbGciOiJIUzUxMiJ9.eyJzdWIiOiJ0dWR2IiwiZGV2aWNlSWQiOjcsImV4cCI6MTcwNjIzNzQwNX0.Afhg7BnpUTzHtQIdlR6AZqDaVTUPdboVNLvbyszk8QhPOqim5OSyEG2gGtR0HwhSitAWNJ4MJ4Ems9XuKXcFqQ";
String collectToken;
boolean authFlag = false;
boolean tokenFlag = false;

StaticJsonDocument<512> subdoc; //json convert payload subscriber
StaticJsonDocument<300> authdoc; 
StaticJsonDocument<300> keepalivedoc; 
StaticJsonDocument<1024> datadoc;


WiFiClient ESPClient;
PubSubClient mqttClient(ESPClient);

/*TaskHandle will allow Sử dụng để có thể quản lý task đang chạy*/ 
TaskHandle_t taskCollectDataHandle = NULL;
TaskHandle_t taskSendDataHandle = NULL;
TaskHandle_t taskKeepAliveHandle = NULL;
TaskHandle_t taskGetTokenHandle = NULL;
/*Define tasks*/
void TaskCollectData( void *pvParameters );
void TaskSendData( void *pvParameters );
void TaskKeepAlive( void *pvParameters );
void TaskGetToken(void *pvParameters);

long previousSendData=0;
long previousAuth=0;

long currentMillis = 0;//tinh thoi gian do luong nuc
long previousMillis = 0;
int interval = 10000;
float calibrationFactor = 4.5;
volatile byte pulseCount;
byte pulse1Sec = 0;
float flowRate;
float flowLitres;
float sensorData[5];


// Define NTP Client to get time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

// Variables to save date and time
String formattedDate;
long unsigned long timestamp;

/*Calback interrup*/
void IRAM_ATTR pulseCounter()
{
  pulseCount++;
}

void getDatetime(){
   while(!timeClient.update()) {
    timeClient.forceUpdate();
  }
  formattedDate = timeClient.getFormattedTime();
  timestamp = timeClient.getEpochTime();
}
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.println("===========================");
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  char inData[length + 1];
  for (int i=0;i<length;i++) {
    Serial.print((char)payload[i]);
    inData[i] = (char) payload[i];
  }
  String mtopic=topic;
  /*Convert char[] ->json */
  deserializeJson(subdoc, inData);
  
  if(mtopic.equals(authTopicSub)){
    if(subdoc["reply"] == "true"){
      collectToken= subdoc["token"].as<String>();

      authFlag = true;
      tokenFlag = true;
      Serial.println("===========================");
      Serial.println("TokenCollectData:"+collectToken);
      mqttClient.unsubscribe(authTopicSub);
      Serial.println("Auth success!");
      if(taskSendDataHandle==NULL && taskKeepAliveHandle==NULL &&  taskGetTokenHandle==NULL){
          xTaskCreate(
            TaskSendData,
            "TaskSendData",
            5120,
            NULL,
            1,
            &taskSendDataHandle
          );
       
          xTaskCreate(
            TaskKeepAlive,
            "TaskKeepAlive",
            2560,
            NULL,
            2,
            &taskKeepAliveHandle
          );
          
          xTaskCreate(
            TaskGetToken,
            "TaskGetToken",
            1024,
            NULL,
            3,
            &taskGetTokenHandle
          );
        }
    }
  }
  if(mtopic.equals(getTokenTopicSub)){
    if (subdoc["reply"] == "true") {
      collectToken = subdoc["token"].as<String>();
      tokenFlag = true;
      Serial.println("GetToken....." +collectToken);
    }
  }
  Serial.println();
}

void reconnect() {
  while(!mqttClient.connected()){
    while(WiFi.status() != WL_CONNECTED){
      Serial.println("===========================");
      Serial.println("WiFi reconnect");
      WiFi.begin(ssid,pass);
      if(taskSendDataHandle!=NULL && taskKeepAliveHandle!=NULL &&  taskGetTokenHandle!=NULL){
        vTaskDelete(taskSendDataHandle);
        vTaskDelete(taskKeepAliveHandle);
        vTaskDelete(taskGetTokenHandle);
        taskSendDataHandle = NULL;
        taskKeepAliveHandle = NULL;
        taskGetTokenHandle = NULL;
      }
      delay(5000);
    }
    Serial.println("===========================");
    Serial.print("Attempting MQTT connection...");
    if(mqttClient.connect("")){
      Serial.print("\nConnected to broker");
      
      mqttClient.subscribe(getTokenTopicSub);
      if(!authFlag){
        mqttClient.subscribe(authTopicSub);
      }else {
        if(taskSendDataHandle==NULL && taskKeepAliveHandle==NULL &&  taskGetTokenHandle==NULL){
          xTaskCreate(
            TaskSendData,
            "TaskSendData",
            5120,
            NULL,
            1,
            &taskSendDataHandle
          );
          xTaskCreate(
            TaskKeepAlive,
            "TaskKeepAlive",
            2560,
            NULL,
            2,
            &taskKeepAliveHandle
          );
          
          xTaskCreate(
            TaskGetToken,
            "TaskGetToken",
            1024,
            NULL,
            3,
            &taskGetTokenHandle
          );
        }
      }
    }else{
      Serial.print("Failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" Try again in 5 seconds");
      if(taskSendDataHandle!=NULL && taskKeepAliveHandle!=NULL &&  taskGetTokenHandle!=NULL){
        vTaskDelete(taskSendDataHandle);
        vTaskDelete(taskKeepAliveHandle);
        vTaskDelete(taskGetTokenHandle);
        taskSendDataHandle = NULL;
        taskKeepAliveHandle = NULL;
        taskGetTokenHandle = NULL;
      }
      delay(5000);
    }
  }
}


void setup() {
  Serial.begin(115200);
  
  /*Kết nối wifi*/
  Serial.println("===========================");
  Serial.print("Connecting to:");
  Serial.println(ssid);
  WiFi.begin(ssid,pass);
  while(WiFi.status()!=WL_CONNECTED){
    delay(100);
    Serial.print(".");
  }
  Serial.print("\nConnected to:");
  Serial.println(ssid);
  //WiFi.setAutoReconnect(true);
  //WiFi.persistent(true);
  /*Kết nối broker*/
  mqttClient.setServer(broker,port);
  mqttClient.setCallback(callback);
  pinMode(SENSOR, INPUT_PULLUP);
  /*Setup sensor*/
  pulseCount = 0;
  flowRate = 0.0;
  flowLitres = 0;
  previousMillis = 0;
  attachInterrupt(digitalPinToInterrupt(SENSOR), pulseCounter, FALLING);
  
  memset(sensorData, 0, sizeof(sensorData));
  /*Setup timestamp*/
  timeClient.begin();
  timeClient.setTimeOffset(25200);//múi giờ 7
  
  /*Create Tasks*/
  
  xTaskCreate(
    TaskCollectData,
    "TaskCollectData",
    3072,
    NULL,
    1,
    &taskCollectDataHandle
  );
  delay(100);
}

void loop() {
  if (!mqttClient.connected()) {
    reconnect();
  }
  /*Nếu chưa authen thì cứ 10s publish auth*/
  if(!authFlag){
    if(millis()- previousAuth>10000){ 
      Serial.println("===========================");
      JsonObject JSONauth = authdoc.to<JsonObject>();
      JSONauth["deviceId"] = deviceID;
      JSONauth["token"] = activeToken;
      char message[1024];
      memset(message, 0, sizeof(message));
      serializeJson(authdoc, message, sizeof(message));
      if(mqttClient.publish(authTopicPub, message)== true){
        Serial.println("Publish authen succ");
      }else{
        Serial.println("Publish authen fail");
      }
      authdoc.clear();
      previousAuth=millis();
    }
  }
  mqttClient.loop();
}

void  TaskCollectData(void *pvParameters)
{
  (void) pvParameters;
  for (;;){
    currentMillis = millis();
    if (currentMillis - previousMillis > interval) //sau 1 giay thi chay lai
    {
      pulse1Sec = pulseCount;
      pulseCount = 0;
      flowRate = ((1000.0 / (millis() - previousMillis)) * pulse1Sec) / calibrationFactor;
      previousMillis = millis();
      flowLitres = (flowRate / 60);
      // Add the millilitres passed in this second to the cumulative total
      sensorData[0] += flowLitres;
      for(int i=1;i<5;i++){
        sensorData[i]+=random(10,40)/1000.0;
      }
    }
     vTaskDelay(200/portTICK_PERIOD_MS);
  }
}


void getTokenCollect(){
  JsonObject JSONtoken = datadoc.to<JsonObject>();
  JSONtoken["deviceId"] = deviceID;
  JSONtoken["token"] = activeToken;
  JSONtoken["active"] = "true";
  char message[1024];
  memset(message, 0, sizeof(message));
  serializeJson(datadoc, message, sizeof(message));
  if (mqttClient.publish(getTokenTopicPub,message) == true ) {
    Serial.println("Pulish get token collectData succ");
  } else {
    Serial.println("Pulish get token collectData fail");
  }
  datadoc.clear();
}

void TaskSendData(void *pvParameters)
{
  (void) pvParameters;
  for (;;) {
    if(tokenFlag){
      if(millis()- previousSendData>10000){  
        Serial.println("===========================");
        
        char message[1024];
        memset(message, 0, sizeof(message));
        JsonObject Datasensor = datadoc.to<JsonObject>();
        Datasensor["deviceId"] = deviceID;
        Datasensor["token"] = collectToken;

        getDatetime();
        Datasensor["time"] = timestamp;
        JsonArray nestedArray = Datasensor.createNestedArray("sensorDataList");
        Serial.print("Data collect:");
        Serial.println(sensorData[0]);
        for (int i = 0; i < 5; i++) {
          JsonObject nested = nestedArray.createNestedObject();
          nested["code"] = sensors[i];
          nested["value"] = sensorData[i];
          sensorData[i]=0;
        }
        serializeJson(datadoc, message, sizeof(message));
        if (mqttClient.publish(collectTopic, message) == true ) {
          Serial.println("Send Data collect");
        }else{
          Serial.println("Send Data collect fail");
        }
        datadoc.clear();
        vTaskDelay(2000/portTICK_PERIOD_MS);
        previousSendData = millis();
      }else {
        vTaskDelay(2000/portTICK_PERIOD_MS);
      }
    }else{
      /*Get token*/
      getTokenCollect();
      vTaskDelay(10000/portTICK_PERIOD_MS);
    }
  }
  vTaskDelete(NULL);
}
void TaskKeepAlive(void *pvParameters)
{
  (void) pvParameters;
  for (;;) {
    JsonObject JSONkeepAlive = keepalivedoc.to<JsonObject>();
    JSONkeepAlive["deviceId"] = deviceID;
    JSONkeepAlive["token"] = activeToken;
    JSONkeepAlive["active"] = "true";
    char message[1024];
    memset(message, 0, sizeof(message));
    serializeJson(keepalivedoc, message, sizeof(message));   
    if(mqttClient.publish(keepAliveTopic,message) == true){
      Serial.println("Publish KeepAlive succ");
    }else{
      Serial.println("Publish KeepAlive fail");
    }
    keepalivedoc.clear();
    vTaskDelay(60000/portTICK_PERIOD_MS);
  }
  vTaskDelete(NULL);
}
void TaskGetToken(void *pvParameters)
{
  (void) pvParameters;
   for (;;) {
    Serial.println("===========================");
    Serial.println("TaskGetToken");
    tokenFlag = false;
    vTaskDelay(60000/portTICK_PERIOD_MS);
  }
  vTaskDelete(NULL);
}

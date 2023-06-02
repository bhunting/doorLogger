/*
  SimpleMQTTClient.ino
  The purpose of this exemple is to illustrate a simple handling of MQTT and Wifi connection.
  Once it connects successfully to a Wifi network and a MQTT broker, it subscribe to a topic and send a message to it.
  It will also send a message delayed 5 seconds later.
*/
#include "E:/dev/sw/credentials.h"

#include "EspMQTTClient.h"

EspMQTTClient client(
  _MY_WIFI_SSID,
  _MY_WIFI_PWD,
  _MY_MQTT_SERVER,  // MQTT Broker server ip
  _MY_MQTT_NAME,   // Can be omitted if not needed
  _MY_MQTT_PWD,   // Can be omitted if not needed
  "espdoorled",     // Client name that uniquely identify your device
  1883              // The MQTT port, default to 1883. this line can be omitted
);

#include <Adafruit_NeoPixel.h>
#define PIN   D4
#define LED_NUM 7
// When we setup the NeoPixel library, we tell it how many pixels, and which pin to use to send signals.
// Note that for older NeoPixel strips you might need to change the third parameter--see the strandtest
// example for more information on possible values.
Adafruit_NeoPixel leds = Adafruit_NeoPixel(LED_NUM, PIN, NEO_GRB + NEO_KHZ800);

// number of LEDs and 3 values for R, G, B
uint8_t ledArray[LED_NUM][3] = {0};

// LED array index for each door
#define IDX_FRONT   (1)
#define IDX_BACK    (2)
#define IDX_MUDROOM (3)
#define IDX_PIR     (4)

long ledTimeout; // LED update timer
long timerNow;

void led_set(uint8_t ledvalues[LED_NUM][3]) 
{
  for (int i = 0; i < LED_NUM; i++) 
  {
    leds.setPixelColor(i, leds.Color(ledvalues[i][0], ledvalues[i][1], ledvalues[i][2]));
    leds.show();
  }
}

void setup()
{
  Serial.begin(115200);

  // Optional functionalities of EspMQTTClient
  client.enableDebuggingMessages(); // Enable debugging messages sent to serial output
  client.enableHTTPWebUpdater(); // Enable the web updater. User and password default to values of MQTTUsername and MQTTPassword. These can be overridded with enableHTTPWebUpdater("user", "password").
  client.enableOTA(); // Enable OTA (Over The Air) updates. Password defaults to MQTTPassword. Port is the default OTA port. Can be overridden with enableOTA("password", port).
  client.enableLastWillMessage("/espdoorled/lastwill", "I am going offline");  // You can activate the retain flag by setting the third parameter to true

  leds.begin(); // This initializes the NeoPixel library.
  led_set(ledArray);
  ledTimeout = millis();
}

#define setLedArray(index, R, G, B) {ledArray[index][0]=R;ledArray[index][1]=G;ledArray[index][2]=B;}
static bool doorChange = false;

// This function is called once everything is connected (Wifi and MQTT)
// WARNING : YOU MUST IMPLEMENT IT IF YOU USE EspMQTTClient
void onConnectionEstablished()
{
  client.subscribe("/doorlogger/FRONT", [](const String & payload) 
  {
    //Serial.println(payload);
    if( payload == "0")
    {
      setLedArray(IDX_FRONT,0,50,0);
    }
    else
    {
      setLedArray(IDX_FRONT,50,0,0);
    }
    doorChange = true;
  });

  client.subscribe("/doorlogger/BACK", [](const String & payload) 
  {
    //Serial.println(payload);
    if( payload == "0")
    {
      setLedArray(IDX_BACK,0,50,0);
    }
    else
    {
      setLedArray(IDX_BACK,50,0,0);
    }
    doorChange = true;
  });

  client.subscribe("/doorlogger/MUDROOM", [](const String & payload) 
  {
    //Serial.println(payload);
    if( payload == "0")
    {
      setLedArray(IDX_MUDROOM,0,50,0);
    }
    else
    {
      setLedArray(IDX_MUDROOM,50,0,0);
    }
    doorChange = true;
  });

  client.subscribe("/doorlogger/PIR", [](const String & payload) 
  {
    //Serial.println(payload);
    if( payload == "0")
    {
      setLedArray(IDX_PIR,0,50,0);
    }
    else
    {
      setLedArray(IDX_PIR,50,0,0);
    }
    doorChange = true;
  });


//  // Publish a message to "mytopic/test"
//  client.publish("mytopic/test", "This is a message"); // You can activate the retain flag by setting the third parameter to true

//  // Execute delayed instructions
//  client.executeDelayed(5 * 1000, []() 
//  {
//    client.publish("mytopic/wildcardtest/test123", "This is a message sent 5 seconds later");
//  });
}

void loop()
{
  client.loop();

  timerNow = millis(); // capture timer at this loop
  if( ((timerNow - ledTimeout) > 5000) || doorChange )
  {
    led_set(ledArray);
    ledTimeout = timerNow;
    doorChange = false;
  }
}

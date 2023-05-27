#include "secrets.h"
#include <ESP32Servo.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <jled.h>

const char *WIFI_SSID = SECRET_WIFI_SSID;
const char *WIFI_PASSWORD = SECRET_WIFI_PASSWORD;

const char *MQTT_SERVER = SECRET_MQTT_SERVER;
const char *MQTT_TOPIC_INBOUND = SECRET_MQTT_TOPIC_INBOUND;
const char *MQTT_TOPIC_OUTBOUND = SECRET_MQTT_TOPIC_OUTBOUND;

const int ONBOARD_LED = 2;
// Recommended PWM GPIO pins on the ESP32 include 2,4,12-19,21-23,25-27,32-33
const int SERVO_PIN = 18;
const int SWITCH_PIN = 33;

// Callback function header. Must be declared before the client constructor
// and the actual callback afterwards. This ensures the client referenced
// inside the callback function is valid.
void mqtt_callback(char *topic, byte *message, unsigned int length);

WiFiClient wifi_client;
PubSubClient mqtt_client(wifi_client);

// Servo control
Servo myservo;

byte *outboundBytes;

void setup()
{
  Serial.begin(9600);

  pinMode(ONBOARD_LED, OUTPUT);
  digitalWrite(ONBOARD_LED, HIGH);

  // Configure the input pin
  pinMode(SWITCH_PIN, INPUT_PULLUP);

  // Allow allocation of all timers (??)
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);

  // standard 50 hz servo
  myservo.setPeriodHertz(50);
  // attach the servo on specified pin to the servo object
  // different servos may require different min/max settings
  // for an accurate 0 to 180 sweep
  myservo.attach(SERVO_PIN, 500, 2400);

  mqtt_client.setServer(MQTT_SERVER, 1883);
  mqtt_client.setCallback(mqtt_callback);

  outboundBytes = (byte *)malloc(1);
}

void connect_wifi()
{
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print("Connecting WiFi to SSID ");
    Serial.println(WIFI_SSID);

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED)
    {
      delay(500);
      Serial.print(".");
    }

    Serial.println();
    Serial.print("WiFi connected, IP ");
    Serial.println(WiFi.localIP());
  }
}

void connect_mqtt()
{
  while (!mqtt_client.connected())
  {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (mqtt_client.connect("ESP32Client", SECRET_MQTT_USER, SECRET_MQTT_PASSWORD))
    {
      Serial.println("connected");
      // Subscribe
      mqtt_client.subscribe(MQTT_TOPIC_INBOUND);
    }
    else
    {
      Serial.print("failed, rc=");
      Serial.print(mqtt_client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void mqtt_callback(char *topic, byte *message, unsigned int length)
{
  Serial.print("Message arrived on topic: ");
  Serial.print(topic);
  Serial.print(": ");

  byte v = message[0];
  Serial.print(v);
  Serial.println();

  if (v < 0)
    v = 0;
  if (v > 180)
    v = 180;

  myservo.write(v);

  outboundBytes[0] = v;
  Serial.print("Publishing ");
  Serial.print(v);
  Serial.print(" to ");
  Serial.println(MQTT_TOPIC_OUTBOUND);
  mqtt_client.publish(MQTT_TOPIC_OUTBOUND, outboundBytes, 1);
  Serial.println("Publish done");
}

void loop()
{

  auto led = JLed(ONBOARD_LED).Breathe(2000).DelayAfter(1000).Forever();
  connect_wifi();
  connect_mqtt();
  mqtt_client.loop();

  Serial.println("Loop");
  delay(100);
}
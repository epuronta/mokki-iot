#include <Arduino.h>
#include "secrets.h"
#include <ESP32Servo.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <jled.h>

const char *WIFI_SSID = SECRET_WIFI_SSID;
const char *WIFI_PASSWORD = SECRET_WIFI_PASSWORD;

const char *MQTT_SERVER = SECRET_MQTT_SERVER;
const char *MQTT_TOPIC_INBOUND = "mokki/pump-change-request";
const char *MQTT_TOPIC_OUTBOUND = "mokki/pump-state";
const char *MQTT_TOPIC_ONLINE = "mokki/pump-online";

// Doubles as the availability heartbeat and as keep-alive traffic. The free tier
// broker deletes instances that go two months without a published message, and a
// connected client alone does not count. Hourly leaves far more margin than
// needed, so this never has to be retuned.
const unsigned long HEARTBEAT_INTERVAL_MS = 3600000;

// Give up on an association attempt and start a fresh one rather than waiting
// forever. Some WiFi failure modes only clear on a new begin().
const unsigned long WIFI_ATTEMPT_TIMEOUT_MS = 20000;

// Last resort when repeated attempts get nowhere, on the assumption that
// whatever is wedged sits below what begin() resets. Roughly 100s of failure
// before it triggers, and a genuinely absent AP just means rebooting until it
// returns, which is harmless here.
const int WIFI_ATTEMPTS_BEFORE_RESTART = 5;

const int ONBOARD_LED = 2;
// Recommended PWM GPIO pins on the ESP32 include 2,4,12-19,21-23,25-27,32-33
const int SERVO_PIN = 18;

// Callback function header. Must be declared before the client constructor
// and the actual callback afterwards. This ensures the client referenced
// inside the callback function is valid.
void mqtt_callback(char *topic, byte *message, unsigned int length);

void connect_wifi();
void connect_mqtt();
void setLedAllGood();
void setLedWifiConnecting();
void setLedMqttConnecting();

WiFiClient wifi_client;
PubSubClient mqtt_client(wifi_client);

// Servo control
Servo myservo;

String inboundString = "";
String outboundString = "";
byte *outboundBytes;

unsigned long lastHeartbeat = 0;

// Initial state is rapid blinking
auto led = JLed(ONBOARD_LED).Blink(100, 100).Forever();

void setup()
{
  Serial.begin(9600);

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

  outboundBytes = (byte *)malloc(4);
}

void connect_wifi()
{
  int failedAttempts = 0;

  while (WiFi.status() != WL_CONNECTED)
  {
    setLedWifiConnecting();

    Serial.print("Connecting WiFi to SSID ");
    Serial.println(WIFI_SSID);

    // Drop the radio first so each attempt starts from a known state.
    WiFi.disconnect(true);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    unsigned long attemptStarted = millis();
    while (WiFi.status() != WL_CONNECTED &&
           millis() - attemptStarted < WIFI_ATTEMPT_TIMEOUT_MS)
    {
      led.Update();
    }

    if (WiFi.status() != WL_CONNECTED)
    {
      failedAttempts++;
      Serial.print("WiFi attempt timed out, retrying. Attempt ");
      Serial.println(failedAttempts);

      if (failedAttempts >= WIFI_ATTEMPTS_BEFORE_RESTART)
      {
        Serial.println("Too many failed WiFi attempts, restarting");
        // Otherwise the message never makes it out before the reset.
        Serial.flush();
        ESP.restart();
      }
      continue;
    }

    Serial.println();
    Serial.print("WiFi connected, IP ");
    Serial.println(WiFi.localIP());

    setLedAllGood();
  }
}

void connect_mqtt()
{
  while (!mqtt_client.connected())
  {
    setLedMqttConnecting();
    
    unsigned long lastConnectAttempt = 0;

    while(!mqtt_client.connected())
    {
      
      
      // Attempt to connect
      if(lastConnectAttempt == 0 || millis() - lastConnectAttempt > 5000)
      {
        Serial.print("Attempting MQTT connection...");

        // The will lets the broker announce us as offline if we drop without a
        // clean disconnect. Otherwise a retained position from a dead controller
        // is indistinguishable from a live one.
        if (mqtt_client.connect("ESP32Client", SECRET_MQTT_USER, SECRET_MQTT_PASSWORD,
                                MQTT_TOPIC_ONLINE, 0, true, "0"))
        {
          Serial.println("connected");

          mqtt_client.publish(MQTT_TOPIC_ONLINE, "1", true);
          lastHeartbeat = millis();

          // Subscribe
          mqtt_client.subscribe(MQTT_TOPIC_INBOUND);
        }
        else
        {
          Serial.print("failed, rc=");
          Serial.print(mqtt_client.state());
          Serial.println(" try again in 5 seconds");
        }
        lastConnectAttempt = millis();
      }
      // Keep updating the status led
      led.Update();
    }
    setLedAllGood();
  }
}

void mqtt_callback(char *topic, byte *payload, unsigned int length)
{
  Serial.print("Message arrived on topic: ");
  Serial.print(topic);
  Serial.print(": ");

  inboundString = "";
  for (int i=0;i<length;i++) {
    Serial.print(payload[i]);
    inboundString += (char)payload[i];
  }
  Serial.println();

  // toInt() can't tell a real "0" from garbage, so parse by hand and refuse
  // anything that isn't a plain number. Otherwise a typo drives the knob to zero.
  const char *start = inboundString.c_str();
  char *end;
  long v = strtol(start, &end, 10);

  // strtol eats leading whitespace, so end == start means no digits at all.
  while (isspace(*end))
    end++;
  if (end == start || *end != '\0')
  {
    Serial.println("Ignoring non-numeric payload");
    return;
  }

  if (v < 0)
    v = 0;
  if (v > 180)
    v = 180;
  
  Serial.print("Setting servo to: ");
  Serial.println(v);

  myservo.write(v);

  outboundString = String(v);
  outboundString.getBytes(outboundBytes, 4);

  Serial.print("Publishing ");
  Serial.print(outboundString);
  Serial.print(" to ");
  Serial.println(MQTT_TOPIC_OUTBOUND);
  // Publish the appropriate amount of bytes.
  // Single-digit value -> single byte etc
  // Retained so a subscriber that connects later still learns the position.
  if(mqtt_client.publish(MQTT_TOPIC_OUTBOUND, outboundBytes, outboundString.length(), true)){
    Serial.println("Publish done");
  } else {
    Serial.println("Publish failed");
  }
}

void loop()
{
  connect_wifi();
  connect_mqtt();

  led.Update();

  mqtt_client.loop();

  // Unsigned subtraction so this keeps working across the millis() rollover at
  // 49 days. The device is meant to stay up all winter.
  if (millis() - lastHeartbeat >= HEARTBEAT_INTERVAL_MS)
  {
    mqtt_client.publish(MQTT_TOPIC_ONLINE, "1", true);
    lastHeartbeat = millis();
  }
}

void setLedAllGood()
{
    led = JLed(ONBOARD_LED).Breathe(5000).DelayAfter(5000).Forever();
}
void setLedWifiConnecting()
{
    led = JLed(ONBOARD_LED).Blink(100, 500).Forever();
}
void setLedMqttConnecting()
{
    led = JLed(ONBOARD_LED).Blink(500, 100).Forever();
}


# mokki-iot

ESP32 that turns a heat pump's power knob with a servo, driven over MQTT. The pump has
no network interface, so the knob gets turned physically.

## How it works

Connects to WiFi, then to the MQTT broker (port 1883, user + password), and subscribes
to `mokki/pump-change-request`. Each message is parsed as an int, clamped to 0..180, and
written to the servo. The resulting angle is echoed on `mokki/pump-state`.

Payloads are ASCII digits, e.g. `90`. Non-numeric input becomes `0` (`String::toInt()`),
driving the knob to the low end.

Onboard LED (GPIO 2):

| Pattern | Meaning |
| --- | --- |
| Blink 100/100 ms | Booting |
| Blink 100 on / 500 off | Connecting to WiFi |
| Blink 500 on / 100 off | Connecting to MQTT |
| Breathe, 5 s | Connected |

## Hardware

- ESP32 dev board
- Servo on **GPIO 18**, 50 Hz, 500..2400 us pulse range

## Dependencies

- [ESP32Servo](https://github.com/madhephaestus/ESP32Servo)
- [PubSubClient](https://github.com/knolleary/pubsubclient)
- [JLed](https://github.com/jandelgado/jled)

## Building

Copy `mqtt-servo/example.secrets.h` to `mqtt-servo/secrets.h` (gitignored) and fill it
in, then open `mqtt-servo/mqtt-servo.ino` in the Arduino IDE and upload. Serial runs at
9600 baud.

`debug.cfg`, `debug_custom.json` and the `.svd` files are OpenOCD/JTAG leftovers from
the Arduino IDE debug setup. Not needed for a normal build.

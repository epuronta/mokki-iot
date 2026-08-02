# mokki-iot

ESP32 that turns a heat pump's power knob with a servo, driven over MQTT. The pump has
no network interface, so the knob gets turned physically.

## How it works

Connects to WiFi, then to the MQTT broker (port 1883, user + password), and subscribes
to `mokki/pump-change-request`. Each message is parsed as an int, clamped to 0..180, and
written to the servo. The resulting angle is echoed on `mokki/pump-state`.

Payloads are ASCII digits, e.g. `90`. Non-numeric input becomes `0` (`String::toInt()`),
driving the knob to the low end.

## Topics

| Topic | Direction | Payload | Retained |
| --- | --- | --- | --- |
| `mokki/pump-change-request` | in | `0`..`180` | up to the publisher |
| `mokki/pump-state` | out | last commanded angle | yes |

`mokki/pump-state` is retained so a subscriber that connects later learns the position
immediately instead of waiting for the next change. Note it's the angle that was
*commanded*, never a measurement, so it's wrong if the knob gets turned by hand.

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

Pulled in by PlatformIO via `lib_deps`:

- [ESP32Servo](https://github.com/madhephaestus/ESP32Servo)
- [PubSubClient](https://github.com/knolleary/pubsubclient)
- [JLed](https://github.com/jandelgado/jled)

## Building

Built with [PlatformIO](https://platformio.org/). Copy `include/example.secrets.h` to
`include/secrets.h` (gitignored) and fill it in, then:

```
pio run            # compile
pio run -t upload  # flash
pio device monitor # serial, 9600 baud
```

# mokki-iot

ESP32 that turns a heat pump's power knob with a servo, driven over MQTT. The pump has
no network interface, so the knob gets turned physically.

## How it works

Connects to WiFi, then to the MQTT broker (port 1883, user + password), and subscribes
to `mokki/pump-change-request`. Each message is parsed as an int, clamped to 0..180, and
written to the servo. The resulting angle is echoed on `mokki/pump-state`.

Payloads are ASCII digits, e.g. `90`. Anything that isn't a plain number is logged and
ignored, leaving the servo alone.

## Topics

| Topic | Direction | Payload | Retained |
| --- | --- | --- | --- |
| `mokki/pump-change-request` | in | `0`..`180` | up to the publisher |
| `mokki/pump-state` | out | last commanded angle | yes |
| `mokki/pump-online` | out | `1` alive, `0` gone | yes |

`mokki/pump-state` is retained so a subscriber that connects later learns the position
immediately instead of waiting for the next change. Note it's the angle that was
*commanded*, never a measurement, so it's wrong if the knob gets turned by hand.

`mokki/pump-online` is an MQTT last will. The broker publishes `0` on its own if the
device drops without a clean disconnect, and the device publishes `1` on connect. Without
it a retained position from a dead controller is indistinguishable from a live one.
Detection lags by roughly 1.5x the keepalive, so about 22 seconds.

The device also republishes `1` hourly. That doubles as keep-alive traffic, since the
free tier broker reclaims instances that go two months without a published message and an
open connection doesn't count.

Onboard LED (GPIO 2):

| Pattern | Meaning |
| --- | --- |
| Blink 100/100 ms | Booting |
| Blink 100 on / 500 off | Connecting to WiFi |
| Blink 500 on / 100 off | Connecting to MQTT |
| Breathe, 5 s | Connected |

## Recovery behaviour

Each WiFi association attempt gets 20 seconds, then the radio is dropped and a fresh
`begin()` starts.

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

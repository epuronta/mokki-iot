# mokki-iot firmware

ESP32 that turns the heat pump knob with a servo. See the [root README](../README.md) for
the system as a whole and the topic contract.

## What it does

Connects to WiFi, then to the MQTT broker (port 1883, user + password), and subscribes to
`mokki/pump-change-request`. Each message is parsed as an int, clamped to 0..180, and
written to the servo. The resulting angle is echoed on `mokki/pump-state`, retained.

Anything that isn't a plain number is logged and ignored, leaving the servo alone.
`toInt()` can't tell a real `0` from garbage, so parsing is done by hand.

## The overshoot

Every command goes to the target +10 degrees first, then to the exact value 500ms later.

The coupling between servo and knob visibly flexes, so driving straight at a target leaves
the servo winding up the coupling rather than turning the knob. That also explains the buzz
at some positions: the servo isn't hunting in its deadband, it's holding against a
constant restoring force. Overshooting takes the flex up first.

This lives in the firmware rather than in whatever is publishing, since the quirk belongs
to this particular servo and coupling. Caveats if the mechanics change:

- The overshoot is always upward and clamps at 180, so targets above 170 get less than the
  full 10 degrees and 180 gets none.
- 500ms is a guess at travel time that happened to work. Raise it if a position starts
  landing short.

## Boot behaviour

`myservo.attach()` configures the LEDC channel but never writes a duty, so no pulses go
out until the first `write()`. The servo is limp until commanded, the knob stays
physically where it was, and a reboot can't disturb it.

Because commands are published retained, a reboot picks up the last commanded angle from
the broker on its own.

## Recovery behaviour

Each WiFi association attempt gets 20 seconds, then the radio is dropped and a fresh
`begin()` starts, since some failure modes only clear that way. After 5 consecutive
failures the device restarts itself, on the assumption that something below the WiFi stack
is wedged.

MQTT deliberately has no such escalation. Bad credentials or a deleted broker instance are
the likely failures there, and rebooting fixes neither.

Both reconnect loops block `loop()`. Accepted: nothing else needs to run while
disconnected, and both keep the status LED updating.

## Status LED

Onboard LED on GPIO 2:

| Pattern | Meaning |
| --- | --- |
| Blink 100/100 ms | Booting |
| Blink 100 on / 500 off | Connecting to WiFi |
| Blink 500 on / 100 off | Connecting to MQTT |
| Breathe, 5 s | Connected |

## Hardware

- ESP32 dev board
- Servo on **GPIO 18**, 50 Hz, 500..2400 us pulse range
- GPIO 33 is free, and is where a potentiometer would go if position feedback ever gets
  added

## Dependencies

Pulled in by PlatformIO via `lib_deps`:

- [ESP32Servo](https://github.com/madhephaestus/ESP32Servo)
- [PubSubClient](https://github.com/knolleary/pubsubclient)
- [JLed](https://github.com/jandelgado/jled)

## Building

Built with [PlatformIO](https://platformio.org/). Copy `include/example.secrets.h` to
`include/secrets.h` (gitignored) and fill it in, then from this directory:

```
pio run            # compile
pio run -t upload  # flash
pio device monitor # serial, 9600 baud
```

There is no OTA, so any change means physical access with a USB cable.

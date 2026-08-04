# mokki-iot

Turns the power knob on a cabin heat pump over the internet. The pump has no network
interface of any kind, so the knob gets turned physically, by a servo bolted to it.

## Layout

| Directory | What it is | Docs |
| --- | --- | --- |
| `firmware/` | ESP32 that drives the servo. C++, PlatformIO. | [`firmware/README.md`](firmware/README.md) |
| `web/` | Web UI for pressing the two presets. Python, FastAPI. | [`web/README.md`](web/README.md) |

## How it works

```
browser ──HTTP──> web ──MQTT──> broker <──MQTT── firmware ──PWM──> servo ──> knob
```

The two halves never talk to each other directly, and neither one is required for the
other to start. All they share is a broker (CloudAMQP, RabbitMQ with the MQTT plugin) and
the three topics below.

The broker is also the only place state lives. Both the position and the device's
availability are retained messages, which is what lets the web app stay completely
stateless and lets the firmware come up already knowing where the knob should be.

## Topics

The contract between the two halves. Changing either side means changing both.

| Topic | Direction | Payload | Retained |
| --- | --- | --- | --- |
| `mokki/pump-change-request` | web → firmware | `0`..`180` | yes, by the web app |
| `mokki/pump-state` | firmware → web | last commanded angle | yes |
| `mokki/pump-online` | firmware → web | `1` alive, `0` gone | yes |

Payloads are ASCII digits, e.g. `90`. The firmware ignores anything that isn't a plain
number rather than guessing, since a typo would otherwise drive the knob to zero.

`mokki/pump-state` is the angle that was **commanded**, never a measurement. There is no
position feedback, so it's simply wrong if somebody turns the knob by hand.

`mokki/pump-online` is an MQTT last will. The broker publishes `0` by itself if the device
drops without a clean disconnect, and the device publishes `1` on connect. Without it a
retained position from a dead controller looks identical to a live one. Detection lags by
roughly 1.5x the keepalive, so about 22 seconds. The device also republishes `1` hourly,
which doubles as keep-alive traffic for the broker's free tier.

Subscribe by exact topic name when poking at this by hand. Wildcard subscriptions never
receive retained messages, so `mosquitto_sub -t 'mokki/#'` comes back empty and looks like
nothing is there.

## The two positions

`150` is "20 vaille", `30` is "5 vaille". Those are the only positions the knob is ever
put in, which is why the web UI is two buttons rather than a slider, and why the full
0..180 range has never been calibrated.

`150` only lands correctly if it's approached from `160` first, because the coupling
flexes and the servo otherwise winds up the coupling instead of turning the knob. The
firmware owns that workaround, so anything publishing a command just sends the target. A
rigid coupling would retire the whole thing, see `TODO.md`.

## Getting it running

Each half is independent, see its own README. Roughly:

```
cd firmware && pio run -t upload   # flash the ESP32
cd web && make install && make run # web UI on :8001
```

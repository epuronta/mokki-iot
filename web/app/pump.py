"""MQTT side of the pump control.

The broker is the source of truth. Nothing is cached between requests, so every
call here opens a connection, does one thing, and drops it. That keeps the app
stateless and means it can be restarted or scaled without losing anything.

Topic names and payload format are the contract with the firmware, see
`firmware/src/main.cpp`.
"""

import asyncio
import contextlib
import logging
import ssl
import uuid
from dataclasses import dataclass

import aiomqtt

from . import settings

log = logging.getLogger(__name__)

TOPIC_COMMAND = "mokki/pump-change-request"
TOPIC_STATE = "mokki/pump-state"
TOPIC_ONLINE = "mokki/pump-online"

# Retained values arrive immediately after SUBACK if they exist at all, so this
# only has to cover the round trip. A missing retained value is a normal case
# (the broker keeps them in an unreplicated node-local store), so a read always
# ends on this timeout rather than on having collected everything.
RETAINED_WAIT_SECONDS = 1.0

# The firmware overshoots by 10 degrees and settles 500ms later, publishing its
# echo up front. 2s leaves room for that plus the round trip without the page
# feeling hung if the device is gone.
ECHO_WAIT_SECONDS = 2.0


@dataclass(frozen=True)
class PumpState:
    """What the broker currently knows. `None` means it wouldn't say."""

    angle: int | None
    online: bool | None


class BrokerUnavailableError(Exception):
    """Raised when the broker can't be reached at all."""


def _client() -> aiomqtt.Client:
    # A fresh identifier per connection because requests can overlap, and two
    # clients sharing an id would kick each other off mid-request.
    #
    # clean_session because a durable session would leave a queue behind, and
    # the free tier deletes idle queues after 28 days. Nothing here needs the
    # broker to remember us between requests anyway.
    return aiomqtt.Client(
        hostname=settings.MQTT_HOST,
        port=settings.MQTT_PORT,
        username=settings.MQTT_USER,
        password=settings.MQTT_PASSWORD,
        tls_context=ssl.create_default_context() if settings.MQTT_TLS else None,
        identifier=f"mokki-web-{uuid.uuid4().hex[:8]}",
        clean_session=True,
    )


def _parse_angle(payload: object) -> int | None:
    if not isinstance(payload, bytes):
        return None
    try:
        return int(payload.decode().strip())
    except (UnicodeDecodeError, ValueError):
        log.warning("Ignoring non-numeric state payload: %r", payload)
        return None


async def read_state() -> PumpState:
    angle: int | None = None
    online: bool | None = None

    try:
        async with _client() as client:
            # Exact topic names, not a wildcard. Wildcard subscriptions never
            # receive retained messages, so `mokki/#` would silently return
            # nothing here.
            await client.subscribe(TOPIC_STATE)
            await client.subscribe(TOPIC_ONLINE)

            with contextlib.suppress(TimeoutError):
                async with asyncio.timeout(RETAINED_WAIT_SECONDS):
                    async for message in client.messages:
                        if message.topic.matches(TOPIC_STATE):
                            angle = _parse_angle(message.payload)
                        elif message.topic.matches(TOPIC_ONLINE):
                            online = message.payload == b"1"
                        if angle is not None and online is not None:
                            break
    except aiomqtt.MqttError as exc:
        raise BrokerUnavailableError(str(exc)) from exc

    return PumpState(angle=angle, online=online)


async def set_angle(angle: int) -> int | None:
    """Command an angle, returning the firmware's echo, or None if it didn't come.

    Waiting for the echo is what stops the redirect after a POST from rendering
    the previous value and looking like the button did nothing.
    """
    try:
        async with _client() as client:
            await client.subscribe(TOPIC_STATE)

            # Retained so the firmware picks up the last command on boot and
            # stops coming up not knowing where the knob is. No firmware change
            # needed for that, it already subscribes to this topic.
            await client.publish(TOPIC_COMMAND, str(angle), qos=1, retain=True)

            with contextlib.suppress(TimeoutError):
                async with asyncio.timeout(ECHO_WAIT_SECONDS):
                    async for message in client.messages:
                        # Subscribing replays the current state with the retain
                        # flag set, while the echo we're waiting for is a live
                        # publish and arrives with it clear. That flag is the
                        # only reliable way to tell them apart: comparing
                        # payloads would misread the replay as an echo whenever
                        # the knob is already at the angle just commanded.
                        if message.retain:
                            continue
                        return _parse_angle(message.payload)
    except aiomqtt.MqttError as exc:
        raise BrokerUnavailableError(str(exc)) from exc

    log.info("No echo within %.1fs for angle %d", ECHO_WAIT_SECONDS, angle)
    return None

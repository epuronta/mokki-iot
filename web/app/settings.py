import os

# CloudAMQP's MQTT plugin listens for TLS on 8883. The firmware still uses plain
# 1883, but that hop is a cabin LAN to the broker, whereas this one runs from a
# VPS and would otherwise put the broker password on the public internet in
# cleartext on every request.
MQTT_HOST = os.environ.get("MQTT_HOST", "")
MQTT_PORT = int(os.environ.get("MQTT_PORT", "8883"))
MQTT_USER = os.environ.get("MQTT_USER", "")
MQTT_PASSWORD = os.environ.get("MQTT_PASSWORD", "")
MQTT_TLS = os.environ.get("MQTT_TLS", "1") == "1"


def require_config() -> None:
    """Fail loudly at startup rather than per request with a connection error."""
    missing = [
        name
        for name, value in (
            ("MQTT_HOST", MQTT_HOST),
            ("MQTT_USER", MQTT_USER),
            ("MQTT_PASSWORD", MQTT_PASSWORD),
        )
        if not value
    ]
    if missing:
        msg = f"Missing required environment variables: {', '.join(missing)}"
        raise RuntimeError(msg)

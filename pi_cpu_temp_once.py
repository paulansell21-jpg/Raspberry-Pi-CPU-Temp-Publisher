#!/usr/bin/env python3
"""
One-off Raspberry Pi CPU Temperature Publisher to NATS (Python reference implementation).
Reads CPU temperature from /sys/class/thermal/thermal_zone0/temp,
formats it to match the X-Sense telemetry schema, prints the payload,
and publishes a single message to NATS.
"""

import asyncio
import json
import socket
import sys
from datetime import datetime, timezone
import nats

NATS_URL = "nats://127.0.0.1:4222"
THERMAL_ZONE_PATH = "/sys/class/thermal/thermal_zone0/temp"


def read_cpu_temp_c() -> float:
    with open(THERMAL_ZONE_PATH, "r", encoding="utf-8") as f:
        millidegrees = int(f.read().strip())
    return round(millidegrees / 1000.0, 2)


def read_pi_model() -> str:
    try:
        with open("/proc/device-tree/model", "r", encoding="utf-8") as f:
            return f.read().replace("\x00", "").strip()
    except OSError:
        return "Raspberry Pi"


async def main():
    hostname = socket.gethostname().strip().lower()
    subject = sys.argv[1] if len(sys.argv) > 1 else f"sensors.xsense.{hostname}_cpu.telemetry"

    temp_c = read_cpu_temp_c()
    temp_f = round(temp_c * 9.0 / 5.0 + 32.0, 2)

    payload = {
        "timestamp_utc": datetime.now(timezone.utc).isoformat(),
        "device_id": f"{hostname}_cpu",
        "room": f"{hostname}_cpu",
        "display_name": f"{hostname.upper()} CPU",
        "station_id": hostname,
        "house_name": "White Hatch",
        "model": read_pi_model(),
        "temperature_c": temp_c,
        "temperature_f": temp_f,
        "online": True,
    }

    payload_json = json.dumps(payload, indent=2)
    print(f"Subject : {subject}")
    print(f"Payload :\n{payload_json}\n")

    nc = await nats.connect(NATS_URL)
    await nc.publish(subject, json.dumps(payload).encode("utf-8"))
    await nc.flush()
    await nc.close()
    print(f"Published successfully to {NATS_URL} on '{subject}'.")


if __name__ == "__main__":
    asyncio.run(main())

# Project Overview: Raspberry Pi CPU Temp Publisher (`pi-temp-pub`)

**Author:** Paul Ansell  
**Date:** November 2026  

---

## 1. Executive Summary

This project provides a lightweight, zero-dependency native C binary (`pi-temp-pub`) that reads the CPU temperature of Raspberry Pi devices (`/sys/class/thermal/thermal_zone0/temp`) and publishes standardized JSON telemetry onto the **NATS** message bus.

Because `pi-temp-pub` speaks the NATS TCP wire protocol directly using standard Linux POSIX sockets, it requires **no Python runtime, no virtual environments (`venv`), and no external libraries**. Once compiled on one Raspberry Pi 3 (`rp3`), the ~15 KB binary can be copied via `scp` to any other Raspberry Pi 3 (`rp3b`, etc.) and scheduled via `cron`.

---

## 2. End-to-End Architecture

```text
┌───────────────────────────────────────┐   ┌───────────────────────────────────────┐
│  RASPBERRY PI 3 MODEL B (rp3)         │   │  RASPBERRY PI 3 MODEL B+ (rp3b)       │
│  • /home/pi/pi-temp-pub/pi-temp-pub   │   │  • /home/pi/pi-temp-pub/pi-temp-pub   │
│  • Cron: */10 * * * *                 │   │  • Cron: */10 * * * *                 │
│  • nats_host = 127.0.0.1              │   │  • nats_host = <rp3-ip>               │
│  • Subject:                           │   │  • Subject:                           │
│    sensors.xsense.rp3_cpu.telemetry   │   │    sensors.xsense.rp3b_cpu.telemetry  │
└───────────────────┬───────────────────┘   └───────────────────┬───────────────────┘
                    │ (TCP :4222)                               │ (LAN TCP :4222)
                    ▼                                           ▼
┌───────────────────────────────────────────────────────────────────────────────────┐
│  NATS SERVER v2.10 (Running on rp3 :4222)                                         │
└─────────────────────────────────────────┬─────────────────────────────────────────┘
                                          │
                                          │ Subscribes to: sensors.xsense.*.telemetry
                                          ▼
┌───────────────────────────────────────────────────────────────────────────────────┐
│  TELEGRAF (/etc/telegraf/telegraf.conf)                                           │
│  • [[inputs.nats_consumer]] (data_format = "json_v2", measurement = "climate")    │
│  • humidity_pct, battery_level, rf_level marked optional = true                   │
└─────────────────────────────────────────┬─────────────────────────────────────────┘
                                          │
                                          ▼
┌───────────────────────────────────────────────────────────────────────────────────┐
│  INFLUXDB v2 (:8086) & GRAFANA                                                    │
│  • Org: "home" | Bucket: "climate" | Measurement: "climate"                       │
│  • Dashboard: White Hatch - Raspberry Pi CPU Temperatures                         │
└───────────────────────────────────────────────────────────────────────────────────┘
```

---

## 3. Configuration (`pi-temp-pub.conf`)

The binary automatically looks for `pi-temp-pub.conf` in the same directory as the executable itself (resolved via `/proc/self/exe`), so `cron` jobs do not need to `cd` into `/home/pi/pi-temp-pub` first. You can also pass an explicit config path as the first CLI argument (`/home/pi/pi-temp-pub/pi-temp-pub /path/to/custom.conf`).

```ini
# NATS broker hostname or IP address (defaults to 127.0.0.1 if commented out)
nats_host = 127.0.0.1

# NATS broker TCP port (defaults to 4222 if commented out)
nats_port = 4222

# Optional Subject Override:
# By default, the binary automatically publishes to:
#   sensors.xsense.<hostname>_cpu.telemetry
# Uncomment the line below ONLY if you want to override it with a unique subject:
# subject = sensors.xsense.custom_name.telemetry

# Optional tag overrides for InfluxDB / Grafana:
# house_name = White Hatch
# room = custom_room
# display_name = Custom Pi Name
```

---

## 4. JSON Payload & InfluxDB Schema

### Sample Published JSON
```json
{
  "timestamp_utc": "2026-11-14T16:20:01.284000+00:00",
  "device_id": "rp3_cpu",
  "room": "rp3_cpu",
  "display_name": "RP3 CPU",
  "station_id": "rp3",
  "house_name": "White Hatch",
  "model": "Raspberry Pi 3 Model B Rev 1.2",
  "temperature_c": 43.47,
  "temperature_f": 110.25,
  "online": true
}
```

### Telegraf `json_v2` Requirement (`/etc/telegraf/telegraf.conf`)
Because Raspberry Pi CPU telemetry does not include humidity, battery, or RF signal fields, those three fields in `/etc/telegraf/telegraf.conf` under `[[inputs.nats_consumer.json_v2]]` must have `optional = true`:

```toml
    [[inputs.nats_consumer.json_v2.field]]
      path = "temperature_c"
      type = "float"
    [[inputs.nats_consumer.json_v2.field]]
      path = "temperature_f"
      type = "float"
    [[inputs.nats_consumer.json_v2.field]]
      path = "humidity_pct"
      type = "float"
      optional = true
    [[inputs.nats_consumer.json_v2.field]]
      path = "battery_level"
      type = "int"
      optional = true
    [[inputs.nats_consumer.json_v2.field]]
      path = "rf_level"
      type = "int"
      optional = true
```

---

## 5. Useful Verification Commands

### Query Latest CPU Temperatures in InfluxDB
```bash
# Check rp3 CPU temperature:
influx query 'from(bucket: "climate") |> range(start: -1h) |> filter(fn: (r) => r.room == "rp3_cpu")'

# Check rp3b CPU temperature:
influx query 'from(bucket: "climate") |> range(start: -1h) |> filter(fn: (r) => r.room == "rp3b_cpu")'

# Check all Raspberry Pi CPU temperatures:
influx query 'from(bucket: "climate") |> range(start: -1h) |> filter(fn: (r) => r.room =~ /.*_cpu$/)'
```

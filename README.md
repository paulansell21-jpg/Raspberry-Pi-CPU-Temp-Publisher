# Raspberry Pi CPU Temp Publisher (`pi-temp-pub`)

A zero-dependency, native C binary (`pi-temp-pub`) for Raspberry Pi devices that reads the SoC thermal sensor (`/sys/class/thermal/thermal_zone0/temp`), detects the device hostname and hardware revision (`/proc/device-tree/model`), and publishes a standardized JSON telemetry payload directly to a **NATS** broker over TCP (`:4222`).

Designed to run from `cron` across multiple Raspberry Pis with **zero Python runtime, virtual environments, or third-party libraries required**.

---

## Features

- **Zero Runtime Dependencies:** Compiles in `< 1 second` with standard `gcc` into a `~15 KB` native ARM binary (`pi-temp-pub`).
- **Automatic Hardware & Hostname Detection:** Reads the Pi's hostname (e.g. `rp3`, `rp3b`) and hardware model (e.g. `Raspberry Pi 3 Model B Rev 1.2`, `Raspberry Pi 3 Model B Plus Rev 1.3`).
- **Auto-Discovered Config File (`pi-temp-pub.conf`):** Automatically loads `pi-temp-pub.conf` from the same folder as the binary (via `/proc/self/exe`), even when invoked by `cron`.
- **Sensible Built-in Defaults:** Defaults to `nats://127.0.0.1:4222` and subject `sensors.xsense.<hostname>_cpu.telemetry` if no config file or override is set.
- **Drop-In InfluxDB & Grafana Compatibility:** Matches the `climate` JSON schema (`temperature_c`, `temperature_f`, `room`, `display_name`, `device_id`, `station_id`, `model`, `house_name`, `timestamp_utc`) ingested by Telegraf into InfluxDB v2.

---

## Repository Contents

| File | Description |
| :--- | :--- |
| `pi_temp_pub.c` | Native C source code for the standalone `pi-temp-pub` binary |
| `pi-temp-pub.conf` | Optional configuration file (NATS host, port, and subject/tag overrides) |
| `pi_cpu_temperature_dashboard.json` | Grafana dashboard JSON for monitoring Raspberry Pi CPU temperatures |
| `pi_cpu_temp_once.py` | Reference Python script (requires `nats-py`) |
| `PROJECT_OVERVIEW.md` | Full architecture, deployment, Telegraf/InfluxDB schema, and operations guide |

---

## Quick Start

### 1. Compile on the Primary Raspberry Pi (`rp3`)
```bash
mkdir -p /home/pi/pi-temp-pub
# Copy pi_temp_pub.c and pi-temp-pub.conf into /home/pi/pi-temp-pub/
gcc -O2 -Wall -s -o /home/pi/pi-temp-pub/pi-temp-pub /home/pi/pi-temp-pub/pi_temp_pub.c
```

### 2. Test Execution
```bash
/home/pi/pi-temp-pub/pi-temp-pub
```
Example output:
```text
Config  : /home/pi/pi-temp-pub/pi-temp-pub.conf
Server  : nats://127.0.0.1:4222
Subject : sensors.xsense.rp3_cpu.telemetry
Payload : {"timestamp_utc":"2026-11-14T16:20:01.284000+00:00","device_id":"rp3_cpu","room":"rp3_cpu","display_name":"RP3 CPU","station_id":"rp3","house_name":"White Hatch","model":"Raspberry Pi 3 Model B Rev 1.2","temperature_c":43.47,"temperature_f":110.25,"online":true}
Published successfully to nats://127.0.0.1:4222 on 'sensors.xsense.rp3_cpu.telemetry'.
```

### 3. Deploy to Additional Raspberry Pis (`rp3b`, etc.)
Copy the compiled `pi-temp-pub` binary and `pi-temp-pub.conf` to the remote Pi:
```bash
ssh pi@<OTHER_PI_IP> "mkdir -p /home/pi/pi-temp-pub"
scp /home/pi/pi-temp-pub/pi-temp-pub /home/pi/pi-temp-pub/pi-temp-pub.conf pi@<OTHER_PI_IP>:/home/pi/pi-temp-pub/
```
On the remote Pi, edit `/home/pi/pi-temp-pub/pi-temp-pub.conf` and set `nats_host` to the IP address of your NATS server (`rp3`).

### 4. Schedule via Cron (Every 10 Minutes)
Run `crontab -e` on each Raspberry Pi and add:
```cron
*/10 * * * * /home/pi/pi-temp-pub/pi-temp-pub >> /home/pi/pi-temp-pub/pi-temp.log 2>&1
```

# Hermes — Remote Telemetry Monitor

A lightweight, real-time hardware monitoring system built on MQTT + Protocol Buffers.
Designed as a developer tool for environments where SSH is unavailable or impractical.

## Architecture

```
[ Monitored Device ]        [ Management Server ]          [ Any Browser ]
  C Publisher          →      Mosquitto Broker        →
  (Protobuf binary)           FastAPI Subscriber      →   Live Dashboard
                              SQLite History               http://<server-ip>:8000
```

---

## 🖥️ Setting Up a Monitored Device

Run this **on the device you want to monitor** (e.g. a remote server, Raspberry Pi, laptop):

```bash
bash scripts/setup-device.sh
```

**What it does:**
- Installs build dependencies (`gcc`, `cmake`, `libssl-dev`, `protobuf-c-compiler`)
- Clones and builds the Paho MQTT C library
- Compiles the Hermes publisher binary
- Installs and enables the `hermes-publisher` systemd service

**Configuration:** Edit the credentials and broker address in `hermes-publisher.service` before running the script, or pass them as environment variables:

```bash
MQTT_BROKER=192.168.1.50 MQTT_PASS=yourpassword INTERVAL=5 bash scripts/setup-device.sh
```

After setup, the publisher starts automatically on boot. Check its status with:
```bash
sudo systemctl status hermes-publisher
journalctl -u hermes-publisher -f
```

---

## 🗄️ Setting Up the Management Server

Run this **on the machine that will run the broker and dashboard** (once per server):

```bash
bash scripts/setup-server.sh
```

**What it does:**
- Installs Mosquitto broker and configures it with authentication
- Prompts you to set an MQTT password for the `hermes` user
- Creates a Python virtual environment and installs FastAPI dependencies
- Generates Python Protobuf bindings from `schema.proto`
- Prints the command to start the dashboard server

**After setup, start the server with:**
```bash
cd server
source venv/bin/activate
uvicorn server:app --host 0.0.0.0 --port 8000
```

Open `http://<this-machine-ip>:8000` in any browser on the network.
Default dashboard credentials: `admin` / `hermes123` (change `DASHBOARD_PASS` in `server.py`)

---

## Files at a glance

| File | Lives on |
|---|---|
| `main.c`, `CMakeLists.txt`, `schema.proto` | Monitored Device |
| `hermes-publisher.service` | Monitored Device |
| `paho.mqtt.c/` | Monitored Device (built from source) |
| `server.py` | Management Server |
| `templates/index.html` | Management Server |
| `requirements.txt` | Management Server |
| `schema_pb2.py` | Management Server |
| `scripts/setup-device.sh` | Run on Monitored Device |
| `scripts/setup-server.sh` | Run on Management Server |

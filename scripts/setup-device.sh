#!/usr/bin/env bash
# setup-device.sh — Run this on the device you want to monitor.
# It installs all dependencies, builds the publisher, and installs the systemd service.

set -e  # Exit immediately on any error

# ── Config (can be overridden via environment variables) ─────────────────────
MQTT_BROKER="${MQTT_BROKER:-localhost}"
MQTT_USER="${MQTT_USER:-hermes}"
MQTT_PASS="${MQTT_PASS:-changeme}"
INTERVAL="${INTERVAL:-3}"

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BINARY="$REPO_DIR/build/hermes_app"
PAHO_DIR="$REPO_DIR/paho.mqtt.c"
SERVICE_DEST="/etc/systemd/system/hermes-publisher.service"

echo "========================================"
echo "  Hermes — Device Setup"
echo "========================================"
echo "  Broker:   $MQTT_BROKER"
echo "  User:     $MQTT_USER"
echo "  Interval: ${INTERVAL}s"
echo "  Repo:     $REPO_DIR"
echo "========================================"
echo ""

# ── 1. Install system dependencies ───────────────────────────────────────────
echo "[1/5] Installing build dependencies..."
sudo apt-get update -qq
sudo apt-get install -y -qq \
    build-essential \
    cmake \
    libssl-dev \
    protobuf-c-compiler \
    libprotobuf-c-dev \
    git

# ── 2. Clone and build Paho MQTT C library ────────────────────────────────────
echo "[2/5] Building Paho MQTT C library..."
if [ ! -d "$PAHO_DIR" ]; then
    git clone https://github.com/eclipse-paho/paho.mqtt.c.git "$PAHO_DIR"
fi
make -C "$PAHO_DIR" --no-print-directory -j"$(nproc)"

# ── 3. Build the publisher binary ─────────────────────────────────────────────
echo "[3/5] Building Hermes publisher..."
mkdir -p "$REPO_DIR/build"
cmake -S "$REPO_DIR" -B "$REPO_DIR/build" \
    -DMQTT_USER="$MQTT_USER" \
    -DMQTT_PASS="$MQTT_PASS" \
    -DINTERVAL="$INTERVAL" \
    -DCMAKE_BUILD_TYPE=Release \
    -DMQTT_BROKER="tcp://$MQTT_BROKER:1883" > /dev/null
make -C "$REPO_DIR/build" --no-print-directory -j"$(nproc)"

echo "    Binary built at: $BINARY"

# ── 4. Install the systemd service ────────────────────────────────────────────
echo "[4/5] Installing systemd service..."
CURRENT_USER="$(whoami)"
PAHO_LIB="$PAHO_DIR/build/output"

# Write the service file dynamically with the correct paths and credentials
sudo tee "$SERVICE_DEST" > /dev/null <<EOF
[Unit]
Description=Hermes MQTT Telemetry Publisher
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=$CURRENT_USER
WorkingDirectory=$REPO_DIR/build
Environment="LD_LIBRARY_PATH=$PAHO_LIB"
ExecStart=$BINARY --user $MQTT_USER --pass $MQTT_PASS --interval $INTERVAL --broker tcp://$MQTT_BROKER:1883
Restart=on-failure
RestartSec=5s
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
EOF

sudo systemctl daemon-reload
sudo systemctl enable hermes-publisher
sudo systemctl restart hermes-publisher

# ── 5. Report ─────────────────────────────────────────────────────────────────
echo "[5/5] Done!"
echo ""
echo "  Publisher is running and will start automatically on boot."
echo "  View live logs with:"
echo "    journalctl -u hermes-publisher -f"
echo ""
sudo systemctl status hermes-publisher --no-pager

#!/usr/bin/env bash
# setup-server.sh — Run this on the management machine (broker + dashboard).
# Installs Mosquitto, configures auth, sets up the Python virtual environment,
# and generates Protobuf bindings.

set -e

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VENV_DIR="$REPO_DIR/venv"

echo "========================================"
echo "  Hermes — Server Setup"
echo "========================================"
echo "  Repo: $REPO_DIR"
echo "========================================"
echo ""

# ── 1. Install system dependencies ───────────────────────────────────────────
echo "[1/5] Installing system dependencies..."
sudo apt-get update -qq
sudo apt-get install -y -qq \
    mosquitto \
    mosquitto-clients \
    protobuf-compiler \
    python3 \
    python3-pip \
    python3-venv

# ── 2. Configure Mosquitto with authentication ────────────────────────────────
echo "[2/5] Configuring Mosquitto broker..."

CONF_FILE="/etc/mosquitto/conf.d/hermes.conf"
PASSWD_FILE="/etc/mosquitto/passwd"

# Write the broker config
sudo tee "$CONF_FILE" > /dev/null <<EOF
listener 1883
allow_anonymous false
password_file $PASSWD_FILE
EOF

# Create the MQTT user — prompts for password interactively
echo ""
echo "  → Set the MQTT password for the 'hermes' broker user:"
echo "    (This is the --pass value you will use on monitored devices)"
echo ""
sudo mosquitto_passwd -c "$PASSWD_FILE" hermes

# Fix ownership so mosquitto service can read the file
sudo chown mosquitto:mosquitto "$PASSWD_FILE"

sudo systemctl enable mosquitto
sudo systemctl restart mosquitto

echo "  Mosquitto is running on port 1883."

# ── 3. Python virtual environment ─────────────────────────────────────────────
echo "[3/5] Setting up Python environment..."
if [ ! -d "$VENV_DIR" ]; then
    python3 -m venv "$VENV_DIR"
fi
# shellcheck disable=SC1091
source "$VENV_DIR/bin/activate"
pip install -q --upgrade pip
pip install -q -r "$REPO_DIR/requirements.txt"

# ── 4. Generate Python Protobuf bindings ──────────────────────────────────────
echo "[4/5] Generating Protobuf bindings..."
protoc --python_out="$REPO_DIR" -I"$REPO_DIR" "$REPO_DIR/schema.proto"
echo "  Generated: $REPO_DIR/schema_pb2.py"

# ── 5. Report ─────────────────────────────────────────────────────────────────
echo "[5/5] Done!"

# Detect this machine's LAN IP for convenience
LAN_IP=$(hostname -I | awk '{print $1}')

echo ""
echo "========================================"
echo "  Setup complete! Next steps:"
echo "========================================"
echo ""
echo "  1. Edit server.py and update MQTT_PASS and DASHBOARD_PASS"
echo "     at the top of the file."
echo ""
echo "  2. Start the dashboard server:"
echo "     cd $REPO_DIR"
echo "     source venv/bin/activate"
echo "     uvicorn server:app --host 0.0.0.0 --port 8000"
echo ""
echo "  3. Open the dashboard from any machine on the network:"
echo "     http://$LAN_IP:8000"
echo ""
echo "  4. On each monitored device, run:"
echo "     MQTT_BROKER=$LAN_IP MQTT_PASS=<yourpassword> bash scripts/setup-device.sh"
echo "========================================"

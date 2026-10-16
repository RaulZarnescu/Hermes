import asyncio
import sqlite3
import json
import time
import secrets
from contextlib import asynccontextmanager

import paho.mqtt.client as mqtt
from fastapi import FastAPI, WebSocket, WebSocketDisconnect, Body, Depends, HTTPException, status
from fastapi.responses import HTMLResponse, JSONResponse
from fastapi.security import HTTPBasic, HTTPBasicCredentials
from fastapi.templating import Jinja2Templates
from fastapi.requests import Request

import schema_pb2

# ─── Config ───────────────────────────────────────────────────────────────────
MQTT_BROKER = "localhost"
MQTT_PORT   = 1883
MQTT_USER   = "hermes"
MQTT_PASS   = "Peroroncino"   # MQTT broker password

# Dashboard HTTP Basic Auth credentials
# Change these before deploying!
DASHBOARD_USER = "admin"
DASHBOARD_PASS = "hermes123"

MQTT_TOPIC = "hermes/+/telemetry"   # + is a wildcard matching one topic segment
DB_PATH    = "hermes.db"

# ─── Auth ────────────────────────────────────────────────────────────────────
security = HTTPBasic()

def require_auth(credentials: HTTPBasicCredentials = Depends(security)) -> str:
    ok_user = secrets.compare_digest(credentials.username.encode(), DASHBOARD_USER.encode())
    ok_pass = secrets.compare_digest(credentials.password.encode(), DASHBOARD_PASS.encode())
    if not (ok_user and ok_pass):
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Invalid credentials",
            headers={"WWW-Authenticate": "Basic"},
        )
    return credentials.username


# ─── Database ─────────────────────────────────────────────────────────────────
def db_connect():
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row   # rows behave like dicts
    return conn

def db_init():
    with db_connect() as conn:
        conn.execute("""
            CREATE TABLE IF NOT EXISTS telemetry (
                id              INTEGER PRIMARY KEY AUTOINCREMENT,
                device_id       TEXT    NOT NULL,
                timestamp       REAL    NOT NULL,
                cpu_temperature INTEGER,
                cpu_core1       INTEGER,
                cpu_core2       INTEGER,
                cpu_core3       INTEGER,
                cpu_core4       INTEGER,
                used_memory     INTEGER,
                total_memory    INTEGER,
                uptime_seconds  INTEGER,
                disk_used_gb    INTEGER,
                disk_total_gb   INTEGER,
                load_avg_1m     REAL
            )
        """)
        # Index makes device lookups fast even with millions of rows
        conn.execute("CREATE INDEX IF NOT EXISTS idx_device_ts ON telemetry(device_id, timestamp)")
        conn.commit()

def db_insert(data: dict):
    with db_connect() as conn:
        conn.execute("""
            INSERT INTO telemetry (
                device_id, timestamp, cpu_temperature,
                cpu_core1, cpu_core2, cpu_core3, cpu_core4,
                used_memory, total_memory, uptime_seconds,
                disk_used_gb, disk_total_gb, load_avg_1m
            ) VALUES (
                :device_id, :timestamp, :cpu_temperature,
                :cpu_core1, :cpu_core2, :cpu_core3, :cpu_core4,
                :used_memory, :total_memory, :uptime_seconds,
                :disk_used_gb, :disk_total_gb, :load_avg_1m
            )
        """, data)
        conn.commit()

# ─── WebSocket manager ────────────────────────────────────────────────────────
class ConnectionManager:
    def __init__(self):
        self.active_connections: list[WebSocket] = []

    async def connect(self, ws: WebSocket):
        await ws.accept()
        self.active_connections.append(ws)

    def disconnect(self, ws: WebSocket):
        if ws in self.active_connections:
            self.active_connections.remove(ws)

    async def broadcast(self, message: str):
        for ws in list(self.active_connections):
            try:
                await ws.send_text(message)
            except Exception:
                self.disconnect(ws)

manager = ConnectionManager()
main_loop   = None
mqtt_client = None

# ─── MQTT callbacks ───────────────────────────────────────────────────────────
def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print(f"Connected to MQTT broker — subscribing to {MQTT_TOPIC}")
        client.subscribe(MQTT_TOPIC)
    else:
        print(f"MQTT connection refused, rc={rc} (wrong credentials?)")

def on_message(client, userdata, msg):
    try:
        telemetry = schema_pb2.Telemetry()
        telemetry.ParseFromString(msg.payload)

        data = {
            "device_id":       telemetry.device_id,
            "timestamp":       time.time(),
            "cpu_temperature": telemetry.cpu_temperature,
            "cpu_core1":       telemetry.cpu_core1,
            "cpu_core2":       telemetry.cpu_core2,
            "cpu_core3":       telemetry.cpu_core3,
            "cpu_core4":       telemetry.cpu_core4,
            "used_memory":     telemetry.used_memory,
            "total_memory":    telemetry.total_memory,
            "uptime_seconds":  telemetry.uptime_seconds,
            "disk_used_gb":    telemetry.disk_used_gb,
            "disk_total_gb":   telemetry.disk_total_gb,
            "load_avg_1m":     round(telemetry.load_avg_1m, 2),
        }

        # Persist to SQLite
        db_insert(data)

        # Forward to all open browser tabs over WebSocket
        global main_loop
        if main_loop:
            asyncio.run_coroutine_threadsafe(
                manager.broadcast(json.dumps(data)), main_loop
            )
    except Exception as e:
        print(f"Failed to decode message: {e}")

# ─── App lifespan (replaces deprecated @app.on_event) ────────────────────────
@asynccontextmanager
async def lifespan(app: FastAPI):
    global main_loop, mqtt_client
    db_init()
    main_loop   = asyncio.get_running_loop()
    mqtt_client = mqtt.Client()
    mqtt_client.username_pw_set(MQTT_USER, MQTT_PASS)
    mqtt_client.on_connect = on_connect
    mqtt_client.on_message = on_message
    mqtt_client.connect(MQTT_BROKER, MQTT_PORT, 60)
    mqtt_client.loop_start()
    yield                   # Server runs here
    mqtt_client.loop_stop()
    mqtt_client.disconnect()

app = FastAPI(lifespan=lifespan)
templates = Jinja2Templates(directory="templates")

# ─── HTTP routes ──────────────────────────────────────────────────────────────
@app.get("/", response_class=HTMLResponse)
async def dashboard(request: Request, user: str = Depends(require_auth)):
    return templates.TemplateResponse(request=request, name="index.html")

@app.get("/api/devices")
async def get_devices(user: str = Depends(require_auth)):
    """Returns all known devices and when they last reported."""
    with db_connect() as conn:
        rows = conn.execute("""
            SELECT device_id, MAX(timestamp) AS last_seen
            FROM telemetry
            GROUP BY device_id
            ORDER BY last_seen DESC
        """).fetchall()
    return JSONResponse([dict(r) for r in rows])

@app.get("/api/history/{device_id}")
async def get_history(device_id: str, user: str = Depends(require_auth)):
    """Returns the last 60 readings for a device — used to pre-fill charts."""
    with db_connect() as conn:
        rows = conn.execute("""
            SELECT * FROM telemetry
            WHERE device_id = ?
            ORDER BY timestamp DESC
            LIMIT 60
        """, (device_id,)).fetchall()
    return JSONResponse(list(reversed([dict(r) for r in rows])))

@app.post("/api/command/{device_id}")
async def send_command(device_id: str, payload: dict = Body(...), user: str = Depends(require_auth)):
    """Publishes a raw command string to hermes/<device_id>/commands."""
    command = payload.get("command", "").strip()
    if not command:
        return JSONResponse({"error": "empty command"}, status_code=400)
    topic = f"hermes/{device_id}/commands"
    mqtt_client.publish(topic, command, qos=1)
    print(f"[API] Sent command '{command}' → {topic}")
    return {"status": "sent", "device": device_id, "command": command}

# ─── WebSocket ────────────────────────────────────────────────────────────────
@app.websocket("/ws")
async def websocket_endpoint(ws: WebSocket):
    await manager.connect(ws)
    try:
        while True:
            await ws.receive_text()
    except WebSocketDisconnect:
        manager.disconnect(ws)

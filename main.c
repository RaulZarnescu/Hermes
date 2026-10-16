#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/statvfs.h>
#include "MQTTClient.h"
#include "build/schema.pb-c.h"

#define DEFAULT_ADDRESS  "tcp://localhost:1883"
#define DEFAULT_INTERVAL 3
#define QOS              1
#define TIMEOUT          10000L

// ─── Shared state (written by callback thread, read by main loop) ─────────────
// volatile tells the compiler "this can change at any time from another thread,
// never cache it in a register". Without it, the compiler might optimise the
// sleep loop into an infinite spin because it never sees g_interval change.
volatile int g_interval = DEFAULT_INTERVAL;
volatile int g_ping     = 0;   // set to 1 by PING command; main loop clears it

// ─── MQTT Callbacks ───────────────────────────────────────────────────────────

// Handles incoming messages on hermes/<device_id>/commands
int msgarrvd(void *context, char *topicName, int topicLen, MQTTClient_message *message) {
    char cmd[256] = {0};
    int len = message->payloadlen < 255 ? message->payloadlen : 255;
    memcpy(cmd, message->payload, len);

    printf("[CMD] Received: '%s'\n", cmd);

    if (strncmp(cmd, "SET_INTERVAL ", 13) == 0) {
        // SET_INTERVAL <N> — update the loop sleep duration on the fly
        int val = atoi(cmd + 13);
        if (val > 0) {
            g_interval = val;
            printf("[CMD] Interval updated to %d seconds\n", val);
        }
    } else if (strcmp(cmd, "PING") == 0) {
        // PING — break out of the sleep early and send an immediate message
        g_ping = 1;
    }

    MQTTClient_freeMessage(&message);
    MQTTClient_free(topicName);
    return 1;
}

void connlost(void *context, char *cause) {
    printf("\nConnection lost — cause: %s\n", cause);
}

// ─── Data collection helpers ──────────────────────────────────────────────────

void getDeviceId(char *out, size_t len) {
    FILE *f = fopen("/proc/sys/kernel/hostname", "r");
    if (f) {
        if (fgets(out, (int)len, f))
            out[strcspn(out, "\n")] = '\0';
        fclose(f);
    } else {
        strncpy(out, "unknown-device", len);
    }
}

int64_t getUptimeSeconds() {
    // /proc/uptime: "<total_uptime> <idle_time>\n"
    FILE *f = fopen("/proc/uptime", "r");
    if (!f) return 0;
    double uptime = 0.0;
    fscanf(f, "%lf", &uptime);
    fclose(f);
    return (int64_t)uptime;
}

float getLoadAverage() {
    // /proc/loadavg: "1min 5min 15min running/total last_pid"
    FILE *f = fopen("/proc/loadavg", "r");
    if (!f) return 0.0f;
    float load1 = 0.0f;
    fscanf(f, "%f", &load1);
    fclose(f);
    return load1;
}

void getDiskUsage(int *used_gb, int *total_gb) {
    // statvfs() gives block-level info about the filesystem mounted at "/"
    struct statvfs stat;
    if (statvfs("/", &stat) != 0) { *used_gb = 0; *total_gb = 0; return; }
    unsigned long long total = (unsigned long long)stat.f_blocks * stat.f_frsize;
    unsigned long long free  = (unsigned long long)stat.f_bfree  * stat.f_frsize;
    *total_gb = (int)(total / (1024ULL * 1024 * 1024));
    *used_gb  = (int)((total - free) / (1024ULL * 1024 * 1024));
}

void getComputerData(Telemetry *info) {
    // 1. CPU Temperature
    FILE *f_temp = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
    if (f_temp) {
        int temp = 0;
        fscanf(f_temp, "%d", &temp);
        info->cpu_temperature = temp / 1000;
        fclose(f_temp);
    } else {
        info->cpu_temperature = 0;
    }

    // 2. Memory
    FILE *f_mem = fopen("/proc/meminfo", "r");
    if (f_mem) {
        long mem_total = 0, mem_available = 0;
        char buf[256];
        while (fgets(buf, sizeof(buf), f_mem)) {
            if      (strncmp(buf, "MemTotal:",     9)  == 0) sscanf(buf, "MemTotal: %ld kB",     &mem_total);
            else if (strncmp(buf, "MemAvailable:", 13) == 0) sscanf(buf, "MemAvailable: %ld kB", &mem_available);
        }
        fclose(f_mem);
        info->total_memory = (int)(mem_total / 1024);
        info->used_memory  = (int)((mem_total - mem_available) / 1024);
    }

    // 3. Per-core CPU usage — we compute a delta between two reads.
    //    The kernel exposes cumulative CPU ticks in /proc/stat. By recording
    //    the previous values and comparing to the current ones, we calculate
    //    the percentage of non-idle ticks over the last interval.
    static unsigned long long pu[4]={0}, pn[4]={0}, ps[4]={0};
    static unsigned long long pi[4]={0}, pw[4]={0}, pr[4]={0}, pq[4]={0};
    static int initialized = 0;

    FILE *f_cpu = fopen("/proc/stat", "r");
    if (f_cpu) {
        char buf[256];
        while (fgets(buf, sizeof(buf), f_cpu)) {
            if (strncmp(buf, "cpu", 3) == 0 && buf[3] >= '0' && buf[3] <= '3') {
                int idx = buf[3] - '0';
                unsigned long long user, nice, sys, idle, iowait, irq, softirq, steal, g, gn;
                sscanf(buf, "cpu%*d %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
                       &user, &nice, &sys, &idle, &iowait, &irq, &softirq, &steal, &g, &gn);

                if (initialized) {
                    unsigned long long idle_now = idle + iowait;
                    unsigned long long idle_was = pi[idx] + pw[idx];
                    unsigned long long busy_now = user + nice + sys + irq + softirq;
                    unsigned long long busy_was = pu[idx] + pn[idx] + ps[idx] + pr[idx] + pq[idx];
                    unsigned long long dtotal   = (idle_now + busy_now) - (idle_was + busy_was);
                    unsigned long long didle    = idle_now - idle_was;
                    int usage = dtotal > 0 ? (int)((100 * (dtotal - didle)) / dtotal) : 0;
                    if      (idx == 0) info->cpu_core1 = usage;
                    else if (idx == 1) info->cpu_core2 = usage;
                    else if (idx == 2) info->cpu_core3 = usage;
                    else if (idx == 3) info->cpu_core4 = usage;
                } else {
                    info->cpu_core1 = info->cpu_core2 = info->cpu_core3 = info->cpu_core4 = 0;
                }
                pu[idx]=user; pn[idx]=nice; ps[idx]=sys;
                pi[idx]=idle; pw[idx]=iowait; pr[idx]=irq; pq[idx]=softirq;
            }
        }
        fclose(f_cpu);
        initialized = 1;
    }

    // 4. Uptime, load average, disk
    info->uptime_seconds = getUptimeSeconds();
    info->load_avg_1m    = getLoadAverage();
    getDiskUsage(&info->disk_used_gb, &info->disk_total_gb);
}

// ─── CLI argument helpers ─────────────────────────────────────────────────────

// Returns the value after a named flag, or NULL if not found.
// e.g. parseArg(argc, argv, "--pass") for "./app --pass secret"
char *parseArg(int argc, char **argv, const char *flag) {
    for (int i = 1; i < argc - 1; i++)
        if (strcmp(argv[i], flag) == 0) return argv[i + 1];
    return NULL;
}

int parseInterval(int argc, char **argv) {
    char *v = parseArg(argc, argv, "--interval");
    if (v) { int n = atoi(v); if (n > 0) return n; }
    return DEFAULT_INTERVAL;
}

// ─── Publish helper ───────────────────────────────────────────────────────────

void publishTelemetry(MQTTClient client, const char *topic, Telemetry *msg) {
    size_t len = telemetry__get_packed_size(msg);
    uint8_t *buf = malloc(len);
    if (!buf) return;
    telemetry__pack(msg, buf);

    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    MQTTClient_deliveryToken token;
    pubmsg.payload    = buf;
    pubmsg.payloadlen = (int)len;
    pubmsg.qos        = QOS;
    pubmsg.retained   = 0;

    int rc = MQTTClient_publishMessage(client, topic, &pubmsg, &token);
    if (rc == MQTTCLIENT_SUCCESS) {
        MQTTClient_waitForCompletion(client, token, TIMEOUT);
        printf("[%s] %zuB | uptime: %llds | load: %.2f | disk: %d/%dGB\n",
               msg->device_id, len, (long long)msg->uptime_seconds,
               msg->load_avg_1m, msg->disk_used_gb, msg->disk_total_gb);
    } else {
        printf("Publish failed, rc=%d\n", rc);
    }
    free(buf);
}

// ─── Main ─────────────────────────────────────────────────────────────────────

int main(int argc, char *argv[]) {
    MQTTClient client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    int rc;

    // Parse CLI args — credentials never appear in source code
    g_interval       = parseInterval(argc, argv);
    char *mqtt_user  = parseArg(argc, argv, "--user");
    char *mqtt_pass  = parseArg(argc, argv, "--pass");
    char *broker     = parseArg(argc, argv, "--broker");
    if (!broker) broker = DEFAULT_ADDRESS;

    // Read device ID once; it becomes the dynamic part of our topics
    char device_id[256] = {0};
    getDeviceId(device_id, sizeof(device_id));
    printf("Device:  %s\nBroker:  %s\nInterval: %ds\n\n", device_id, broker, g_interval);

    // Build per-device topics: hermes/<hostname>/telemetry and hermes/<hostname>/commands
    char telemetry_topic[512], command_topic[512];
    snprintf(telemetry_topic, sizeof(telemetry_topic), "hermes/%s/telemetry", device_id);
    snprintf(command_topic,   sizeof(command_topic),   "hermes/%s/commands",  device_id);

    // Use the device_id as the MQTT client ID so the broker can tell devices apart
    if ((rc = MQTTClient_create(&client, broker, device_id,
                                MQTTCLIENT_PERSISTENCE_NONE, NULL)) != MQTTCLIENT_SUCCESS) {
        printf("Failed to create client, rc=%d\n", rc);
        exit(EXIT_FAILURE);
    }

    if ((rc = MQTTClient_setCallbacks(client, NULL, connlost, msgarrvd, NULL)) != MQTTCLIENT_SUCCESS) {
        printf("Failed to set callbacks, rc=%d\n", rc);
        exit(EXIT_FAILURE);
    }

    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession      = 1;
    if (mqtt_user) {
        conn_opts.username = mqtt_user;
        conn_opts.password = mqtt_pass;
    }

    if ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS) {
        printf("Failed to connect to broker, rc=%d\n", rc);
        exit(EXIT_FAILURE);
    }
    printf("Connected to %s\n", broker);

    // Subscribe to commands so the server can control us remotely
    if ((rc = MQTTClient_subscribe(client, command_topic, QOS)) == MQTTCLIENT_SUCCESS)
        printf("Listening for commands on: %s\n\n", command_topic);

    while (1) {
        Telemetry msg = TELEMETRY__INIT;
        msg.device_id = device_id;
        getComputerData(&msg);
        publishTelemetry(client, telemetry_topic, &msg);

        // Sleep in 1-second slices so we react quickly to g_interval changes
        // or a PING command without being stuck in a long sleep()
        for (int i = 0; i < g_interval; i++) {
            if (g_ping) { g_ping = 0; break; }
            sleep(1);
        }
    }

    MQTTClient_disconnect(client, 10000);
    MQTTClient_destroy(&client);
    return 0;
}

/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "mqtt_mgr.h"

#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

#include "fresh.h"
#include "log.h"
#include "radsens.h"
#include "time_mgr.h"
#include "wifi_mgr.h"

static const char *TAG = "mqtt";

#ifndef APP_VER
#define APP_VER "dev"
#endif

#define HA_STATUS_TOPIC "homeassistant/status"
#define AVAIL_ONLINE    "online"
#define AVAIL_OFFLINE   "offline"

#define BUFFER_SIZE       1024
#define KEEPALIVE_S       30
#define SOCKET_TIMEOUT_S  5
#define RECONNECT_MIN_MS  5000u
#define RECONNECT_MAX_MS  60000u
#define TICK_MS           1000u

static WiFiClient s_net;
static PubSubClient s_client(s_net);

static struct {
    const appconf_t *cfg;
    bool enabled;
    uint32_t interval_ms;

    char client_id[32];
    char t_avail[48];
    char dev_id[24];
    char unique_static[40];
    char unique_dynamic[40];
    char d_static[80];
    char d_dynamic[80];

    bool connected;
    bool subscribed;
    bool discovery_on;
    bool discovery_dirty;
    bool cleanup_done;
    bool avail_published;
    bool avail_state;
    uint32_t last_pub_mono;
    int64_t last_pub_epoch;
    uint32_t errors;

    uint32_t next_connect_at;
    uint32_t backoff_ms;
    uint32_t last_tick;
} ctx;

static bool publish_discovery_one(const char *name, const char *unique_id,
                                  const char *state_topic, const char *cfg_topic)
{
    JsonDocument doc;
    doc["name"] = name;
    doc["unique_id"] = unique_id;
    doc["state_topic"] = state_topic;
    doc["availability_topic"] = ctx.t_avail;
    doc["payload_available"] = AVAIL_ONLINE;
    doc["payload_not_available"] = AVAIL_OFFLINE;
    doc["unit_of_measurement"] = "µR/h";
    doc["state_class"] = "measurement";
    doc["icon"] = "mdi:radioactive";
    JsonObject dev = doc["device"].to<JsonObject>();
    dev["identifiers"] = ctx.dev_id;
    dev["name"] = ctx.cfg->device_name;
    dev["manufacturer"] = "ClimateGuard";
    dev["model"] = "RadSens on ESP8266";
    dev["sw_version"] = APP_VER;

    char buf[640];
    size_t n = serializeJson(doc, buf, sizeof buf);
    if (n == 0 || n >= sizeof buf) {
        return false;
    }
    return s_client.publish(cfg_topic, buf, true);
}

static void publish_discovery(void)
{
    bool ok = publish_discovery_one("Static intensity", ctx.unique_static,
                                    ctx.cfg->mqtt_static_topic, ctx.d_static);
    ok = publish_discovery_one("Dynamic intensity", ctx.unique_dynamic,
                               ctx.cfg->mqtt_dynamic_topic, ctx.d_dynamic) && ok;
    if (ok) {
        LOGI(TAG, "discovery configs published");
    } else {
        ctx.errors++;
        LOGW(TAG, "discovery publish failed");
    }
}

static void publish_discovery_cleanup(void)
{
    s_client.publish(ctx.d_static, "", true);
    s_client.publish(ctx.d_dynamic, "", true);
    ctx.cleanup_done = true;
}

static void publish_availability(bool on)
{
    if (!s_client.publish(ctx.t_avail, on ? AVAIL_ONLINE : AVAIL_OFFLINE, true)) {
        ctx.errors++;
        return;
    }
    ctx.avail_published = true;
    ctx.avail_state = on;
    LOGI(TAG, "availability: %s", on ? AVAIL_ONLINE : AVAIL_OFFLINE);
}

static void publish_readings(int64_t now_epoch)
{
    radsens_reading_t r;
    radsens_get_reading(&r);
    if (!reading_fresh(&r)) {
        return;
    }
    char val[16];
    snprintf(val, sizeof val, "%.1f", (double)r.static_urh);
    bool ok = s_client.publish(ctx.cfg->mqtt_static_topic, val, false);
    snprintf(val, sizeof val, "%.1f", (double)r.dynamic_urh);
    ok = s_client.publish(ctx.cfg->mqtt_dynamic_topic, val, false) && ok;
    if (!ok) {
        ctx.errors++;
        LOGW(TAG, "publish failed (state %d)", s_client.state());
        return;
    }
    ctx.last_pub_mono = millis();
    ctx.last_pub_epoch = now_epoch;
    LOGI(TAG, "published static %.1f dynamic %.1f", (double)r.static_urh, (double)r.dynamic_urh);
}

static void subscribe_ha_status(void)
{
    if (!s_client.subscribe(HA_STATUS_TOPIC)) {
        ctx.errors++;
        LOGW(TAG, "subscribe %s failed", HA_STATUS_TOPIC);
        return;
    }
    ctx.subscribed = true;
    LOGI(TAG, "subscribed to %s", HA_STATUS_TOPIC);
}

static void on_message(char *topic, uint8_t *payload, unsigned int len)
{
    /* Home Assistant publishes "online" here when it restarts and has
     * forgotten the discovered entities. */
    if (strcmp(topic, HA_STATUS_TOPIC) == 0 && len == strlen(AVAIL_ONLINE) &&
        memcmp(payload, AVAIL_ONLINE, len) == 0) {
        LOGI(TAG, "home assistant online, republishing discovery");
        ctx.discovery_dirty = true;
    }
}

static void try_connect(void)
{
    uint32_t now = millis();
    if ((int32_t)(now - ctx.next_connect_at) < 0) {
        return;
    }
    LOGI(TAG, "connecting to %s:%u as %s", ctx.cfg->mqtt_host, ctx.cfg->mqtt_port, ctx.client_id);
    const char *user = ctx.cfg->mqtt_username[0] != '\0' ? ctx.cfg->mqtt_username : nullptr;
    const char *pass = ctx.cfg->mqtt_password[0] != '\0' ? ctx.cfg->mqtt_password : nullptr;
    bool ok = s_client.connect(ctx.client_id, user, pass, ctx.t_avail, 1, true, AVAIL_OFFLINE, true);
    if (!ok) {
        ctx.errors++;
        LOGW(TAG, "connect failed (state %d), next try in %lu s", s_client.state(),
             (unsigned long)(ctx.backoff_ms / 1000u));
        ctx.next_connect_at = millis() + ctx.backoff_ms;
        ctx.backoff_ms = ctx.backoff_ms * 2 > RECONNECT_MAX_MS ? RECONNECT_MAX_MS : ctx.backoff_ms * 2;
        return;
    }
    ctx.connected = true;
    ctx.avail_published = false;
    ctx.discovery_dirty = true;
    ctx.subscribed = false;
    ctx.backoff_ms = RECONNECT_MIN_MS;
    LOGI(TAG, "connected to broker");
}

void mqtt_mgr_start(const appconf_t *cfg, const char *device_id)
{
    ctx.cfg = cfg;
    if (!cfg->mqtt_enabled) {
        LOGI(TAG, "mqtt disabled");
        return;
    }
    ctx.enabled = true;
    ctx.discovery_on = cfg->mqtt_discovery;
    ctx.interval_ms = (uint32_t)cfg->mqtt_interval_s * 1000u;
    ctx.backoff_ms = RECONNECT_MIN_MS;
    ctx.next_connect_at = millis();

    snprintf(ctx.client_id, sizeof ctx.client_id, "gammameter-%s", device_id);
    snprintf(ctx.t_avail, sizeof ctx.t_avail, "gammameter/%s/availability", device_id);
    snprintf(ctx.dev_id, sizeof ctx.dev_id, "gammameter_%s", device_id);
    snprintf(ctx.unique_static, sizeof ctx.unique_static, "%s_static", ctx.dev_id);
    snprintf(ctx.unique_dynamic, sizeof ctx.unique_dynamic, "%s_dynamic", ctx.dev_id);
    snprintf(ctx.d_static, sizeof ctx.d_static, "homeassistant/sensor/%s/config", ctx.unique_static);
    snprintf(ctx.d_dynamic, sizeof ctx.d_dynamic, "homeassistant/sensor/%s/config", ctx.unique_dynamic);

    s_net.setTimeout(SOCKET_TIMEOUT_S * 1000);
    s_client.setServer(cfg->mqtt_host, cfg->mqtt_port);
    s_client.setBufferSize(BUFFER_SIZE);
    s_client.setKeepAlive(KEEPALIVE_S);
    s_client.setSocketTimeout(SOCKET_TIMEOUT_S);
    s_client.setCallback(on_message);

    LOGI(TAG, "mqtt started: %s:%u interval %u s discovery %s", cfg->mqtt_host,
         cfg->mqtt_port, cfg->mqtt_interval_s, cfg->mqtt_discovery ? "on" : "off");
}

void mqtt_mgr_tick(void)
{
    if (!ctx.enabled) {
        return;
    }
    if (!wifi_mgr_sta_connected()) {
        if (ctx.connected) {
            s_client.disconnect();
            ctx.connected = false;
            LOGW(TAG, "wifi down, broker connection dropped");
        }
        ctx.next_connect_at = millis() + RECONNECT_MIN_MS;
        return;
    }
    if (!s_client.connected()) {
        if (ctx.connected) {
            ctx.connected = false;
            ctx.errors++;
            LOGW(TAG, "disconnected from broker (state %d)", s_client.state());
            ctx.next_connect_at = millis() + RECONNECT_MIN_MS;
        }
        try_connect();
        return;
    }
    s_client.loop();

    uint32_t now = millis();
    if (now - ctx.last_tick < TICK_MS) {
        return;
    }
    ctx.last_tick = now;

    radsens_reading_t r;
    radsens_get_reading(&r);
    bool on = reading_fresh(&r);
    if (!ctx.avail_published || on != ctx.avail_state) {
        publish_availability(on);
    }

    if (ctx.discovery_on && !ctx.subscribed) {
        subscribe_ha_status();
    }
    if (ctx.discovery_dirty) {
        if (ctx.discovery_on) {
            publish_discovery();
        } else if (!ctx.cleanup_done) {
            publish_discovery_cleanup();
        }
        ctx.discovery_dirty = false;
    }

    int64_t epoch = time_mgr_unix();
    if (epoch <= 0) {
        return; /* no readings with a wrong timestamp */
    }
    if (ctx.last_pub_mono != 0 && now - ctx.last_pub_mono < ctx.interval_ms) {
        return;
    }
    publish_readings(epoch);
}

void mqtt_mgr_get_status(mqtt_mgr_status_t *out)
{
    out->enabled = ctx.enabled;
    out->connected = ctx.connected;
    out->last_publish = ctx.last_pub_epoch;
    out->errors = ctx.errors;
}

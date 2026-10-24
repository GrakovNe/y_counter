/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "web_server.h"

#include <ArduinoJson.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>

#include "captive.h"
#include "fresh.h"
#include "history.h"
#include "log.h"
#include "mqtt_mgr.h"
#include "radsens.h"
#include "settings.h"
#include "tcp_reaper.h"
#include "time_mgr.h"
#include "wifi_mgr.h"

#include "info_html.h"
#include "main_html.h"
#include "setup_html.h"

static const char *TAG = "web";

#ifndef APP_VER
#define APP_VER "dev"
#endif

#define POST_BODY_MAX 2048
#define RESTART_DELAY_MS 1000u
#define REAP_PERIOD_MS 5000u
#define REAP_RETRANSMITS 4   /* ~45 s of silence with lwIP's RTO backoff */

static ESP8266WebServer s_server(80);
static const appconf_t *s_cfg;
static char s_device_id[5];
static char s_mac[18];
static uint32_t s_last_reap;
static uint32_t s_reaped;
static bool s_restart_armed;
static bool s_restart_factory;
static uint32_t s_restart_at;

/* lwIP on the ESP8266 has five TCP control blocks for everything, MQTT
 * included, and the server handles one client at a time. A browser that
 * keeps three idle connections open starves the rest, so every response
 * closes its connection. */
static void one_shot(void)
{
    s_server.keepAlive(false);
}

static void send_json(int code, const JsonDocument &doc)
{
    one_shot();
    String out;
    serializeJson(doc, out);
    s_server.send(code, F("application/json"), out);
}

static void send_json_err(const char *msg)
{
    JsonDocument doc;
    doc["error"] = msg;
    send_json(400, doc);
}

static void send_ok(void)
{
    JsonDocument doc;
    doc["ok"] = true;
    send_json(200, doc);
}

static void serve_gz(const uint8_t *gz, size_t len)
{
    one_shot();
    s_server.sendHeader(F("Content-Encoding"), F("gzip"));
    s_server.sendHeader(F("Cache-Control"), F("no-cache"));
    s_server.send_P(200, PSTR("text/html; charset=utf-8"), (PGM_P)gz, len);
}

static void handler_main(void)   { serve_gz(main_html_gz, main_html_gz_len); }
static void handler_config(void) { serve_gz(setup_html_gz, setup_html_gz_len); }
static void handler_info(void)   { serve_gz(info_html_gz, info_html_gz_len); }

static void handler_root(void)
{
    if (wifi_mgr_ap_up()) {
        handler_config();
    } else {
        handler_main();
    }
}

static void handler_wifi_scan(void)
{
    if (s_server.hasArg(F("refresh"))) {
        wifi_mgr_scan_start();
    }
    size_t n;
    const wifi_mgr_scan_entry_t *nets = wifi_mgr_scan_peek(&n);

    JsonDocument doc;
    JsonArray arr = doc["networks"].to<JsonArray>();
    for (size_t i = 0; i < n; i++) {
        JsonObject o = arr.add<JsonObject>();
        o["ssid"] = nets[i].ssid;
        o["rssi"] = nets[i].rssi;
        o["secure"] = nets[i].secure;
    }
    doc["scanning"] = wifi_mgr_scan_busy();
    send_json(200, doc);
}

static double round1(float v)
{
    return round((double)v * 10.0) / 10.0;
}

static void add_time(JsonObject o)
{
    int64_t ts = time_mgr_unix();
    if (ts < 0) {
        o["timestamp"] = nullptr;
    } else {
        o["timestamp"] = (long long)ts;
    }
}

static void handler_current(void)
{
    radsens_reading_t r;
    radsens_get_reading(&r);

    JsonDocument doc;
    add_time(doc.to<JsonObject>());
    doc["time_synced"] = time_mgr_synced();
    doc["uptime_s"] = time_mgr_uptime_s();
    doc["sensor"]["connected"] = r.connected;
    if (reading_fresh(&r)) {
        doc["static"]["urh"] = round1(r.static_urh);
        doc["static"]["age_s"] = r.age_ms / 1000u;
        doc["dynamic"]["urh"] = round1(r.dynamic_urh);
        doc["dynamic"]["age_s"] = r.age_ms / 1000u;
    } else {
        doc["static"] = nullptr;
        doc["dynamic"] = nullptr;
    }
    send_json(200, doc);
}

/* 144 records do not fit a JsonDocument comfortably in the ESP8266 heap;
 * the array is streamed in chunks. */
static void handler_history(void)
{
    static hist_rec_t recs[HIST_RING_CAP];
    uint16_t n = history_snapshot(recs);

    one_shot();
    s_server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    s_server.send(200, F("application/json"), "");

    String chunk;
    chunk.reserve(1200);
    chunk += F("{\"step_s\":");
    chunk += history_step_s();
    chunk += F(",\"records\":[");
    for (uint16_t i = 0; i < n; i++) {
        char rec[80];
        char st[12] = "null", dy[12] = "null";
        if (recs[i].static_x10 != HIST_NONE) {
            snprintf(st, sizeof st, "%u.%u", recs[i].static_x10 / 10, recs[i].static_x10 % 10);
        }
        if (recs[i].dynamic_x10 != HIST_NONE) {
            snprintf(dy, sizeof dy, "%u.%u", recs[i].dynamic_x10 / 10, recs[i].dynamic_x10 % 10);
        }
        snprintf(rec, sizeof rec, "%s{\"t\":%lu,\"static\":%s,\"dynamic\":%s}",
                 i ? "," : "", (unsigned long)recs[i].t, st, dy);
        chunk += rec;
        if (chunk.length() > 1024) {
            s_server.sendContent(chunk);
            chunk = "";
        }
    }
    chunk += F("]}");
    s_server.sendContent(chunk);
    s_server.sendContent("");
}

static const char *reset_reason_code(void)
{
    switch (ESP.getResetInfoPtr()->reason) {
    case REASON_DEFAULT_RST:      return "power_on";
    case REASON_WDT_RST:          return "watchdog";
    case REASON_EXCEPTION_RST:    return "panic";
    case REASON_SOFT_WDT_RST:     return "soft_watchdog";
    case REASON_SOFT_RESTART:     return "software";
    case REASON_DEEP_SLEEP_AWAKE: return "deep_sleep";
    case REASON_EXT_SYS_RST:      return "external";
    default:                      return "unknown";
    }
}

static void handler_status(void)
{
    radsens_reading_t r;
    radsens_get_reading(&r);
    wifi_mgr_status_t w;
    wifi_mgr_get_status(&w);
    mqtt_mgr_status_t mq;
    mqtt_mgr_get_status(&mq);

    JsonDocument doc;

    JsonObject tm = doc["time"].to<JsonObject>();
    add_time(tm);
    tm["synced"] = time_mgr_synced();
    tm["uptime_s"] = time_mgr_uptime_s();

    uint32_t heap_free;
    uint16_t heap_max;
    uint8_t heap_frag;
    ESP.getHeapStats(&heap_free, &heap_max, &heap_frag);
    FSInfo fs;
    bool fs_ok = LittleFS.info(fs);

    JsonObject dev = doc["device"].to<JsonObject>();
    dev["name"] = s_cfg->device_name;
    dev["id"] = s_device_id;
    dev["hostname"] = s_cfg->hostname;
    dev["chip"] = "ESP8266EX";
    dev["cpu_mhz"] = ESP.getCpuFreqMHz();
    dev["firmware"] = APP_VER;
    dev["core"] = ESP.getCoreVersion();
    dev["sdk"] = ESP.getSdkVersion();
    dev["build_date"] = __DATE__;
    dev["mac"] = s_mac;
    dev["flash_bytes"] = ESP.getFlashChipRealSize();
    dev["sketch_bytes"] = ESP.getSketchSize();
    dev["sketch_free_bytes"] = ESP.getFreeSketchSpace();
    dev["heap_free"] = heap_free;
    dev["heap_max_block"] = heap_max;
    dev["heap_frag"] = heap_frag;
    if (fs_ok) {
        dev["fs_used"] = fs.usedBytes;
        dev["fs_total"] = fs.totalBytes;
    } else {
        dev["fs_used"] = nullptr;
        dev["fs_total"] = nullptr;
    }
    dev["reset_reason"] = reset_reason_code();
    dev["http_dropped"] = s_reaped;

    doc["mode"] = wifi_mgr_mode_name(w.mode);

    JsonObject sensor = doc["sensor"].to<JsonObject>();
    sensor["connected"] = r.connected;
    if (r.valid) {
        sensor["static_urh"] = round1(r.static_urh);
        sensor["dynamic_urh"] = round1(r.dynamic_urh);
        sensor["age_s"] = r.age_ms / 1000u;
    } else {
        sensor["static_urh"] = nullptr;
        sensor["dynamic_urh"] = nullptr;
        sensor["age_s"] = nullptr;
    }
    sensor["pulses"] = r.pulses;
    sensor["reads_ok"] = r.reads_ok;
    sensor["reads_bad"] = r.reads_bad;
    sensor["bus_resets"] = r.bus_resets;
    if (r.connected) {
        sensor["firmware"] = r.firmware;
        sensor["sensitivity"] = r.sensitivity;
        sensor["hv_on"] = r.hv_on;
    } else {
        sensor["firmware"] = nullptr;
        sensor["sensitivity"] = nullptr;
        sensor["hv_on"] = nullptr;
    }

    JsonObject wifi = doc["wifi"].to<JsonObject>();
    wifi["connected"] = w.sta_connected;
    wifi["ssid"] = w.ssid[0] ? (const char *)w.ssid : nullptr;
    wifi["bssid"] = w.bssid[0] ? (const char *)w.bssid : nullptr;
    if (w.sta_connected) {
        wifi["rssi"] = w.rssi;
        wifi["channel"] = w.channel;
        wifi["ip"] = w.ip;
        wifi["gateway"] = w.gateway;
    } else {
        wifi["rssi"] = nullptr;
        wifi["channel"] = nullptr;
        wifi["ip"] = nullptr;
        wifi["gateway"] = nullptr;
    }
    wifi["last_disconnect"] = w.last_disconnect[0] ? (const char *)w.last_disconnect : nullptr;
    wifi["link_checks"] = w.link_checks;
    wifi["link_recoveries"] = w.link_recoveries;

    JsonObject mqtt = doc["mqtt"].to<JsonObject>();
    mqtt["enabled"] = mq.enabled;
    mqtt["connected"] = mq.connected;
    if (mq.last_publish > 0) {
        mqtt["last_publish"] = (long long)mq.last_publish;
    } else {
        mqtt["last_publish"] = nullptr;
    }
    mqtt["errors"] = mq.errors;

    send_json(200, doc);
}

static void handler_config_get(void)
{
    JsonDocument doc;
    doc["device"]["name"] = s_cfg->device_name;
    doc["wifi"]["ssid"] = s_cfg->wifi_ssid;
    doc["wifi"]["password_set"] = s_cfg->wifi_password[0] != '\0';
    doc["time"]["ntp_server"] = s_cfg->ntp_server;
    JsonObject mq = doc["mqtt"].to<JsonObject>();
    mq["enabled"] = s_cfg->mqtt_enabled;
    mq["host"] = s_cfg->mqtt_host;
    mq["port"] = s_cfg->mqtt_port;
    mq["username"] = s_cfg->mqtt_username;
    mq["password_set"] = s_cfg->mqtt_password[0] != '\0';
    mq["static_topic"] = s_cfg->mqtt_static_topic;
    mq["dynamic_topic"] = s_cfg->mqtt_dynamic_topic;
    mq["interval_s"] = s_cfg->mqtt_interval_s;
    mq["discovery"] = s_cfg->mqtt_discovery;
    send_json(200, doc);
}

/* Missing keys keep their value; a present key must have the right type. */
static bool merge_str(JsonObjectConst obj, const char *key, char *dst, size_t cap,
                      char *err, size_t err_sz)
{
    JsonVariantConst v = obj[key];
    if (v.isNull()) {
        return true;
    }
    const char *str = v.as<const char *>();
    if (!v.is<const char *>() || str == NULL || strnlen(str, cap) >= cap) {
        snprintf(err, err_sz, "\"%s\" must be a string of at most %u bytes", key, (unsigned)cap - 1);
        return false;
    }
    strlcpy(dst, str, cap);
    return true;
}

static bool merge_bool(JsonObjectConst obj, const char *key, bool *dst,
                       char *err, size_t err_sz)
{
    JsonVariantConst v = obj[key];
    if (v.isNull()) {
        return true;
    }
    if (!v.is<bool>()) {
        snprintf(err, err_sz, "\"%s\" must be true or false", key);
        return false;
    }
    *dst = v.as<bool>();
    return true;
}

static bool merge_int(JsonObjectConst obj, const char *key, int32_t *dst, int min, int max,
                      char *err, size_t err_sz)
{
    JsonVariantConst v = obj[key];
    if (v.isNull()) {
        return true;
    }
    if (!v.is<int>() || v.as<int>() < min || v.as<int>() > max) {
        snprintf(err, err_sz, "\"%s\" must be an integer between %d and %d", key, min, max);
        return false;
    }
    *dst = v.as<int>();
    return true;
}

static bool section(JsonObjectConst root, const char *key, JsonObjectConst *out,
                    char *err, size_t err_sz)
{
    JsonVariantConst v = root[key];
    if (v.isNull()) {
        *out = JsonObjectConst();
        return true;
    }
    if (!v.is<JsonObjectConst>()) {
        snprintf(err, err_sz, "\"%s\" must be an object", key);
        return false;
    }
    *out = v.as<JsonObjectConst>();
    return true;
}

static void handler_config_post(void)
{
    const String &body = s_server.arg(F("plain"));
    if (body.length() == 0 || body.length() > POST_BODY_MAX) {
        send_json_err("body is empty or too large");
        return;
    }
    JsonDocument doc;
    if (deserializeJson(doc, body)) {
        send_json_err("body is not valid JSON");
        return;
    }
    JsonObjectConst root = doc.as<JsonObjectConst>();
    if (root.isNull()) {
        send_json_err("body must be an object");
        return;
    }

    appconf_t merged;
    settings_load(&merged, s_device_id);

    char err[128] = "";
    JsonObjectConst sec;
    bool ok = section(root, "device", &sec, err, sizeof err) &&
              (sec.isNull() ||
               merge_str(sec, "name", merged.device_name, sizeof merged.device_name, err, sizeof err));

    ok = ok && section(root, "wifi", &sec, err, sizeof err) &&
         (sec.isNull() ||
          (merge_str(sec, "ssid", merged.wifi_ssid, sizeof merged.wifi_ssid, err, sizeof err) &&
           merge_str(sec, "password", merged.wifi_password, sizeof merged.wifi_password, err, sizeof err)));

    ok = ok && section(root, "time", &sec, err, sizeof err) &&
         (sec.isNull() ||
          merge_str(sec, "ntp_server", merged.ntp_server, sizeof merged.ntp_server, err, sizeof err));

    if (ok && section(root, "mqtt", &sec, err, sizeof err) && !sec.isNull()) {
        int32_t port = merged.mqtt_port;
        int32_t interval = merged.mqtt_interval_s;
        ok = merge_bool(sec, "enabled", &merged.mqtt_enabled, err, sizeof err) &&
             merge_str(sec, "host", merged.mqtt_host, sizeof merged.mqtt_host, err, sizeof err) &&
             merge_int(sec, "port", &port, 1, 65535, err, sizeof err) &&
             merge_str(sec, "username", merged.mqtt_username, sizeof merged.mqtt_username, err, sizeof err) &&
             merge_str(sec, "password", merged.mqtt_password, sizeof merged.mqtt_password, err, sizeof err) &&
             merge_str(sec, "static_topic", merged.mqtt_static_topic, sizeof merged.mqtt_static_topic, err, sizeof err) &&
             merge_str(sec, "dynamic_topic", merged.mqtt_dynamic_topic, sizeof merged.mqtt_dynamic_topic, err, sizeof err) &&
             merge_int(sec, "interval_s", &interval, APPCONF_MQTT_INTERVAL_MIN_S,
                       APPCONF_MQTT_INTERVAL_MAX_S, err, sizeof err) &&
             merge_bool(sec, "discovery", &merged.mqtt_discovery, err, sizeof err);
        if (ok) {
            merged.mqtt_port = (uint16_t)port;
            merged.mqtt_interval_s = (uint16_t)interval;
        }
    } else if (ok && err[0] != '\0') {
        ok = false;
    }

    if (ok) {
        ok = appconf_validate(&merged, err, sizeof err);
    }
    if (!ok) {
        send_json_err(err);
        return;
    }
    if (!settings_save(&merged)) {
        send_json_err("could not save settings");
        return;
    }
    appconf_derive(&merged, s_device_id);

    JsonDocument out;
    out["ok"] = true;
    char url[APPCONF_HOST_MAX + 32];
    snprintf(url, sizeof url, "http://%s.local/", merged.hostname);
    out["url"] = url;
    LOGI(TAG, "settings saved, restarting soon");
    send_json(200, out);
    web_server_schedule_restart(false);
}

static void handler_options(void)
{
    one_shot();
    s_server.sendHeader(F("Access-Control-Allow-Methods"), F("GET, POST, OPTIONS"));
    s_server.sendHeader(F("Access-Control-Allow-Headers"), F("Content-Type"));
    s_server.send(204);
}

static void handler_reboot(void)
{
    LOGW(TAG, "reboot requested over HTTP");
    send_ok();
    web_server_schedule_restart(false);
}

static void handler_factory_reset(void)
{
    LOGW(TAG, "factory reset requested over HTTP");
    send_ok();
    web_server_schedule_restart(true);
}

static void handler_not_found(void)
{
    one_shot();
    if (wifi_mgr_ap_up()) {
        /* Captive-portal probes land here and get sent to the setup page. */
        s_server.sendHeader(F("Location"), F(CAPTIVE_URL));
        s_server.send(302, F("text/html; charset=utf-8"),
                      F("<html><head><meta http-equiv=\"refresh\" content=\"0;url=" CAPTIVE_URL "\"></head>"
                        "<body>Setting up your device: <a href=\"" CAPTIVE_URL "\">Gamma Meter setup</a></body></html>"));
        return;
    }
    s_server.send(404, F("text/plain"), F("not found"));
}

void web_server_start(const appconf_t *cfg, const char device_id[5])
{
    s_cfg = cfg;
    memcpy(s_device_id, device_id, sizeof s_device_id);
    strlcpy(s_mac, WiFi.macAddress().c_str(), sizeof s_mac);

    s_server.enableCORS(true);
    s_server.on(F("/"), HTTP_GET, handler_root);
    s_server.on(F("/main"), HTTP_GET, handler_main);
    s_server.on(F("/config"), HTTP_GET, handler_config);
    s_server.on(F("/info"), HTTP_GET, handler_info);
    s_server.on(F("/api/v1/wifi/scan"), HTTP_GET, handler_wifi_scan);
    s_server.on(F("/api/v1/current"), HTTP_GET, handler_current);
    s_server.on(F("/api/v1/history"), HTTP_GET, handler_history);
    s_server.on(F("/api/v1/status"), HTTP_GET, handler_status);
    s_server.on(F("/api/v1/config"), HTTP_GET, handler_config_get);
    s_server.on(F("/api/v1/config"), HTTP_POST, handler_config_post);
    s_server.on(F("/api/v1/config"), HTTP_OPTIONS, handler_options);
    s_server.on(F("/api/v1/reboot"), HTTP_POST, handler_reboot);
    s_server.on(F("/api/v1/reboot"), HTTP_OPTIONS, handler_options);
    s_server.on(F("/api/v1/factory-reset"), HTTP_POST, handler_factory_reset);
    s_server.on(F("/api/v1/factory-reset"), HTTP_OPTIONS, handler_options);
    s_server.onNotFound(handler_not_found);
    s_server.begin();
    LOGI(TAG, "http server up on :80");
}

void web_server_schedule_restart(bool factory)
{
    s_restart_factory = s_restart_factory || factory;
    s_restart_at = millis() + RESTART_DELAY_MS;
    s_restart_armed = true;
}

/* lwIP has ten TCP segments for every connection on the chip. A browser
 * tab on a phone that went to sleep leaves a connection whose data is
 * never acknowledged; it keeps retransmitting for minutes and holds most
 * of the pool, so the next page never gets sent. lwIP would give up after
 * twelve retransmissions; this gives up after four. */
static void reap_dead_connections(void)
{
    s_reaped += tcp_reap_dead(80, REAP_RETRANSMITS);
}

uint32_t web_server_reaped(void)
{
    return s_reaped;
}

void web_server_tick(void)
{
    s_server.handleClient();
    if (millis() - s_last_reap >= REAP_PERIOD_MS) {
        s_last_reap = millis();
        reap_dead_connections();
    }
    if (s_restart_armed && (int32_t)(millis() - s_restart_at) >= 0) {
        s_restart_armed = false;
        if (s_restart_factory) {
            settings_erase();
            /* The SDK's own copy of the old credentials goes too, or the
             * legacy import would bring them back on the next boot. */
            ESP.eraseConfig();
        }
        LOGW(TAG, "restarting");
        Serial.flush();
        delay(50);
        ESP.restart();
    }
}

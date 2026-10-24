/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "settings.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

#include "log.h"

static const char *TAG = "settings";

#define CFG_PATH "/config.json"

bool settings_fs_init(void)
{
    if (!LittleFS.begin()) {
        LOGE(TAG, "LittleFS mount failed, settings and history unavailable");
        return false;
    }
    FSInfo info;
    LittleFS.info(info);
    LOGI(TAG, "LittleFS: %u of %u bytes used", info.usedBytes, info.totalBytes);
    return true;
}

static void get_str(JsonVariantConst v, char *out, size_t out_sz)
{
    const char *s = v.as<const char *>();
    if (v.is<const char *>() && s != NULL) {
        strlcpy(out, s, out_sz);
    }
}

static void get_bool(JsonVariantConst v, bool *out)
{
    if (v.is<bool>()) {
        *out = v.as<bool>();
    }
}

static void get_u16(JsonVariantConst v, uint16_t *out)
{
    if (v.is<unsigned int>()) {
        unsigned int u = v.as<unsigned int>();
        if (u <= 0xFFFF) {
            *out = (uint16_t)u;
        }
    }
}

/* A reset between writing the temporary file and renaming it leaves only
 * the .tmp; it is complete, so it is taken. */
static void recover_tmp(void)
{
    if (!LittleFS.exists(CFG_PATH) && LittleFS.exists(CFG_PATH ".tmp")) {
        LOGW(TAG, "recovering settings from %s", CFG_PATH ".tmp");
        LittleFS.rename(CFG_PATH ".tmp", CFG_PATH);
    }
}

bool settings_exist(void)
{
    recover_tmp();
    return LittleFS.exists(CFG_PATH);
}

void settings_load(appconf_t *c, const char device_id[5])
{
    appconf_defaults(c, device_id);
    recover_tmp();

    File f = LittleFS.open(CFG_PATH, "r");
    if (!f) {
        LOGI(TAG, "no saved settings, using factory defaults");
        return;
    }
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
        LOGE(TAG, "config.json: %s, using factory defaults", err.c_str());
        return;
    }

    get_str(doc["name"], c->device_name, sizeof c->device_name);
    get_str(doc["wifi_ssid"], c->wifi_ssid, sizeof c->wifi_ssid);
    get_str(doc["wifi_pass"], c->wifi_password, sizeof c->wifi_password);
    get_str(doc["ntp"], c->ntp_server, sizeof c->ntp_server);
    get_bool(doc["mq_en"], &c->mqtt_enabled);
    get_str(doc["mq_host"], c->mqtt_host, sizeof c->mqtt_host);
    get_u16(doc["mq_port"], &c->mqtt_port);
    get_str(doc["mq_user"], c->mqtt_username, sizeof c->mqtt_username);
    get_str(doc["mq_pass"], c->mqtt_password, sizeof c->mqtt_password);
    get_str(doc["mq_t_static"], c->mqtt_static_topic, sizeof c->mqtt_static_topic);
    get_str(doc["mq_t_dynamic"], c->mqtt_dynamic_topic, sizeof c->mqtt_dynamic_topic);
    get_u16(doc["mq_int"], &c->mqtt_interval_s);
    get_bool(doc["mq_disc"], &c->mqtt_discovery);

    appconf_derive(c, device_id);
}

bool settings_save(const appconf_t *c)
{
    JsonDocument doc;
    doc["name"] = c->device_name;
    doc["wifi_ssid"] = c->wifi_ssid;
    doc["wifi_pass"] = c->wifi_password;
    doc["ntp"] = c->ntp_server;
    doc["mq_en"] = c->mqtt_enabled;
    doc["mq_host"] = c->mqtt_host;
    doc["mq_port"] = c->mqtt_port;
    doc["mq_user"] = c->mqtt_username;
    doc["mq_pass"] = c->mqtt_password;
    doc["mq_t_static"] = c->mqtt_static_topic;
    doc["mq_t_dynamic"] = c->mqtt_dynamic_topic;
    doc["mq_int"] = c->mqtt_interval_s;
    doc["mq_disc"] = c->mqtt_discovery;

    /* Write to a temporary name and rename, so a reset mid-write leaves
     * the previous file intact. */
    File f = LittleFS.open(CFG_PATH ".tmp", "w");
    if (!f) {
        LOGE(TAG, "cannot open %s for writing", CFG_PATH ".tmp");
        return false;
    }
    size_t n = serializeJson(doc, f);
    f.close();
    if (n == 0) {
        LOGE(TAG, "serialize failed");
        LittleFS.remove(CFG_PATH ".tmp");
        return false;
    }
    /* lfs_rename replaces the target atomically. */
    if (!LittleFS.rename(CFG_PATH ".tmp", CFG_PATH)) {
        LOGE(TAG, "rename failed");
        return false;
    }
    LOGI(TAG, "settings saved (%u bytes)", n);
    return true;
}

void settings_erase(void)
{
    LittleFS.remove(CFG_PATH);
    LittleFS.remove(CFG_PATH ".tmp");
    LOGW(TAG, "settings erased");
}

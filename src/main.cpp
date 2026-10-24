/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>

#include "appconf.h"
#include "fresh.h"
#include "history.h"
#include "log.h"
#include "mqtt_mgr.h"
#include "radsens.h"
#include "settings.h"
#include "time_mgr.h"
#include "web_server.h"
#include "wifi_mgr.h"

static const char *TAG = "main";

#ifndef APP_VER
#define APP_VER "dev"
#endif

#define SUMMARY_PERIOD_MS 5000u
#define SENSOR_SENSITIVITY 105 /* imp/uR, the RadSens default for SBM-20 */

static appconf_t s_cfg;
static char s_device_id[5];
static bool s_mdns_up;
static uint32_t s_mdns_retry_at;
static uint32_t s_last_summary;

/* The previous firmware kept the Wi-Fi credentials in the SDK's flash
 * area. On the first boot without a settings file they are moved over. */
static void import_legacy_wifi(void)
{
    if (settings_exist()) {
        return;
    }
    String ssid = WiFi.SSID();
    String psk = WiFi.psk();
    if (ssid.length() == 0 || ssid.length() > APPCONF_SSID_MAX ||
        psk.length() > APPCONF_PASS_MAX) {
        return;
    }
    strlcpy(s_cfg.wifi_ssid, ssid.c_str(), sizeof s_cfg.wifi_ssid);
    strlcpy(s_cfg.wifi_password, psk.c_str(), sizeof s_cfg.wifi_password);
    char err[128];
    if (!appconf_validate(&s_cfg, err, sizeof err)) {
        LOGW(TAG, "SDK credentials for \"%s\" not imported: %s", s_cfg.wifi_ssid, err);
        s_cfg.wifi_ssid[0] = '\0';
        s_cfg.wifi_password[0] = '\0';
        return;
    }
    if (settings_save(&s_cfg)) {
        LOGI(TAG, "imported Wi-Fi credentials for \"%s\" from the SDK", s_cfg.wifi_ssid);
    }
}

static void mdns_tick(void)
{
    if (!wifi_mgr_sta_connected()) {
        return;
    }
    if (!s_mdns_up) {
        if ((int32_t)(millis() - s_mdns_retry_at) < 0) {
            return;
        }
        if (MDNS.begin(s_cfg.hostname)) {
            MDNS.addService("http", "tcp", 80);
            s_mdns_up = true;
            LOGI(TAG, "mdns: http://%s.local/", s_cfg.hostname);
        } else {
            LOGW(TAG, "mdns start failed, retrying in 10 s");
            s_mdns_retry_at = millis() + 10000;
        }
        return;
    }
    MDNS.update();
}

static void summary(void)
{
    radsens_reading_t r;
    radsens_get_reading(&r);
    uint32_t heap_free;
    uint16_t heap_max;
    uint8_t heap_frag;
    ESP.getHeapStats(&heap_free, &heap_max, &heap_frag);

    if (!r.connected) {
        LOGI(TAG, "waiting for sensor... (heap %u, mode %s)", heap_free,
             wifi_mgr_mode_name(wifi_mgr_mode()));
        return;
    }
    char vals[64] = "n/a";
    if (reading_fresh(&r)) {
        snprintf(vals, sizeof vals, "static %.1f dynamic %.1f uR/h (%lu s ago)",
                 (double)r.static_urh, (double)r.dynamic_urh, (unsigned long)(r.age_ms / 1000u));
    }
    LOGI(TAG, "summary: %s, reads %lu ok / %lu bad, heap %u (max %u, frag %u%%), wifi %s "
              "(sdk %d, ch %d, %d dBm)",
         vals, (unsigned long)r.reads_ok, (unsigned long)r.reads_bad, heap_free, heap_max,
         heap_frag, wifi_mgr_mode_name(wifi_mgr_mode()), (int)WiFi.status(), WiFi.channel(),
         (int)WiFi.RSSI());
}

void setup()
{
    Serial.begin(115200);
    Serial.println();
    LOGI(TAG, "Gamma Meter firmware %s (core %s, sdk %s, built %s %s)", APP_VER,
         ESP.getCoreVersion().c_str(), ESP.getSdkVersion(), __DATE__, __TIME__);
    LOGI(TAG, "reset reason: %s", ESP.getResetReason().c_str());

    settings_fs_init();

    uint8_t mac[6];
    WiFi.macAddress(mac);
    appconf_device_id(mac, s_device_id);

    appconf_defaults(&s_cfg, s_device_id);
    import_legacy_wifi();
    settings_load(&s_cfg, s_device_id);
    LOGI(TAG, "device %s: name \"%s\", hostname %s.local", s_device_id, s_cfg.device_name,
         s_cfg.hostname);
    LOGI(TAG, "wifi: %s, mqtt: %s, ntp: %s",
         s_cfg.wifi_ssid[0] != '\0' ? s_cfg.wifi_ssid : "not configured",
         s_cfg.mqtt_enabled ? s_cfg.mqtt_host : "disabled", s_cfg.ntp_server);

    const radsens_config_t rcfg = { .sensitivity = SENSOR_SENSITIVITY, .led = true, .poll_ms = 1000 };
    radsens_start(&rcfg);

    wifi_mgr_start(&s_cfg, s_device_id);
    web_server_start(&s_cfg, s_device_id);
    time_mgr_start(&s_cfg);
    history_start();
    mqtt_mgr_start(&s_cfg, s_device_id);
}

void loop()
{
    wifi_mgr_tick();
    web_server_tick();
    mdns_tick();
    radsens_tick();
    history_tick();
    mqtt_mgr_tick();
    time_mgr_tick();

    uint32_t now = millis();
    if (now - s_last_summary >= SUMMARY_PERIOD_MS) {
        s_last_summary = now;
        summary();
    }
}

/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "time_mgr.h"

#include <Arduino.h>
#include <coredecls.h>
#include <time.h>

#include "log.h"

static const char *TAG = "time";

static bool s_synced;
static uint32_t s_uptime_hi;
static uint32_t s_last_millis;

void time_mgr_start(const appconf_t *cfg)
{
    if (cfg->ntp_server[0] == '\0') {
        LOGW(TAG, "no NTP server configured, time stays unsynced");
        return;
    }
    settimeofday_cb([](bool from_sntp) {
        if (from_sntp && !s_synced) {
            s_synced = true;
            LOGI(TAG, "synced, unix time %lld", (long long)time(nullptr));
        }
    });
    /* UTC: the pages format times in the browser's zone. The server string
     * is copied by the core, cfg may go away. */
    configTime("UTC0", cfg->ntp_server);
    LOGI(TAG, "sntp started, server \"%s\"", cfg->ntp_server);
}

bool time_mgr_synced(void)
{
    return s_synced;
}

int64_t time_mgr_unix(void)
{
    if (!s_synced) {
        return -1;
    }
    return (int64_t)time(nullptr);
}

void time_mgr_tick(void)
{
    (void)time_mgr_uptime_s();
}

uint32_t time_mgr_uptime_s(void)
{
    /* millis() wraps every 49.7 days; tick() keeps the high word current. */
    uint32_t now = millis();
    if (now < s_last_millis) {
        s_uptime_hi++;
    }
    s_last_millis = now;
    return (uint32_t)((((uint64_t)s_uptime_hi << 32) | now) / 1000u);
}

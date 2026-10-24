/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "appconf.h"

/* Plain MQTT (no TLS: BearSSL does not fit next to the web server in the
 * ESP8266 heap). Readings, availability with a last will, Home Assistant
 * discovery. All client calls happen in mqtt_mgr_tick(). */

typedef struct {
    bool     enabled;
    bool     connected;
    int64_t  last_publish;   /* unix time, 0 = never */
    uint32_t errors;
} mqtt_mgr_status_t;

void mqtt_mgr_start(const appconf_t *cfg, const char *device_id);
void mqtt_mgr_tick(void);
void mqtt_mgr_get_status(mqtt_mgr_status_t *out);

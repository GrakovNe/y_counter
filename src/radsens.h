/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* ClimateGuard RadSens over I2C, polled from the main loop. The sensor is
 * powered independently of the ESP, so its counters survive our resets. */

typedef struct {
    bool     connected;      /* the last poll answered with the right chip id */
    bool     valid;          /* static_urh/dynamic_urh hold a reading */
    float    static_urh;     /* mean over the last 500 s, uR/h */
    float    dynamic_urh;    /* mean over the last <123 s, uR/h */
    uint32_t age_ms;         /* time since the reading was taken */
    uint32_t pulses;         /* Geiger pulses counted since boot */
    uint32_t reads_ok;
    uint32_t reads_bad;
    uint32_t bus_resets;
    uint8_t  firmware;       /* sensor firmware register */
    uint16_t sensitivity;    /* imp/uR as reported by the sensor */
    bool     hv_on;
} radsens_reading_t;

typedef struct {
    uint16_t sensitivity;    /* imp/uR, written when the sensor (re)appears */
    bool     led;
    uint32_t poll_ms;        /* 0 = 1000 */
} radsens_config_t;

void radsens_start(const radsens_config_t *cfg);

/* Called from loop(). Polls the sensor, applies the fault policy. */
void radsens_tick(void);

void radsens_get_reading(radsens_reading_t *out);

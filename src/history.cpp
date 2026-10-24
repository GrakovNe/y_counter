/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "history.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <math.h>

#include "fresh.h"
#include "log.h"
#include "radsens.h"
#include "time_mgr.h"

static const char *TAG = "history";

#define HIST_PATH "/history.bin"

#ifndef HIST_WINDOW_S
#define HIST_WINDOW_S 600u
#endif
#ifndef HIST_SAMPLE_MS
#define HIST_SAMPLE_MS 5000u
#endif

static hist_store_t s_store;
static bool s_ready;
static uint32_t s_last_sample;

static uint32_t s_window;
static float s_static_sum, s_dynamic_sum;
static uint16_t s_n;

static void store_save(void)
{
    File f = LittleFS.open(HIST_PATH ".tmp", "w");
    if (!f) {
        LOGE(TAG, "cannot open %s", HIST_PATH ".tmp");
        return;
    }
    size_t n = f.write((const uint8_t *)&s_store, sizeof s_store);
    f.close();
    if (n != sizeof s_store) {
        LOGE(TAG, "short write (%u of %u)", n, sizeof s_store);
        LittleFS.remove(HIST_PATH ".tmp");
        return;
    }
    if (!LittleFS.rename(HIST_PATH ".tmp", HIST_PATH)) {
        LOGE(TAG, "rename failed");
    }
}

static uint16_t to_x10(float v)
{
    long x = lroundf(v * 10.0f);
    if (x < 0) {
        return 0;
    }
    if (x >= (long)HIST_NONE) {
        return HIST_NONE - 1;
    }
    return (uint16_t)x;
}

static void close_window(void)
{
    if (s_n == 0) {
        return;
    }
    /* After a backwards clock step the window may be older than the ring's
     * newest record; the chart cannot show two records for one time. */
    if (s_store.count > 0 && s_window < hist_store_newest_t(&s_store)) {
        LOGW(TAG, "window %lu is older than the ring, dropped", (unsigned long)s_window);
        return;
    }
    hist_rec_t r;
    r.t = s_window;
    r.static_x10 = to_x10(s_static_sum / (float)s_n);
    r.dynamic_x10 = to_x10(s_dynamic_sum / (float)s_n);
    hist_store_append(&s_store, r);
    store_save();
    LOGI(TAG, "window %lu: static %u.%u, dynamic %u.%u uR/h (n %u)",
         (unsigned long)r.t, r.static_x10 / 10, r.static_x10 % 10,
         r.dynamic_x10 / 10, r.dynamic_x10 % 10, s_n);
}

static void sample_once(void)
{
    int64_t now = time_mgr_unix();
    if (now < 0) {
        return;
    }
    uint32_t window = (uint32_t)(now - now % HIST_WINDOW_S);

    if (s_window == 0) {
        s_window = window;
    } else if (window != s_window) {
        if (window > s_window) {
            close_window();
        }
        s_window = window;
        s_static_sum = s_dynamic_sum = 0;
        s_n = 0;
    }

    radsens_reading_t rd;
    radsens_get_reading(&rd);
    if (reading_fresh(&rd)) {
        s_static_sum += rd.static_urh;
        s_dynamic_sum += rd.dynamic_urh;
        s_n++;
    }
}

void history_start(void)
{
    if (!LittleFS.exists(HIST_PATH) && LittleFS.exists(HIST_PATH ".tmp")) {
        LittleFS.rename(HIST_PATH ".tmp", HIST_PATH);
    }
    File f = LittleFS.open(HIST_PATH, "r");
    bool loaded = false;
    if (f) {
        if (f.size() == sizeof s_store &&
            f.read((uint8_t *)&s_store, sizeof s_store) == sizeof s_store &&
            hist_store_check(&s_store)) {
            loaded = true;
        }
        f.close();
    }
    if (loaded) {
        LOGI(TAG, "ring loaded: %u records", s_store.count);
    } else {
        hist_store_init(&s_store);
        store_save();
        LOGI(TAG, "ring created");
    }
    s_ready = true;
}

void history_tick(void)
{
    if (!s_ready) {
        return;
    }
    uint32_t now = millis();
    if (now - s_last_sample < HIST_SAMPLE_MS) {
        return;
    }
    s_last_sample = now;
    sample_once();
}

uint16_t history_snapshot(hist_rec_t *out)
{
    if (!s_ready) {
        return 0;
    }
    return hist_store_sorted(&s_store, out);
}

uint32_t history_step_s(void)
{
    return HIST_WINDOW_S;
}

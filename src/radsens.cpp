/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "radsens.h"

#include <Arduino.h>
#include <Wire.h>

#include "log.h"

static const char *TAG = "radsens";

#define RS_ADDR            0x66
#define RS_CHIP_ID         0x7D
#define RS_FRAME_LEN       21
#define RS_REG_HV          0x11
#define RS_REG_SENS_LO     0x12
#define RS_REG_SENS_HI     0x13
#define RS_REG_LED         0x14

#define DEFAULT_POLL_MS    1000u
#define FAILS_TO_DISCONNECT 3
#define FAILS_TO_BUS_RESET  10
#define I2C_CLOCK_HZ       100000

static struct {
    radsens_config_t  cfg;
    radsens_reading_t r;
    uint32_t          poll_ms;
    uint32_t          last_poll;
    uint32_t          reading_at;
    uint32_t          fail_streak;
    bool              configured;  /* sensor registers written since it appeared */
} ctx;

/* Releases a slave that holds SDA low after an interrupted transfer:
 * nine clock pulses with SDA high, then a STOP. */
static void bus_recover(void)
{
    ctx.r.bus_resets++;
    LOGW(TAG, "I2C bus reset (%lu failures in a row)", (unsigned long)ctx.fail_streak);
    pinMode(SDA, INPUT_PULLUP);
    pinMode(SCL, OUTPUT_OPEN_DRAIN);
    digitalWrite(SCL, HIGH);
    delayMicroseconds(5);
    for (int i = 0; i < 9 && digitalRead(SDA) == LOW; i++) {
        digitalWrite(SCL, LOW);
        delayMicroseconds(5);
        digitalWrite(SCL, HIGH);
        delayMicroseconds(5);
    }
    pinMode(SDA, OUTPUT_OPEN_DRAIN);
    digitalWrite(SDA, LOW);
    delayMicroseconds(5);
    digitalWrite(SCL, HIGH);
    delayMicroseconds(5);
    digitalWrite(SDA, HIGH);
    delayMicroseconds(5);
    Wire.begin();
    Wire.setClock(I2C_CLOCK_HZ);
}

static bool write_reg(uint8_t reg, uint8_t value)
{
    Wire.beginTransmission(RS_ADDR);
    Wire.write(reg);
    Wire.write(value);
    bool ok = Wire.endTransmission(true) == 0;
    delay(15); /* the sensor stores register writes in its EEPROM */
    return ok;
}

/* Register writes go to the sensor's EEPROM, so only what differs from the
 * frame just read is written; a flaky bus must not wear it out. */
static void configure_sensor(const uint8_t *d)
{
    uint16_t sens = (uint16_t)(d[19] << 8) | d[18];
    bool ok = true;
    int writes = 0;
    if (d[17] != 1) {
        ok = write_reg(RS_REG_HV, 1) && ok;
        writes++;
    }
    if (sens != ctx.cfg.sensitivity) {
        ok = write_reg(RS_REG_SENS_LO, (uint8_t)(ctx.cfg.sensitivity & 0xFF)) && ok;
        ok = write_reg(RS_REG_SENS_HI, (uint8_t)(ctx.cfg.sensitivity >> 8)) && ok;
        writes++;
    }
    if ((d[20] == 1) != ctx.cfg.led) {
        ok = write_reg(RS_REG_LED, ctx.cfg.led ? 1 : 0) && ok;
        writes++;
    }
    ctx.configured = ok;
    if (!ok) {
        LOGW(TAG, "configuration write failed, retrying on the next poll");
    } else if (writes > 0) {
        LOGI(TAG, "configured: HV on, sensitivity %u imp/uR, led %s (%d registers written)",
             ctx.cfg.sensitivity, ctx.cfg.led ? "on" : "off", writes);
    } else {
        LOGI(TAG, "sensor already configured: HV on, sensitivity %u imp/uR, led %s",
             sens, d[20] == 1 ? "on" : "off");
    }
}

static bool read_frame(uint8_t *d)
{
    Wire.beginTransmission(RS_ADDR);
    Wire.write(0x00);
    if (Wire.endTransmission(true) != 0) {
        return false;
    }
    if (Wire.requestFrom((uint8_t)RS_ADDR, (uint8_t)RS_FRAME_LEN) != RS_FRAME_LEN) {
        while (Wire.available()) {
            Wire.read();
        }
        return false;
    }
    for (int i = 0; i < RS_FRAME_LEN; i++) {
        d[i] = (uint8_t)Wire.read();
    }
    return d[0] == RS_CHIP_ID;
}

static void poll(void)
{
    uint8_t d[RS_FRAME_LEN];
    bool ok = read_frame(d);
    uint32_t now = millis();

    if (!ok) {
        ctx.r.reads_bad++;
        ctx.fail_streak++;
        if (ctx.fail_streak == FAILS_TO_DISCONNECT) {
            LOGW(TAG, "sensor not answering, marking disconnected");
            ctx.r.connected = false;
            ctx.configured = false;
        }
        if (ctx.fail_streak >= FAILS_TO_BUS_RESET &&
            ctx.fail_streak % FAILS_TO_BUS_RESET == 0) {
            bus_recover();
        }
        return;
    }

    if (!ctx.r.connected) {
        LOGI(TAG, "sensor connected, firmware %u", d[1]);
        ctx.r.connected = true;
    }
    ctx.fail_streak = 0;
    ctx.r.reads_ok++;
    ctx.r.firmware = d[1];
    ctx.r.dynamic_urh = (float)(((uint32_t)d[3] << 16) | ((uint32_t)d[4] << 8) | d[5]) / 10.0f;
    ctx.r.static_urh  = (float)(((uint32_t)d[6] << 16) | ((uint32_t)d[7] << 8) | d[8]) / 10.0f;
    ctx.r.pulses += ((uint32_t)d[9] << 8) | d[10];
    ctx.r.hv_on = d[17] == 1;
    ctx.r.sensitivity = (uint16_t)(d[19] << 8) | d[18];
    ctx.r.valid = true;
    ctx.reading_at = now;

    if (!ctx.configured) {
        configure_sensor(d);
    }
}

void radsens_start(const radsens_config_t *cfg)
{
    ctx.cfg = *cfg;
    ctx.poll_ms = cfg->poll_ms ? cfg->poll_ms : DEFAULT_POLL_MS;
    Wire.begin();
    Wire.setClock(I2C_CLOCK_HZ);
    ctx.last_poll = millis() - ctx.poll_ms; /* poll on the first tick */
    LOGI(TAG, "I2C ready (SDA %d, SCL %d), polling every %lu ms",
         SDA, SCL, (unsigned long)ctx.poll_ms);
}

void radsens_tick(void)
{
    uint32_t now = millis();
    if (now - ctx.last_poll < ctx.poll_ms) {
        return;
    }
    ctx.last_poll = now;
    poll();
}

void radsens_get_reading(radsens_reading_t *out)
{
    *out = ctx.r;
    out->age_ms = ctx.r.valid ? millis() - ctx.reading_at : 0;
}

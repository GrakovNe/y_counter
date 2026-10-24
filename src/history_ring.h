/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Ring of ten-minute averages for the last 24 hours. Plain data and plain
 * functions, so the host tests can compile it. Intensities are stored in
 * tenths of a microroentgen per hour; HIST_NONE marks a missing value. */

#define HIST_RING_CAP 144
#define HIST_MAGIC    0x47535431u
#define HIST_VERSION  1

#define HIST_NONE 0xFFFFu

typedef struct {
    uint32_t t;            /* window start, unix seconds */
    uint16_t static_x10;
    uint16_t dynamic_x10;
} hist_rec_t;

typedef struct {
    uint32_t   magic;
    uint16_t   version;
    uint16_t   next;
    uint16_t   count;
    uint16_t   pad;
    hist_rec_t rec[HIST_RING_CAP];
} hist_store_t;

void hist_store_init(hist_store_t *s);

/* False when the magic or version is wrong; otherwise clamps next/count
 * into range so a damaged header cannot index outside the ring. */
bool hist_store_check(hist_store_t *s);

/* A record with the same timestamp as the newest one replaces it. */
void hist_store_append(hist_store_t *s, hist_rec_t r);

/* Timestamp of the newest record; 0 when the ring is empty. */
uint32_t hist_store_newest_t(const hist_store_t *s);

/* Copies the records oldest first into out[HIST_RING_CAP]; returns count. */
uint16_t hist_store_sorted(const hist_store_t *s, hist_rec_t *out);

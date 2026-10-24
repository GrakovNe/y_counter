/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdint.h>

#include "history_ring.h"

/* Ten-minute averages of both intensities, kept in /history.bin on
 * LittleFS. Records start once the clock has synced. */

void history_start(void);
void history_tick(void);

uint16_t history_snapshot(hist_rec_t *out);
uint32_t history_step_s(void);

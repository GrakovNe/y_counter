/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include "radsens.h"

/* The sensor is polled every second; 30 s without a good frame means the
 * bus or the sensor is gone and the last value must not be published. */
#define FRESH_MAX_AGE_MS 30000u

static inline bool reading_fresh(const radsens_reading_t *r)
{
    return r->valid && r->age_ms <= FRESH_MAX_AGE_MS;
}

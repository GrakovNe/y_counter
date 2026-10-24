/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "appconf.h"

void time_mgr_start(const appconf_t *cfg);

bool time_mgr_synced(void);

/* Unix time, or -1 before the first NTP sync. */
int64_t time_mgr_unix(void);

/* Called from loop() so the millis() wrap is never missed. */
void time_mgr_tick(void);

uint32_t time_mgr_uptime_s(void);

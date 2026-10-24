/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <IPAddress.h>

/* DNS on :53 that answers every A query with the AP address, so phones
 * open the setup page by themselves. Served from the main loop. */
#define CAPTIVE_URL "http://192.168.4.1/"

void captive_start(const IPAddress &ap_ip);
void captive_stop(void);
void captive_tick(void);

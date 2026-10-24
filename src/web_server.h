/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include "appconf.h"

/* HTTP on :80: three pages and the JSON API, see README. Also owns the
 * delayed restart so a response leaves before the device reboots. */
void web_server_start(const appconf_t *cfg, const char device_id[5]);
void web_server_tick(void);

/* Restart in a second; with factory=true settings are erased first. */
void web_server_schedule_restart(bool factory);

/* Dead port-80 connections aborted so far. */
uint32_t web_server_reaped(void);

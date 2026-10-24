/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include "appconf.h"

/* Settings live in /config.json on LittleFS. The file system is mounted
 * here and formatted once if it is blank. */
bool settings_fs_init(void);

/* Never fails: defaults, then whatever the file has. */
void settings_load(appconf_t *c, const char device_id[5]);

bool settings_save(const appconf_t *c);

bool settings_exist(void);

/* Removes the settings file. History is kept. */
void settings_erase(void);

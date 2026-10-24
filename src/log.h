/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <Arduino.h>

/* "I (12345) tag: message" on the UART console, format strings in flash. */
#define LOG_(lvl, tag, fmt, ...)                                              \
    Serial.printf_P(PSTR("%c (%lu) %s: " fmt "\n"), lvl,                     \
                    (unsigned long)millis(), tag, ##__VA_ARGS__)

#define LOGI(tag, fmt, ...) LOG_('I', tag, fmt, ##__VA_ARGS__)
#define LOGW(tag, fmt, ...) LOG_('W', tag, fmt, ##__VA_ARGS__)
#define LOGE(tag, fmt, ...) LOG_('E', tag, fmt, ##__VA_ARGS__)

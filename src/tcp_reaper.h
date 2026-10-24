/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdint.h>

/* Aborts every TCP connection on local_port that has retransmitted at
 * least max_retransmits times. Returns how many were dropped. Lives in
 * its own file: lwIP's tcp_state enum clashes with ESP8266WiFi.h. */
uint32_t tcp_reap_dead(uint16_t local_port, uint8_t max_retransmits);

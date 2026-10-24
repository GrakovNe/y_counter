/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "tcp_reaper.h"

#include <Arduino.h>
#include <IPAddress.h>
#include <lwip/priv/tcp_priv.h>
#include <lwip/tcp.h>

#include "log.h"

static const char *TAG = "tcp";

uint32_t tcp_reap_dead(uint16_t local_port, uint8_t max_retransmits)
{
    uint32_t dropped = 0;
    struct tcp_pcb *pcb = tcp_active_pcbs;
    while (pcb != NULL) {
        struct tcp_pcb *next = pcb->next;
        if (pcb->local_port == local_port && pcb->nrtx >= max_retransmits) {
            LOGW(TAG, "dropping dead connection from %s:%u (%u retransmits, state %d)",
                 IPAddress(pcb->remote_ip.addr).toString().c_str(), pcb->remote_port,
                 pcb->nrtx, (int)pcb->state);
            tcp_abort(pcb);
            dropped++;
        }
        pcb = next;
    }
    return dropped;
}

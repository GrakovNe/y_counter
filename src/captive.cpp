/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "captive.h"

#include <DNSServer.h>

#include "log.h"

static const char *TAG = "captive";

static DNSServer s_dns;
static bool s_running;

void captive_start(const IPAddress &ap_ip)
{
    if (s_running) {
        return;
    }
    s_dns.setTTL(60);
    s_dns.setErrorReplyCode(DNSReplyCode::NoError);
    if (!s_dns.start(53, "*", ap_ip)) {
        LOGE(TAG, "DNS start failed");
        return;
    }
    s_running = true;
    LOGI(TAG, "DNS on :53 -> %s", ap_ip.toString().c_str());
}

void captive_stop(void)
{
    if (!s_running) {
        return;
    }
    s_dns.stop();
    s_running = false;
    LOGI(TAG, "DNS off");
}

void captive_tick(void)
{
    if (s_running) {
        s_dns.processNextRequest();
    }
}

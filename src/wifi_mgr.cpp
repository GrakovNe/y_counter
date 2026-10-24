/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "wifi_mgr.h"

#include <ESP8266WiFi.h>
#include <lwip/etharp.h>
#include <lwip/netif.h>
#include <string.h>

#include "captive.h"
#include "log.h"

static const char *TAG = "wifi-mgr";

#define CONNECT_TIMEOUT_MS   (120u * 1000u)
#define FALLBACK_RETRY_MS    (60u * 1000u)
#define CONNECTING_RETRY_MS  (5u * 1000u)
#define PENDING_GUARD_MS     (20u * 1000u)
#define SCAN_TIMEOUT_MS      (15u * 1000u)
#define SCAN_RESUME_MS       500u
#define LINK_CHECK_MS        (30u * 1000u)
#define LINK_ARP_MISSES      3

static const IPAddress AP_IP(192, 168, 4, 1);
static const IPAddress AP_MASK(255, 255, 255, 0);

static struct {
    char ssid[APPCONF_SSID_MAX + 1];
    char password[APPCONF_PASS_MAX + 1];
    char ap_ssid[APPCONF_NAME_MAX + 1];

    wifi_mgr_mode_t mode;
    uint32_t mode_since;
    bool ap_up;
    bool sta_has_ip;
    bool connect_pending;
    uint32_t pending_since;
    bool got_ip_event;
    bool user_disconnect;
    bool scan_busy;
    bool scan_requested;
    uint32_t scan_since;
    uint32_t retry_at;
    bool retry_armed;

    uint32_t link_checked_at;
    uint8_t  arp_misses;

    wifi_mgr_status_t status;

    wifi_mgr_scan_entry_t scan[WIFI_MGR_SCAN_MAX];
    size_t scan_count;

    WiFiEventHandler on_connected, on_disconnected, on_got_ip, on_dhcp_timeout,
                     on_ap_join, on_ap_leave;
} s;

const char *wifi_mgr_mode_name(wifi_mgr_mode_t mode)
{
    switch (mode) {
    case WIFI_MGR_PROVISIONING: return "provisioning";
    case WIFI_MGR_CONNECTING:   return "connecting";
    case WIFI_MGR_NORMAL:       return "normal";
    case WIFI_MGR_FALLBACK:     return "fallback";
    }
    return "?";
}

static const char *disconnect_reason_name(uint8_t reason)
{
    switch (reason) {
    case WIFI_DISCONNECT_REASON_AUTH_EXPIRE:
    case WIFI_DISCONNECT_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_DISCONNECT_REASON_AUTH_FAIL:
    case WIFI_DISCONNECT_REASON_HANDSHAKE_TIMEOUT:
        return "wrong_password";
    case WIFI_DISCONNECT_REASON_NO_AP_FOUND:
        return "not_found";
    case WIFI_DISCONNECT_REASON_BEACON_TIMEOUT:
        return "lost";
    default:
        return "other";
    }
}

static void set_mode(wifi_mgr_mode_t mode)
{
    if (s.mode == mode) {
        return;
    }
    LOGI(TAG, "mode %s -> %s", wifi_mgr_mode_name(s.mode), wifi_mgr_mode_name(mode));
    s.mode = mode;
    s.mode_since = millis();
    s.status.mode = mode;
}

static void arm_retry(uint32_t delay_ms)
{
    s.retry_at = millis() + delay_ms;
    s.retry_armed = true;
}

static void ap_start(void)
{
    if (s.ap_up) {
        return;
    }
    WiFi.softAPConfig(AP_IP, AP_IP, AP_MASK);
    if (!WiFi.softAP(s.ap_ssid, nullptr, 1, false, 4)) {
        LOGE(TAG, "softAP failed");
        return;
    }
    s.ap_up = true;
    s.status.ap_up = true;
    captive_start(AP_IP);
    LOGI(TAG, "AP \"%s\" up, %s", s.ap_ssid, CAPTIVE_URL);
}

static void ap_stop(void)
{
    if (!s.ap_up) {
        return;
    }
    captive_stop();
    WiFi.softAPdisconnect(true);
    s.ap_up = false;
    s.status.ap_up = false;
    LOGI(TAG, "AP off");
}

static void sta_connect(void)
{
    if (s.ssid[0] == '\0' || s.connect_pending) {
        return;
    }
    WiFi.enableSTA(true);
    s.user_disconnect = false;
    WiFi.begin(s.ssid, s.password[0] != '\0' ? s.password : nullptr);
    s.connect_pending = true;
    s.pending_since = millis();
    LOGI(TAG, "connecting to \"%s\"", s.ssid);
}

/* The station lost its link without the SDK telling us. Drop it and let
 * the connecting state machine bring it back. */
static void force_reconnect(const char *why)
{
    LOGW(TAG, "link lost without an event (%s), reconnecting", why);
    strlcpy(s.status.last_disconnect, "silent", sizeof s.status.last_disconnect);
    s.status.link_recoveries++;
    s.sta_has_ip = false;
    s.connect_pending = false;
    s.arp_misses = 0;
    set_mode(WIFI_MGR_CONNECTING);
    s.user_disconnect = true;
    WiFi.disconnect(false);
    arm_retry(SCAN_RESUME_MS);
}

static struct netif *sta_netif(void)
{
    struct netif *n;
    NETIF_FOREACH(n) {
        if (n->name[0] == 's' && n->name[1] == 't') {
            return n;
        }
    }
    return nullptr;
}

/* Every LINK_CHECK_MS in normal mode: the SDK must still report an IP, and
 * the gateway must have answered the ARP request sent on the previous
 * check. A router that changes channel can leave the ESP8266 associated
 * on the old one with no disconnect event; this is what catches it. */
static void link_check(void)
{
    if (WiFi.status() != WL_CONNECTED || !WiFi.localIP().isSet()) {
        force_reconnect("sdk status");
        return;
    }
    IPAddress gw = WiFi.gatewayIP();
    struct netif *n = sta_netif();
    if (!gw.isSet() || n == nullptr) {
        return;
    }
    s.status.link_checks++;
    ip4_addr_t gw_ip;
    ip4_addr_set_u32(&gw_ip, (uint32_t)gw);
    struct eth_addr *eth;
    const ip4_addr_t *ipr;
    if (etharp_find_addr(n, &gw_ip, &eth, &ipr) < 0) {
        s.arp_misses++;
        if (s.arp_misses >= LINK_ARP_MISSES) {
            force_reconnect("gateway not answering ARP");
            return;
        }
    } else {
        s.arp_misses = 0;
    }
    etharp_request(n, &gw_ip);
}

static void on_scan_done(int n)
{
    /* static: the SDK stack that runs this callback is small. */
    static wifi_mgr_scan_entry_t scratch[WIFI_MGR_SCAN_MAX];
    size_t count = 0;

    if (n < 0) {
        LOGW(TAG, "scan failed (%d)", n);
    } else {
        for (int i = 0; i < n; i++) {
            String ssid = WiFi.SSID(i);
            if (ssid.length() == 0 || ssid.length() > APPCONF_SSID_MAX) {
                continue;
            }
            int8_t rssi = (int8_t)WiFi.RSSI(i);
            bool secure = WiFi.encryptionType(i) != ENC_TYPE_NONE;
            size_t j;
            for (j = 0; j < count; j++) {
                if (strcmp(scratch[j].ssid, ssid.c_str()) == 0) {
                    if (rssi > scratch[j].rssi) {
                        scratch[j].rssi = rssi;
                    }
                    break;
                }
            }
            if (j == count && count < WIFI_MGR_SCAN_MAX) {
                strlcpy(scratch[count].ssid, ssid.c_str(), sizeof scratch[count].ssid);
                scratch[count].rssi = rssi;
                scratch[count].secure = secure;
                count++;
            }
        }
        for (size_t i = 1; i < count; i++) {
            wifi_mgr_scan_entry_t key = scratch[i];
            size_t j = i;
            while (j > 0 && scratch[j - 1].rssi < key.rssi) {
                scratch[j] = scratch[j - 1];
                j--;
            }
            scratch[j] = key;
        }
        memcpy(s.scan, scratch, count * sizeof scratch[0]);
        s.scan_count = count;
        LOGI(TAG, "scan done: %u networks", (unsigned)count);
    }
    WiFi.scanDelete();
    s.scan_busy = false;

    if (s.ssid[0] != '\0' && !s.sta_has_ip) {
        s.connect_pending = false;
        arm_retry(SCAN_RESUME_MS);
    }
}

static void handle_scan(void)
{
    /* A scan cannot start while the station is associating. */
    if (s.ssid[0] != '\0' && !s.sta_has_ip) {
        s.user_disconnect = true;
        WiFi.disconnect(false);
        s.connect_pending = false;
    }
    s.scan_since = millis();
    WiFi.scanNetworksAsync(on_scan_done, true);
    if (WiFi.scanComplete() == WIFI_SCAN_FAILED) {
        LOGW(TAG, "scan start failed");
        s.user_disconnect = false;
        s.scan_busy = false;
        if (s.ssid[0] != '\0' && !s.sta_has_ip) {
            arm_retry(SCAN_RESUME_MS);
        }
    }
}

static void lost_connection(const char *name)
{
    strlcpy(s.status.last_disconnect, name, sizeof s.status.last_disconnect);
    arm_retry(s.mode == WIFI_MGR_FALLBACK ? FALLBACK_RETRY_MS : CONNECTING_RETRY_MS);
    if (s.mode == WIFI_MGR_NORMAL) {
        set_mode(WIFI_MGR_CONNECTING);
    }
}

void wifi_mgr_start(const appconf_t *cfg, const char device_id[5])
{
    strlcpy(s.ssid, cfg->wifi_ssid, sizeof s.ssid);
    strlcpy(s.password, cfg->wifi_password, sizeof s.password);
    appconf_default_name(device_id, s.ap_ssid, sizeof s.ap_ssid);
    strlcpy(s.status.ssid, cfg->wifi_ssid, sizeof s.status.ssid);

    /* Credentials live in our settings file; the SDK must not write its
     * own copy to flash on every begin(). */
    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false);
    WiFi.hostname(cfg->hostname);
    WiFi.setSleepMode(WIFI_NONE_SLEEP);

    s.on_connected = WiFi.onStationModeConnected([](const WiFiEventStationModeConnected &ev) {
        /* connect_pending stays set until the IP arrives: DHCP is part of
         * the attempt and must not be restarted by the retry timer. */
        s.status.channel = ev.channel;
        snprintf(s.status.bssid, sizeof s.status.bssid, "%02x:%02x:%02x:%02x:%02x:%02x",
                 ev.bssid[0], ev.bssid[1], ev.bssid[2], ev.bssid[3], ev.bssid[4], ev.bssid[5]);
        LOGI(TAG, "associated with %s on channel %u, waiting for IP", s.status.bssid, ev.channel);
    });

    s.on_disconnected = WiFi.onStationModeDisconnected([](const WiFiEventStationModeDisconnected &ev) {
        s.sta_has_ip = false;
        /* ASSOC_LEAVE is the SDK dropping an attempt we replaced with begin();
         * user_disconnect marks our own disconnect() before a scan. Neither
         * is a failure of the replacement attempt, so connect_pending stays.
         * In normal mode there is no attempt in flight: every drop counts. */
        bool ours = s.user_disconnect || ev.reason == WIFI_DISCONNECT_REASON_ASSOC_LEAVE;
        if (ours && s.mode != WIFI_MGR_NORMAL) {
            LOGI(TAG, "disconnected (reason %u, ours)", ev.reason);
            return;
        }
        s.connect_pending = false;
        const char *name = disconnect_reason_name(ev.reason);
        LOGW(TAG, "disconnected (reason %u -> %s)", ev.reason, name);
        lost_connection(name);
    });

    s.on_dhcp_timeout = WiFi.onStationModeDHCPTimeout([]() {
        s.sta_has_ip = false;
        s.connect_pending = false;
        LOGW(TAG, "DHCP timeout");
        lost_connection("dhcp");
    });

    s.on_got_ip = WiFi.onStationModeGotIP([](const WiFiEventStationModeGotIP &ev) {
        s.sta_has_ip = true;
        s.connect_pending = false;
        s.arp_misses = 0;
        s.link_checked_at = millis();
        strlcpy(s.status.ip, ev.ip.toString().c_str(), sizeof s.status.ip);
        strlcpy(s.status.gateway, ev.gw.toString().c_str(), sizeof s.status.gateway);
        LOGI(TAG, "got IP %s, gateway %s", s.status.ip, s.status.gateway);
        s.got_ip_event = true; /* mode change and AP teardown happen in tick() */
    });

    s.on_ap_join = WiFi.onSoftAPModeStationConnected([](const WiFiEventSoftAPModeStationConnected &ev) {
        LOGI(TAG, "phone joined AP %02x:%02x:%02x:%02x:%02x:%02x",
             ev.mac[0], ev.mac[1], ev.mac[2], ev.mac[3], ev.mac[4], ev.mac[5]);
    });
    s.on_ap_leave = WiFi.onSoftAPModeStationDisconnected([](const WiFiEventSoftAPModeStationDisconnected &ev) {
        LOGI(TAG, "phone left AP %02x:%02x:%02x:%02x:%02x:%02x",
             ev.mac[0], ev.mac[1], ev.mac[2], ev.mac[3], ev.mac[4], ev.mac[5]);
    });

    if (s.ssid[0] == '\0') {
        set_mode(WIFI_MGR_PROVISIONING);
        ap_start();
    } else {
        set_mode(WIFI_MGR_CONNECTING);
        sta_connect();
    }
}

void wifi_mgr_tick(void)
{
    captive_tick();

    if (s.got_ip_event) {
        s.got_ip_event = false;
        set_mode(WIFI_MGR_NORMAL);
        ap_stop();
    }

    uint32_t now = millis();

    if (s.scan_busy && now - s.scan_since >= SCAN_TIMEOUT_MS) {
        LOGW(TAG, "scan did not finish in %u s", SCAN_TIMEOUT_MS / 1000u);
        s.scan_busy = false;
        if (s.ssid[0] != '\0' && !s.sta_has_ip) {
            arm_retry(0);
        }
    }
    if (s.scan_requested && !s.scan_busy) {
        s.scan_requested = false;
        s.scan_busy = true;
        handle_scan();
    }

    if (s.connect_pending && now - s.pending_since >= PENDING_GUARD_MS) {
        LOGW(TAG, "no result from the SDK in %u s, retrying", PENDING_GUARD_MS / 1000u);
        s.connect_pending = false;
        arm_retry(0);
    }

    bool retry_due = s.retry_armed && (int32_t)(now - s.retry_at) >= 0;

    switch (s.mode) {
    case WIFI_MGR_NORMAL:
        if (!s.sta_has_ip) {
            /* An event path that forgot to leave normal mode. */
            set_mode(WIFI_MGR_CONNECTING);
            arm_retry(CONNECTING_RETRY_MS);
        } else if (now - s.link_checked_at >= LINK_CHECK_MS) {
            s.link_checked_at = now;
            link_check();
        }
        break;
    case WIFI_MGR_CONNECTING:
        if (!s.sta_has_ip && !s.scan_busy) {
            if (retry_due) {
                s.retry_armed = false;
                sta_connect();
                arm_retry(CONNECTING_RETRY_MS);
            }
            if (now - s.mode_since >= CONNECT_TIMEOUT_MS) {
                LOGW(TAG, "no connection for %u s, opening the fallback AP",
                     CONNECT_TIMEOUT_MS / 1000u);
                ap_start();
                set_mode(WIFI_MGR_FALLBACK);
                arm_retry(FALLBACK_RETRY_MS);
            }
        }
        break;
    case WIFI_MGR_FALLBACK:
        if (!s.sta_has_ip && !s.scan_busy && retry_due) {
            s.retry_armed = false;
            sta_connect();
            arm_retry(FALLBACK_RETRY_MS);
        }
        break;
    default:
        break;
    }
}

wifi_mgr_mode_t wifi_mgr_mode(void)
{
    return s.mode;
}

bool wifi_mgr_ap_up(void)
{
    return s.ap_up;
}

bool wifi_mgr_sta_connected(void)
{
    return s.sta_has_ip;
}

void wifi_mgr_get_status(wifi_mgr_status_t *out)
{
    *out = s.status;
    out->sta_connected = s.sta_has_ip;
    if (s.sta_has_ip) {
        out->rssi = (int8_t)WiFi.RSSI();
        out->channel = (uint8_t)WiFi.channel();
    }
}

void wifi_mgr_scan_start(void)
{
    if (s.scan_busy || s.scan_requested) {
        return;
    }
    s.scan_requested = true;
}

bool wifi_mgr_scan_busy(void)
{
    return s.scan_busy || s.scan_requested;
}

const wifi_mgr_scan_entry_t *wifi_mgr_scan_peek(size_t *count)
{
    *count = s.scan_count;
    return s.scan;
}

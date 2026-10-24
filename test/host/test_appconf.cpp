/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "appconf.h"

#include <string.h>

#include "tinytest.h"

static const char ID[] = "1a2b";

static void test_device_id(void)
{
    const uint8_t mac1[6] = { 0x94, 0xb9, 0x7e, 0x14, 0x8a, 0xb8 };
    char id[5];
    appconf_device_id(mac1, id);
    CHECK_STR(id, "8ab8");

    const uint8_t mac2[6] = { 0, 0, 0, 0, 0xff, 0x0a };
    appconf_device_id(mac2, id);
    CHECK_STR(id, "ff0a");
}

static void test_hostname(void)
{
    char h[APPCONF_HOST_MAX + 1];

    appconf_hostname("GammaMeter-1A2B", ID, h, sizeof h);
    CHECK_STR(h, "gammameter-1a2b");

    appconf_hostname("  --foo--bar--  ", ID, h, sizeof h);
    CHECK_STR(h, "foo-bar");

    appconf_hostname("a_b!", ID, h, sizeof h);
    CHECK_STR(h, "a-b");

    appconf_hostname("Ünïcode Spaced Name!", ID, h, sizeof h);
    CHECK_STR(h, "n-code-spaced-name");

    appconf_hostname("Дозиметр", ID, h, sizeof h);
    CHECK_STR(h, "gammameter-1a2b");

    appconf_hostname("", ID, h, sizeof h);
    CHECK_STR(h, "gammameter-1a2b");

    char long_name[APPCONF_NAME_MAX + 1];
    memset(long_name, 'a', sizeof long_name - 1);
    long_name[sizeof long_name - 1] = '\0';
    appconf_hostname(long_name, ID, h, sizeof h);
    CHECK(strlen(h) == APPCONF_NAME_MAX);
}

static void test_defaults(void)
{
    appconf_t c;
    appconf_defaults(&c, ID);
    CHECK_STR(c.device_name, "GammaMeter-1A2B");
    CHECK_STR(c.hostname, "gammameter-1a2b");
    CHECK_STR(c.ntp_server, "pool.ntp.org");
    CHECK(c.mqtt_port == 1883);
    CHECK(c.mqtt_interval_s == 30);
    CHECK(!c.mqtt_enabled);
    CHECK(!c.mqtt_discovery);
    CHECK_STR(c.mqtt_static_topic, "gammameter/1a2b/static");
    CHECK_STR(c.mqtt_dynamic_topic, "gammameter/1a2b/dynamic");
    CHECK(c.wifi_ssid[0] == '\0');

    char err[128] = "";
    CHECK(appconf_validate(&c, err, sizeof err));
}

static void test_validate(void)
{
    appconf_t c;
    char err[128];

    appconf_defaults(&c, ID);
    c.device_name[0] = '\0';
    CHECK(!appconf_validate(&c, err, sizeof err));

    appconf_defaults(&c, ID);
    strcpy(c.wifi_ssid, "net");
    strcpy(c.wifi_password, "short");
    CHECK(!appconf_validate(&c, err, sizeof err));
    strcpy(c.wifi_password, "longenough");
    CHECK(appconf_validate(&c, err, sizeof err));
    c.wifi_password[0] = '\0';
    CHECK(appconf_validate(&c, err, sizeof err)); /* open network */

    appconf_defaults(&c, ID);
    strcpy(c.wifi_password, "orphan-pass");
    CHECK(!appconf_validate(&c, err, sizeof err));

    appconf_defaults(&c, ID);
    strcpy(c.ntp_server, "bad host");
    CHECK(!appconf_validate(&c, err, sizeof err));

    /* MQTT fields are ignored while MQTT is off. */
    appconf_defaults(&c, ID);
    c.mqtt_enabled = false;
    c.mqtt_host[0] = '\0';
    c.mqtt_interval_s = 1;
    CHECK(appconf_validate(&c, err, sizeof err));

    c.mqtt_enabled = true;
    CHECK(!appconf_validate(&c, err, sizeof err));
    strcpy(c.mqtt_host, "192.168.1.102");
    CHECK(!appconf_validate(&c, err, sizeof err)); /* interval 1 */
    c.mqtt_interval_s = 30;
    CHECK(appconf_validate(&c, err, sizeof err));
    strcpy(c.mqtt_static_topic, "has space");
    CHECK(!appconf_validate(&c, err, sizeof err));
    strcpy(c.mqtt_static_topic, "a/+/b");
    CHECK(!appconf_validate(&c, err, sizeof err));
    strcpy(c.mqtt_static_topic, "a/b");
    c.mqtt_port = 0;
    CHECK(!appconf_validate(&c, err, sizeof err));
}

int main(void)
{
    test_device_id();
    test_hostname();
    test_defaults();
    test_validate();
    TT_REPORT("appconf");
}

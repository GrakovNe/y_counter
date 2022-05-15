#include <Arduino.h>
#include <Wire.h>
#include <ESP8266mDNS.h>
#include "lib/rad_sense/radSens1v2.h"
#include "lib/wifi_manager/WiFiManager.h"
#include "ui.h"

#define DEVICE_NAME "ymeter"

ESP8266WebServer httpServer(80);
WiFiManager wm;
ClimateGuard_RadSens1v2 radSens(RS_DEFAULT_I2C_ADDRESS);

float staticIntensity = 0;
float dynamicIntensity = 0;
uint32_t pulseCounter = 0;

unsigned long counter = 0;

String to_json() {
    return "{"
           "\"staticIntensity\": " +
           String(staticIntensity) +
           ",\"dynamicIntensity\": " +
           String(dynamicIntensity) +
           ",\"pulseCounter\": " +
           String(pulseCounter) +
           "}";
}

void restServerRouting() {
    httpServer.on("/statistic", HTTP_GET, []() {
        httpServer.sendHeader("Access-Control-Allow-Origin", "*");
        httpServer.send(200, F("application/json"), to_json());
    });

    httpServer.onNotFound([]() {
        httpServer.sendHeader("Access-Control-Allow-Origin", "*");
        httpServer.send(200, F("text/html; charset=utf-8"), mainPage);
    });

}

void configure_wlan() {
    wm.setTitle(DEVICE_NAME);

    WiFi.hostname(DEVICE_NAME);
    wm.setHostname(DEVICE_NAME);
    wm.setHttpPort(8080);

    WiFi.mode(WIFI_STA);
    wm.setCaptivePortalEnable(false);
    wm.setConfigPortalBlocking(false);

    if (wm.autoConnect(DEVICE_NAME)) {
        wm.startConfigPortal();
    }

    restServerRouting();
    httpServer.begin();

    MDNS.begin(DEVICE_NAME, WiFi.localIP());
}

void init_sensor() {
    radSens.radSens_init();
    radSens.setSensitivity(105);
    radSens.setHVGeneratorState(true);
    radSens.setLedState(true);
}

void update_data() {
    dynamicIntensity = radSens.getRadIntensyDynamic();
    staticIntensity = radSens.getRadIntensyStatic();
    pulseCounter = radSens.getNumberOfPulses();
}

void update_and_publish() {
    update_data();
    Serial.println(to_json());
}

void setup() {
    Serial.begin(115200);

    configure_wlan();
    init_sensor();

    counter = millis();
}

void loop() {
    wm.process();
    httpServer.handleClient();
    MDNS.update();

    if (millis() - counter >= 1000) {
        update_and_publish();
        counter = millis();
    }
}
#include <Arduino.h>
#include <Wire.h>
#include <ESP8266mDNS.h>
#include "lib/rad_sense/radSens1v2.h"
#include "lib/wifi_manager/WiFiManager.h"
#include "lib/timer/ESP8266TimerInterrupt.h"

#define TIMER_INTERVAL_2S             2000L

ESP8266Timer timer;
ESP8266WebServer httpServer(8080);
WiFiManager wm;
ClimateGuard_RadSens1v2 radSens(RS_DEFAULT_I2C_ADDRESS);

float staticIntensity = 0;
float dynamicIntensity = 0;
uint32_t pulseCounter = 0;

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
        httpServer.send(200, F("application/json"),to_json());
    });
}

void configure_wlan() {
    wm.setTitle("Y-Meter");

    WiFi.mode(WIFI_STA);
    if (wm.autoConnect("radsense")) {
        wm.setConfigPortalBlocking(false);
        wm.startConfigPortal();

        restServerRouting();
        httpServer.begin();
    } else {
        Serial.println("Failed to connect, resetting...");
        wm.resetSettings();
        EspClass::restart();
    }
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

    timer.setInterval(TIMER_INTERVAL_2S * 1000, update_and_publish);
}

void loop() {
    wm.process();
    httpServer.handleClient();
}
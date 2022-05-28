#include <Arduino.h>
#include <Wire.h>
#include <ESP8266mDNS.h>
#include "lib/rad_sense/radSens1v2.h"
#include "lib/wifi_manager/WiFiManager.h"
#include "ui.h"
#include "measurement_history_service.h"

#define DEVICE_NAME "ymeter"

ESP8266WebServer httpServer(80);
WiFiManager wm;
ClimateGuard_RadSens1v2 radSens(RS_DEFAULT_I2C_ADDRESS);

float staticIntensity = 0;
float dynamicIntensity = 0;

unsigned long current_timer_counter = 0;
unsigned long historical_timer_counter = 0;

long static_intensity_offset;
long dynamic_intensity_offset;

float static_intensity_history[STATIC_INTENSITY_CAPACITY];
float dynamic_intensity_history[STATIC_INTENSITY_CAPACITY];

String offset_array_to_string(float array[], int size, long offset) {

    float offset_array[size];

    int index = 0;
    for (int i = offset; i < size; i++) {
        offset_array[index] = array[i];
        index++;
    }

    for (int i = 0; i < offset; i++) {
        offset_array[index] = array[i];
        index++;
    }

    String result = "[";

    for (int i = 0; i < size - 1; i++) {
        result += String(offset_array[i]);
        result += ", ";
    }

    result += String(offset_array[size - 1]);
    result += "]";

    return result;
}

String historical_values_to_json() {
    return "{"
           "\"staticIntensityOffset\": " +
           String(static_intensity_offset) +
           ",\"dynamicIntensityOffset\": " +
           String(dynamic_intensity_offset) +
           ",\"staticIntensity\": " +
           offset_array_to_string(static_intensity_history, STATIC_INTENSITY_CAPACITY, static_intensity_offset) +
           ",\"dynamicIntensity\": " +
           offset_array_to_string(dynamic_intensity_history, DYNAMIC_INTENSITY_CAPACITY, dynamic_intensity_offset) +
           "}";
}

String current_values_to_json() {
    return "{"
           "\"staticIntensity\": " +
           String(staticIntensity) +
           ",\"dynamicIntensity\": " +
           String(dynamicIntensity) +
           "}";
}

void restServerRouting() {
    httpServer.on("/statistic", HTTP_GET, []() {
        httpServer.sendHeader("Access-Control-Allow-Origin", "*");
        httpServer.send(200, F("application/json"), current_values_to_json());
    });

    httpServer.on("/history", HTTP_GET, []() {
        httpServer.sendHeader("Access-Control-Allow-Origin", "*");
        httpServer.send(200, F("application/json"), historical_values_to_json());
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
}

void update_and_publish() {
    update_data();
    Serial.println(current_values_to_json());
}

void setup() {
    Serial.begin(115200);
    init_persistence();

    configure_wlan();
    init_sensor();

    current_timer_counter = millis();
    historical_timer_counter = millis();
}

void loop() {
    wm.process();
    httpServer.handleClient();
    MDNS.update();

    if (millis() - current_timer_counter >= 1000) {
        update_and_publish();
        current_timer_counter = millis();
    }

    if (millis() - historical_timer_counter >= 5 * 1000 * 60) {
        persist_static_intensity(staticIntensity);
        persist_dynamic_intensity(dynamicIntensity);
        historical_timer_counter = millis();
    }
}
#include <EEPROM.h>
#include "measurement_history_service.h"

void read_array(int address, float array[], int size) {
    for (int i = 0; i < size; i++) {
        EEPROM.get(address + sizeof(i) * i, array[i]);
    }
}

void persist_array(int address, float array[], int size) {
    float persisted_values[size];
    read_array(address, persisted_values, size);

    for (int i = 0; i < size; i++) {
        if (persisted_values[i] != array[i]) {
            EEPROM.put(address + sizeof(i) * i, array[i]);
        }
    }
}

void persist_static_intensity(float staticIntensity) {
    static_intensity_history[current_offset] = staticIntensity;

    current_offset = (current_offset + 1) % STATIC_INTENSITY_CAPACITY;

    EEPROM.begin(2048);
    EEPROM.put(OFFSET_ADDRESS, current_offset);
    persist_array(STATIC_INTENSITY_ADDRESS, static_intensity_history, STATIC_INTENSITY_CAPACITY);
    EEPROM.commit();
}

void init_persistence() {
    EEPROM.begin(2048);

    if (!EEPROM.read(NON_FIRST_RUN_ADDRESS)) {
        EEPROM.begin(2048);

        persist_static_intensity(0);
        EEPROM.put(NON_FIRST_RUN_ADDRESS, true);

        EEPROM.commit();
    }

    EEPROM.begin(2048);
    EEPROM.get(OFFSET_ADDRESS, current_offset);

    read_array(STATIC_INTENSITY_ADDRESS, static_intensity_history, STATIC_INTENSITY_CAPACITY);
    EEPROM.end();
}

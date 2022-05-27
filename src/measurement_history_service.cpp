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

void persist_dynamic_intensity(float dynamicIntensity) {
    dynamic_intensity_history[dynamic_intensity_offset] = dynamicIntensity;

    dynamic_intensity_offset = dynamic_intensity_offset % DYNAMIC_INTENSITY_CAPACITY;

    EEPROM.begin(4096);
    persist_array(DYNAMIC_INTENSITY_ADDRESS, static_intensity_history, DYNAMIC_INTENSITY_CAPACITY);

    dynamic_intensity_offset++;
    EEPROM.put(DYNAMIC_INTENSITY_OFFSET_ADDRESS, dynamic_intensity_offset);
    EEPROM.commit();
}

void persist_static_intensity(float staticIntensity) {
    static_intensity_history[static_intensity_offset] = staticIntensity;

    static_intensity_offset = static_intensity_offset % STATIC_INTENSITY_CAPACITY;

    EEPROM.begin(4096);
    persist_array(STATIC_INTENSITY_ADDRESS, static_intensity_history, STATIC_INTENSITY_CAPACITY);


    static_intensity_offset++;
    EEPROM.put(STATIC_INTENSITY_OFFSET_ADDRESS, static_intensity_offset);
    EEPROM.commit();
}

void init_persistence() {
    EEPROM.begin(4096);

    if (!EEPROM.read(NON_FIRST_RUN_ADDRESS)) {
        EEPROM.begin(4096);

        persist_static_intensity(0);
        persist_dynamic_intensity(0);

        EEPROM.put(NON_FIRST_RUN_ADDRESS, true);
        EEPROM.put(STATIC_INTENSITY_OFFSET_ADDRESS, 0);
        EEPROM.put(DYNAMIC_INTENSITY_OFFSET_ADDRESS, 0);
        EEPROM.commit();
    }

    EEPROM.begin(4096);
    EEPROM.get(STATIC_INTENSITY_OFFSET_ADDRESS, static_intensity_offset);
    EEPROM.get(DYNAMIC_INTENSITY_OFFSET_ADDRESS, dynamic_intensity_offset);

    read_array(STATIC_INTENSITY_ADDRESS, static_intensity_history, STATIC_INTENSITY_CAPACITY);
    read_array(DYNAMIC_INTENSITY_ADDRESS, dynamic_intensity_history, DYNAMIC_INTENSITY_CAPACITY);
    EEPROM.end();
}

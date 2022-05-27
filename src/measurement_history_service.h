#define NON_FIRST_RUN_ADDRESS 10
#define DYNAMIC_INTENSITY_OFFSET_ADDRESS 20
#define STATIC_INTENSITY_OFFSET_ADDRESS 30

#define STATIC_INTENSITY_CAPACITY 288
#define STATIC_INTENSITY_ADDRESS 40

#define DYNAMIC_INTENSITY_CAPACITY 288
#define DYNAMIC_INTENSITY_ADDRESS 1200

extern long static_intensity_offset;
extern long dynamic_intensity_offset;
extern float static_intensity_history[STATIC_INTENSITY_CAPACITY]; // one times per 5 minutes of last 24 hours. 1152 bytes
extern float dynamic_intensity_history[DYNAMIC_INTENSITY_CAPACITY]; // one times per 5 minutes of last 24 hours. 1152 bytes

void persist_static_intensity(float staticIntensity);
void persist_dynamic_intensity(float dynamicIntensity);
void init_persistence();
#define NON_FIRST_RUN_ADDRESS 10
#define OFFSET_ADDRESS 30

#define STATIC_INTENSITY_CAPACITY 288
#define STATIC_INTENSITY_ADDRESS 40

extern long current_offset;
extern float static_intensity_history[STATIC_INTENSITY_CAPACITY]; // one times per 5 minutes of last 24 hours. 1152 bytes

void persist_static_intensity(float staticIntensity);
void init_persistence();
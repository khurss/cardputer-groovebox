#pragma once
// Audio code/tables stay off the flash instruction/data cache on ESP32. This keeps
// waveform rendering and SD work from evicting the real-time DSP working set.
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define GB_AUDIO_CODE IRAM_ATTR
#define GB_AUDIO_DATA DRAM_ATTR
#else
#define GB_AUDIO_CODE
#define GB_AUDIO_DATA
#endif

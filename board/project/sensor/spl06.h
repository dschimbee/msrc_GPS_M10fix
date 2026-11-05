#ifndef SPL06_H
#define SPL06_H

#include "common.h"

// Processing mode of the SPL06 (change this one line before compiling):
//   1 = extended: glitch rejection, averaging (config "averaging vario"), reference pressure (P0) as mean of several
//       readings, vertical speed from a sliding window that is refreshed at every reading, vario interval down to 40 ms
//   0 = standard: the same processing as BMP280, BMP180 and MS5611: no filtering and no averaging, P0 is the first
//       reading, vertical speed is the altitude difference over the vario interval (at least 250 ms)
#define SPL06_EXTENDED_PROCESSING 1

#define SPL06_SENSOR_INTERVAL_MS 40  // time between two readings

// Smallest vario interval (ms) accepted for the SPL06. Extended: equal to the reading interval, a shorter window would
// not contain a new reading. Standard: 250 ms like all other sensors.
#if SPL06_EXTENDED_PROCESSING
#define SPL06_VARIO_INTERVAL_MIN_MS SPL06_SENSOR_INTERVAL_MS
#else
#define SPL06_VARIO_INTERVAL_MIN_MS 250
#endif

typedef struct spl06_parameters_t {
    float alpha_vario;
    bool auto_offset;
    uint16_t vario_interval;
    float *temperature, *pressure, *altitude, *vspeed;
    uint32_t *alt_ts;
} spl06_parameters_t;

typedef struct spl06_calibration_t {
    int16_t c0, c1;
    int32_t c00, c10;
    int16_t c01, c11, c20, c21, c30;
    float kp, kt;  // scale factors of the configured oversampling
} spl06_calibration_t;

extern context_t context;

void spl06_task(void *parameters);

#endif

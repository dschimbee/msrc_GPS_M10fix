#ifndef SPL06_H
#define SPL06_H

#include "common.h"

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

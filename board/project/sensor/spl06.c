#include "spl06.h"

#include <math.h>
#include <stdio.h>

#include "auto_offset.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"

/* Goertek SPL06-001 barometric pressure sensor */

#define REGISTER_PSR_B2 0x00  // 0x00-0x02 pressure, 0x03-0x05 temperature (24 bit, two's complement)
#define REGISTER_PRS_CFG 0x06
#define REGISTER_TMP_CFG 0x07
#define REGISTER_MEAS_CFG 0x08
#define REGISTER_CFG_REG 0x09
#define REGISTER_RESET 0x0C
#define REGISTER_ID 0x0D
#define REGISTER_COEF 0x10  // 0x10-0x21 (18 bytes)

#define MEAS_CFG_COEF_RDY 0x80
#define MEAS_CFG_SENSOR_RDY 0x40
#define MEAS_CTRL_CONTINUOUS 0x07  // continuous pressure and temperature measurement
#define TMP_CFG_EXTERNAL 0x80      // use external (MEMS) temperature sensor, as required by the datasheet
#define CFG_REG_T_SHIFT 0x08       // required for temperature oversampling > 8
#define CFG_REG_P_SHIFT 0x04       // required for pressure oversampling > 8
#define RESET_SOFT 0x89

// Rate: measurements per second (0=1, 1=2, 2=4, 3=8, 4=16, 5=32, 6=64, 7=128)
// Precision: oversampling (0=1, 1=2, 2=4, 3=8, 4=16, 5=32, 6=64, 7=128)
// Pressure 32/s x8 (14.8 ms), temperature 8/s x4 (8.4 ms): both fit in one 31 ms slot. No shift bits needed (<= 8x)
#define PRESSURE_RATE 5
#define PRESSURE_PRECISION 3
#define TEMPERATURE_RATE 3
#define TEMPERATURE_PRECISION 2
#define SCALE_FACTOR_PRESSURE 7864320.0f     // x8
#define SCALE_FACTOR_TEMPERATURE 3670016.0f  // x4

#define I2C_ADDRESS_1 0x77  // SDO high (default of most breakout boards)
#define I2C_ADDRESS_2 0x76  // SDO low
#define SENSOR_INTERVAL_MS 40

// Start-up: the first readings are discarded so that the sensor can settle, then the reference pressure (P0) is the
// mean of P0_READINGS raw readings instead of a single noisy one.
#define DISCARD_READINGS 25  // 1 s at SENSOR_INTERVAL_MS
#define P0_READINGS 10
// A jump between two readings that is larger than this is an I2C glitch (250 Pa are about 20 m)
#define MAX_PRESSURE_STEP 250.0f

static void read(spl06_parameters_t *parameter, spl06_calibration_t *calibration, uint8_t address);
static void update_vspeed(float *vspeed, uint32_t now, float altitude, uint32_t window_us);
static uint8_t begin(spl06_calibration_t *calibration);
static void parse_calibration(const uint8_t *data, spl06_calibration_t *calibration);
static int32_t sign_extend(uint32_t value, uint bits);
static void compensate(const spl06_calibration_t *calibration, int32_t raw_pressure, int32_t raw_temperature,
                       float *temperature, float *pressure);

void spl06_task(void *parameters) {
    spl06_parameters_t parameter = *(spl06_parameters_t *)parameters;
    xTaskNotifyGive(context.receiver_task_handle);
    *parameter.altitude = 0;
    *parameter.vspeed = 0;
    *parameter.temperature = 0;
    *parameter.pressure = 0;
    *parameter.alt_ts = 0;

    spl06_calibration_t calibration;
    vTaskDelay(500 / portTICK_PERIOD_MS);

    uint8_t address = begin(&calibration);
    while (1) {
        read(&parameter, &calibration, address);
        debug("\nSPL06 (%u) < Temp: %.2f Pressure: %.0f Altitude: %0.2f Vspeed: %.2f",
              uxTaskGetStackHighWaterMark(NULL), *parameter.temperature, *parameter.pressure, *parameter.altitude,
              *parameter.vspeed);
        vTaskDelay(SENSOR_INTERVAL_MS / portTICK_PERIOD_MS);
    }
}

static void read(spl06_parameters_t *parameter, spl06_calibration_t *calibration, uint8_t address) {
    uint8_t data[6];
    static float pressure_initial = 0;
    static float pressure_filtered = 0;
    static float pressure_last = 0;
    static float pressure_sum = 0;
    static uint pressure_count = 0;
    static uint discard_readings = DISCARD_READINGS;
    float pressure_raw;

    data[0] = REGISTER_PSR_B2;
    i2c_write_blocking(i2c0, address, data, 1, true);
    if (i2c_read_blocking(i2c0, address, data, 6, false) != 6) return;
    int32_t raw_pressure = sign_extend(((uint32_t)data[0] << 16) | ((uint32_t)data[1] << 8) | data[2], 24);
    int32_t raw_temperature = sign_extend(((uint32_t)data[3] << 16) | ((uint32_t)data[4] << 8) | data[5], 24);

    if (raw_pressure == 0 && raw_temperature == 0) return;  // no measurement available yet (registers still at reset value)

    compensate(calibration, raw_pressure, raw_temperature, parameter->temperature, &pressure_raw);  // Pa

    // Ignore a reading that jumps from the previous one: it is a glitch. The reading after it is compared with the
    // glitch and ignored too, the third one is accepted again (so a real step is not blocked for long).
    float pressure_previous = pressure_last;
    pressure_last = pressure_raw;
    if (pressure_previous != 0 && fabsf(pressure_raw - pressure_previous) > MAX_PRESSURE_STEP) return;

    // Exponential moving average on the pressure (config: averaging elements vario). get_average() handles
    // alpha > 1 (no smoothing). During the start-up discard phase the filter follows the raw value.
    if (pressure_filtered == 0 || discard_readings > 0)
        pressure_filtered = pressure_raw;
    else
        pressure_filtered = get_average(parameter->alpha_vario, pressure_filtered, pressure_raw);
    *parameter->pressure = pressure_filtered;

    if (discard_readings > 0) {
        discard_readings--;
        return;
    }
    if (pressure_initial == 0) {  // reference pressure: mean of the first P0_READINGS readings
        pressure_sum += pressure_raw;
        if (++pressure_count < P0_READINGS) return;
        pressure_initial = pressure_sum / pressure_count;
        debug("\nSPL06 P0: %.0f", pressure_initial);
    }
    *parameter->altitude = get_altitude(*parameter->pressure, *parameter->temperature, pressure_initial);
    uint32_t now = time_us_32();
    *parameter->alt_ts = now;
    update_vspeed(parameter->vspeed, now, *parameter->altitude, (uint32_t)parameter->vario_interval * 1000);
#ifdef SIM_SENSORS
    *parameter->temperature = 12.34;
    *parameter->pressure = 1234.56;
    *parameter->altitude = 2345;
#endif
}

static uint8_t begin(spl06_calibration_t *calibration) {
    i2c_init(i2c0, 100 * 1000);
    gpio_set_function(I2C0_SDA_GPIO, GPIO_FUNC_I2C);
    gpio_set_function(I2C0_SCL_GPIO, GPIO_FUNC_I2C);
    gpio_pull_up(I2C0_SDA_GPIO);
    gpio_pull_up(I2C0_SCL_GPIO);

    uint8_t data[18] = {0};

    // Find sensor address
    uint8_t address = I2C_ADDRESS_1;
    data[0] = REGISTER_ID;
    if (i2c_write_blocking(i2c0, I2C_ADDRESS_1, data, 1, true) < 0) address = I2C_ADDRESS_2;
    i2c_read_blocking(i2c0, address, data, 1, false);  // complete the transaction
    debug("\nSPL06 address: 0x%X id: 0x%X", address, data[0]);

    // Soft reset, then wait until coefficients and sensor are ready (max 40 ms)
    data[0] = REGISTER_RESET;
    data[1] = RESET_SOFT;
    i2c_write_blocking(i2c0, address, data, 2, false);
    vTaskDelay(50 / portTICK_PERIOD_MS);
    for (uint i = 0; i < 10; i++) {
        data[0] = REGISTER_MEAS_CFG;
        i2c_write_blocking(i2c0, address, data, 1, true);
        i2c_read_blocking(i2c0, address, data, 1, false);
        if ((data[0] & (MEAS_CFG_COEF_RDY | MEAS_CFG_SENSOR_RDY)) == (MEAS_CFG_COEF_RDY | MEAS_CFG_SENSOR_RDY)) break;
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    // Read calibration coefficients
    data[0] = REGISTER_COEF;
    i2c_write_blocking(i2c0, address, data, 1, true);
    i2c_read_blocking(i2c0, address, data, 18, false);
    parse_calibration(data, calibration);
    calibration->kp = SCALE_FACTOR_PRESSURE;
    calibration->kt = SCALE_FACTOR_TEMPERATURE;

    // Configure sensor
    data[0] = REGISTER_PRS_CFG;
    data[1] = (PRESSURE_RATE << 4) | PRESSURE_PRECISION;
    i2c_write_blocking(i2c0, address, data, 2, false);
    data[0] = REGISTER_TMP_CFG;
    // The calibration coefficients c0/c1 refer to the external (MEMS) temperature sensor: TMP_EXT must be 1
    data[1] = TMP_CFG_EXTERNAL | (TEMPERATURE_RATE << 4) | TEMPERATURE_PRECISION;
    i2c_write_blocking(i2c0, address, data, 2, false);
    data[0] = REGISTER_CFG_REG;
    data[1] = 0;
    i2c_write_blocking(i2c0, address, data, 2, false);
    data[0] = REGISTER_MEAS_CFG;
    data[1] = MEAS_CTRL_CONTINUOUS;
    i2c_write_blocking(i2c0, address, data, 2, false);
    return address;
}

static int32_t sign_extend(uint32_t value, uint bits) {
    uint32_t mask = 1u << (bits - 1);
    return (int32_t)((value ^ mask) - mask);
}

static void parse_calibration(const uint8_t *data, spl06_calibration_t *calibration) {
    calibration->c0 = sign_extend(((uint32_t)data[0] << 4) | (data[1] >> 4), 12);
    calibration->c1 = sign_extend((((uint32_t)data[1] & 0x0F) << 8) | data[2], 12);
    calibration->c00 = sign_extend(((uint32_t)data[3] << 12) | ((uint32_t)data[4] << 4) | (data[5] >> 4), 20);
    calibration->c10 = sign_extend((((uint32_t)data[5] & 0x0F) << 16) | ((uint32_t)data[6] << 8) | data[7], 20);
    calibration->c01 = (int16_t)(((uint16_t)data[8] << 8) | data[9]);
    calibration->c11 = (int16_t)(((uint16_t)data[10] << 8) | data[11]);
    calibration->c20 = (int16_t)(((uint16_t)data[12] << 8) | data[13]);
    calibration->c21 = (int16_t)(((uint16_t)data[14] << 8) | data[15]);
    calibration->c30 = (int16_t)(((uint16_t)data[16] << 8) | data[17]);
}

static void compensate(const spl06_calibration_t *calibration, int32_t raw_pressure, int32_t raw_temperature,
                       float *temperature, float *pressure) {
    float traw_sc = (float)raw_temperature / calibration->kt;
    float praw_sc = (float)raw_pressure / calibration->kp;
    *temperature = (float)calibration->c0 * 0.5f + (float)calibration->c1 * traw_sc;
    *pressure = (float)calibration->c00 +
                praw_sc * ((float)calibration->c10 +
                           praw_sc * ((float)calibration->c20 + praw_sc * (float)calibration->c30)) +
                traw_sc * (float)calibration->c01 +
                traw_sc * praw_sc * ((float)calibration->c11 + praw_sc * (float)calibration->c21);
}

// Vertical speed from a sliding window: every cycle the altitude is compared with the altitude from at least
// window_us ago, so the value is refreshed at the sensor rate instead of once per window.
#define VSPEED_BUFFER_SIZE 80  // covers 3.2 s at 40 ms, enough for the maximum vario interval (2.55 s)
static void update_vspeed(float *vspeed, uint32_t now, float altitude, uint32_t window_us) {
    static uint32_t ts[VSPEED_BUFFER_SIZE];
    static float alt[VSPEED_BUFFER_SIZE];
    static uint head = 0, count = 0;  // head: newest entry

    head = (head + 1) % VSPEED_BUFFER_SIZE;
    ts[head] = now;
    alt[head] = altitude;
    if (count < VSPEED_BUFFER_SIZE) count++;

    for (uint k = 1; k < count; k++) {  // newest entry that is at least window_us old
        uint i = (head + VSPEED_BUFFER_SIZE - k) % VSPEED_BUFFER_SIZE;
        uint32_t dt = now - ts[i];
        if (dt >= window_us) {
            *vspeed = (altitude - alt[i]) / (dt / 1000000.0f);
            return;
        }
    }
    // not enough history yet: keep the previous value
}

/**
 * @file bme280.c
 * @brief Pilote minimal du BME280 / BMP280 (voir bme280.h).
 */
#include "bme280.h"

#define REG_CALIB0   0x88U   /* 26 octets : T1..P9, puis H1 en 0xA1 */
#define REG_CHIP_ID  0xD0U
#define REG_CALIB1   0xE1U   /* 7 octets : H2..H6 */
#define REG_CTRL_HUM 0xF2U
#define REG_STATUS   0xF3U
#define REG_CTRL_MEAS 0xF4U
#define REG_CONFIG   0xF5U
#define REG_DATA     0xF7U   /* 8 octets : pression, température, humidité */

#define STATUS_MEASURING 0x08U
#define CTRL_HUM_X1      0x01U
#define CTRL_MEAS_FORCED 0x25U  /* osrs_t x1, osrs_p x1, mode forcé */

static uint16_t u16le(const uint8_t *b) { return (uint16_t)(b[0] | (b[1] << 8)); }
static int16_t s16le(const uint8_t *b) { return (int16_t)u16le(b); }

bool bme280_init(bme280_dev *dev)
{
    uint8_t id, c0[26], c1[7];

    if (!dev->read(REG_CHIP_ID, &id, 1) || (id != BME280_CHIP_ID && id != BMP280_CHIP_ID))
        return false;
    dev->chipId = id;
    if (!dev->read(REG_CALIB0, c0, sizeof(c0)))
        return false;

    dev->calib.T1 = u16le(&c0[0]);  dev->calib.T2 = s16le(&c0[2]);  dev->calib.T3 = s16le(&c0[4]);
    dev->calib.P1 = u16le(&c0[6]);  dev->calib.P2 = s16le(&c0[8]);  dev->calib.P3 = s16le(&c0[10]);
    dev->calib.P4 = s16le(&c0[12]); dev->calib.P5 = s16le(&c0[14]); dev->calib.P6 = s16le(&c0[16]);
    dev->calib.P7 = s16le(&c0[18]); dev->calib.P8 = s16le(&c0[20]); dev->calib.P9 = s16le(&c0[22]);
    dev->calib.H1 = c0[25];

    if (id == BME280_CHIP_ID)
    {
        if (!dev->read(REG_CALIB1, c1, sizeof(c1)))
            return false;
        dev->calib.H2 = s16le(&c1[0]);
        dev->calib.H3 = c1[2];
        dev->calib.H4 = (int16_t)(((int16_t)(int8_t)c1[3] << 4) | (c1[4] & 0x0F));
        dev->calib.H5 = (int16_t)(((int16_t)(int8_t)c1[5] << 4) | (c1[4] >> 4));
        dev->calib.H6 = (int8_t)c1[6];
    }
    return dev->write(REG_CONFIG, 0x00);       /* filtre coupé */
}

bool bme280_trigger(const bme280_dev *dev)
{
    /* ctrl_hum n'est pris en compte qu'après une écriture de ctrl_meas. */
    if (dev->chipId == BME280_CHIP_ID && !dev->write(REG_CTRL_HUM, CTRL_HUM_X1))
        return false;
    return dev->write(REG_CTRL_MEAS, CTRL_MEAS_FORCED);
}

bool bme280_isReady(const bme280_dev *dev, bool *busError)
{
    uint8_t st;
    *busError = !dev->read(REG_STATUS, &st, 1);
    return !*busError && !(st & STATUS_MEASURING);
}

int32_t bme280_compTemp(const bme280_calib *c, int32_t adcT, int32_t *tFine)
{
    int32_t v1 = ((((adcT >> 3) - ((int32_t)c->T1 << 1))) * (int32_t)c->T2) >> 11;
    int32_t v2 = (((((adcT >> 4) - (int32_t)c->T1) * ((adcT >> 4) - (int32_t)c->T1)) >> 12) *
                  (int32_t)c->T3) >> 14;
    *tFine = v1 + v2;
    return (*tFine * 5 + 128) >> 8;
}

uint32_t bme280_compPress(const bme280_calib *c, int32_t adcP, int32_t tFine)
{
    int64_t v1, v2, p;

    v1 = (int64_t)tFine - 128000;
    v2 = v1 * v1 * (int64_t)c->P6;
    v2 = v2 + ((v1 * (int64_t)c->P5) * 131072);
    v2 = v2 + ((int64_t)c->P4 * 34359738368LL);
    v1 = ((v1 * v1 * (int64_t)c->P3) / 256) + ((v1 * (int64_t)c->P2) * 4096);
    v1 = ((((int64_t)1) * 140737488355328LL + v1) * (int64_t)c->P1) / 8589934592LL;
    if (v1 == 0)
        return 0;                                 /* évite une division par zéro */
    p = 1048576 - adcP;
    p = (((p * 2147483648LL) - v2) * 3125) / v1;
    v1 = ((int64_t)c->P9 * (p / 8192) * (p / 8192)) / 33554432;
    v2 = ((int64_t)c->P8 * p) / 524288;
    p = ((p + v1 + v2) / 256) + ((int64_t)c->P7 * 16);
    return (uint32_t)p;
}

uint32_t bme280_compHum(const bme280_calib *c, int32_t adcH, int32_t tFine)
{
    int32_t v = tFine - 76800;
    v = (((((adcH << 14) - ((int32_t)c->H4 << 20) - ((int32_t)c->H5 * v)) + 16384) >> 15) *
         (((((((v * (int32_t)c->H6) >> 10) * (((v * (int32_t)c->H3) >> 11) + 32768)) >> 10) +
            2097152) * (int32_t)c->H2 + 8192) >> 14));
    v = v - (((((v >> 15) * (v >> 15)) >> 7) * (int32_t)c->H1) >> 4);
    if (v < 0) v = 0;
    if (v > 419430400) v = 419430400;
    return (uint32_t)(v >> 12);
}

bool bme280_read(const bme280_dev *dev, float *tempC, float *presHpa, float *humPct)
{
    uint8_t d[8];
    int32_t adcP, adcT, adcH, tFine;

    if (!dev->read(REG_DATA, d, sizeof(d)))
        return false;
    adcP = (int32_t)(((uint32_t)d[0] << 12) | ((uint32_t)d[1] << 4) | (d[2] >> 4));
    adcT = (int32_t)(((uint32_t)d[3] << 12) | ((uint32_t)d[4] << 4) | (d[5] >> 4));
    adcH = (int32_t)(((uint32_t)d[6] << 8) | d[7]);
    if (adcT == 0x80000)                          /* valeur "pas de mesure" */
        return false;

    *tempC = (float)bme280_compTemp(&dev->calib, adcT, &tFine) / 100.0f;
    *presHpa = (float)bme280_compPress(&dev->calib, adcP, tFine) / 25600.0f;
    *humPct = (dev->chipId == BME280_CHIP_ID)
              ? (float)bme280_compHum(&dev->calib, adcH, tFine) / 1024.0f : -1.0f;
    return true;
}

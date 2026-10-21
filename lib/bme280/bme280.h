/**
 * @file bme280.h
 * @brief Pilote minimal du BME280 / BMP280 (Bosch) en mode forcé, sans attente.
 *
 * Formules de compensation entières de la fiche technique Bosch (BST-BME280-DS002,
 * §4.2.3 et §8.2). L'accès au bus passe par deux fonctions fournies par
 * l'appelant, ce qui garde le pilote indépendant de la HAL (et testable sur PC).
 */
#ifndef BME280_H
#define BME280_H

#include <stdint.h>
#include <stdbool.h>

#define BME280_CHIP_ID   0x60U
#define BMP280_CHIP_ID   0x58U   /* même capteur sans hygrométrie */

typedef bool (*bme280_read_fn)(uint8_t reg, uint8_t *data, uint8_t len);
typedef bool (*bme280_write_fn)(uint8_t reg, uint8_t value);

typedef struct
{
    uint16_t T1; int16_t T2, T3;
    uint16_t P1; int16_t P2, P3, P4, P5, P6, P7, P8, P9;
    uint8_t H1, H3; int16_t H2, H4, H5; int8_t H6;
} bme280_calib;

typedef struct
{
    bme280_read_fn read;
    bme280_write_fn write;
    uint8_t chipId;            /* BME280_CHIP_ID ou BMP280_CHIP_ID */
    bme280_calib calib;
} bme280_dev;

/** @brief Lit l'identifiant et la calibration. @return false si absent ou autre capteur */
bool bme280_init(bme280_dev *dev);
/** @brief Lance une mesure (x1 sur chaque grandeur, environ 10 ms). */
bool bme280_trigger(const bme280_dev *dev);
/** @brief false tant que la mesure n'est pas finie (ou si le bus ne répond pas : *busError). */
bool bme280_isReady(const bme280_dev *dev, bool *busError);
/**
 * @brief Lit et compense la dernière mesure.
 * @param tempC °C, presHpa hPa, humPct %HR (NaN-safe : humPct = -1 sur un BMP280)
 */
bool bme280_read(const bme280_dev *dev, float *tempC, float *presHpa, float *humPct);

/* Compensation (fiche technique), exposée pour les tests.
   temp : 0,01 °C ; press : Pa x 256 ; hum : %HR x 1024. tFine : sortie de la température. */
int32_t bme280_compTemp(const bme280_calib *c, int32_t adcT, int32_t *tFine);
uint32_t bme280_compPress(const bme280_calib *c, int32_t adcP, int32_t tFine);
uint32_t bme280_compHum(const bme280_calib *c, int32_t adcH, int32_t tFine);

#endif

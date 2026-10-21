/**
 * @file sensors.h
 * @brief Tableau des capteurs, timeouts et seuils.
 */
#ifndef SENSORS_H
#define SENSORS_H

#include "types.h"

/** Index des capteurs dans le tableau (ordre des colonnes du fichier). */
enum { SENSOR_TEMP_AIR, SENSOR_HYGR, SENSOR_PRESSURE, SENSOR_LUMIN, SENSOR_COUNT };

/** @return false si aucun capteur d'ambiance (BME680/BME280) ne répond (il sera recherché à chaque cycle). */
bool sensors_init(void);
/** @brief Lance une mesure du capteur d'ambiance (mode forcé) et remet les états à PENDING. */
void sensors_beginCycle(void);
/** @return true quand tous les capteurs actifs ont répondu */
bool sensors_poll(void);
/** @brief NA, seuils, compteurs de timeout et erreurs capteur. */
void sensors_endCycle(void);
/** @brief Capteur d'ambiance reconnu : "BME680", "BME280", "BMP280 ..." ou "absent". */
const char *sensors_ambientName(void);
uint8_t sensors_count(void);
const Sensor *sensors_get(uint8_t index);
LuminLevel sensors_luminLevel(float value);
const char *sensors_luminLevelName(LuminLevel level);

#endif

/**
 * @file acquisition.h
 * @brief Cycle de mesure : horodatage, capteurs, GPS, ligne, sortie.
 */
#ifndef ACQUISITION_H
#define ACQUISITION_H

#include "types.h"

void acquisition_init(void);
/** @brief Démarre les mesures ; la première a lieu tout de suite. */
void acquisition_start(void);
/** @brief Arrête les mesures (entrée en configuration). */
void acquisition_stop(void);
bool acquisition_isRunning(void);
/** @brief Lance une mesure tout de suite (entrée en maintenance). */
void acquisition_measureNow(void);
/** @brief Automate attente / mesure, appelé à chaque tour de boucle. */
void acquisition_update(void);
/** @brief Ligne d'en-tête des fichiers (colonnes du tableau des capteurs). */
void acquisition_header(char *buf, size_t len);

#endif

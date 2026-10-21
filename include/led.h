/**
 * @file led.h
 * @brief Couleur du mode et clignotement des erreurs sur la LED P9813.
 */
#ifndef LED_H
#define LED_H

#include "types.h"

void led_init(void);
void led_setMode(Mode m);
/** @brief Active ou efface une erreur (un bit par erreur). */
void led_setError(SystemStatus s, bool active);
void led_clearErrors(void);
/** @brief Pendant un appui : couleur du mode visé en plus sombre, fixe. */
void led_setPreview(bool active, Mode target);
/** @brief Erreur active la plus prioritaire (STATUS_OK s'il n'y en a pas). */
SystemStatus led_highestError(void);
/** @brief Affiche le mode, ou l'erreur prioritaire à 1 Hz. */
void led_update(void);

#endif

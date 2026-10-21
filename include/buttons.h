/**
 * @file buttons.h
 * @brief Boutons rouge et vert : interruptions, anti-rebond, appui de 5 s.
 */
#ifndef BUTTONS_H
#define BUTTONS_H

#include "types.h"

void buttons_init(void);
/** @brief Bouton rouge maintenu au démarrage (entrée en configuration). */
bool buttons_redHeldAtBoot(void);
void buttons_update(void);
/** @brief Événement produit une seule fois après 5 s d'appui, puis EVT_NONE. */
Event buttons_getEvent(void);
/** @brief Durée de l'appui en cours (ms), 0 si relâché ou si l'appui long est déjà compté.
 *  @param index 0 = rouge, 1 = vert */
uint32_t buttons_heldMs(uint8_t index);

/** @brief État filtré (anti-rebond) d'un bouton : 0 = rouge, 1 = vert. */
bool buttons_isPressed(uint8_t index);

#endif

/**
 * @file mode_manager.h
 * @brief Automate des quatre modes (avenant du 28/09/2026).
 */
#ifndef MODE_MANAGER_H
#define MODE_MANAGER_H

#include "types.h"

/** @param redHeldAtBoot bouton rouge appuyé au démarrage : configuration */
void mode_init(bool redHeldAtBoot);
/** @brief Lit les événements et change de mode. */
void mode_update(void);
/** @brief Mode actif : lisible par tous les modules, à tout moment. */
Mode mode_get(void);
const char *mode_name(Mode m);
/** @brief Relance le délai de 30 min (chaque commande de la console). */
void mode_notifyActivity(void);
/** @brief Table des transitions (fonction pure, testée sur PC). */
Mode mode_next(Mode current, Event e);

#endif

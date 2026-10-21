/**
 * @file config.h
 * @brief Paramètres de la station et sauvegarde dans la dernière page de Flash.
 */
#ifndef CONFIG_H
#define CONFIG_H

#include "types.h"

/** @brief Charge la page de Flash ; valeurs par défaut si magic ou CRC est faux. */
void config_init(void);

/** @brief Paramètres courants, en lecture seule pour tous les modules. */
const Config *config_get(void);

/**
 * @brief Modifie un paramètre puis l'enregistre aussitôt en Flash.
 * @param name  nom en majuscules (LOG_INTERVALL est accepté)
 * @return CFG_OK, CFG_UNKNOWN ou CFG_OUT_OF_RANGE
 */
ConfigResult config_set(const char *name, int32_t value);

/** @brief Le nom désigne-t-il un paramètre ? */
bool config_exists(const char *name);

/** @brief Remet toutes les valeurs par défaut et les enregistre. */
void config_reset(void);

/** @brief Écrit les paramètres en Flash. @return false si l'écriture échoue */
bool config_save(void);

/** @brief Nombre de paramètres et lecture par index (commande SHOW). */
uint8_t config_count(void);
bool config_getByIndex(uint8_t index, const char **name, int32_t *value);

#endif

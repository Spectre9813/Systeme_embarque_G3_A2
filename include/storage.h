/**
 * @file storage.h
 * @brief Fichiers LOG sur la carte SD : écriture, archivage, retrait.
 */
#ifndef STORAGE_H
#define STORAGE_H

#include "types.h"

/** @brief Monte la carte au démarrage. @return false si elle est absente */
bool storage_init(void);
bool storage_mount(void);
/** @brief Démonte la carte avant son retrait (mode maintenance). */
void storage_unmount(void);
/** @brief Ligne d'en-tête écrite en tête de chaque nouveau fichier. */
void storage_setHeader(const char *header);
/**
 * @brief Ajoute une ligne au fichier du jour, archive le fichier s'il est plein.
 * @param dt   date de la mesure, NULL si la RTC n'a pas répondu
 * @param line ligne sans fin de ligne
 * @param name si non NULL, reçoit le nom du fichier écrit
 * @return true si la ligne est sur la carte
 */
bool storage_writeLine(const RTC_DateTime *dt, const char *line, char *name);
bool storage_isMounted(void);

#endif

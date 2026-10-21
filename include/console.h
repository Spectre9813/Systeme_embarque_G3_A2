/**
 * @file console.h
 * @brief Commandes de configuration sur la liaison série.
 */
#ifndef CONSOLE_H
#define CONSOLE_H

#include "types.h"

void console_init(void);
/** @brief Exécute une commande à chaque fin de ligne reçue. */
void console_update(void);
void console_print(const char *text);
/** @brief Exécute une ligne déjà reçue (utilisé par les tests sur PC). */
void console_execute(char *line);

#endif

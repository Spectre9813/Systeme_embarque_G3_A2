/**
 * @file board.h
 * @brief Démarrage de la carte et réglages des bus (couche pilotes).
 *
 * Complète setup.c (dépôt de base, non modifié) : passe l'horloge de 4 à
 * 80 MHz puis recalcule le débit de l'I2C, de l'UART console et du SPI.
 */
#ifndef BOARD_H
#define BOARD_H

#include <stdbool.h>
#include <stdint.h>

/** @brief Global_Init() du dépôt, puis horloge à 80 MHz et bus recalés. */
void board_init(void);

/** @brief Débloque le bus I2C (9 coups d'horloge) et réinitialise I2C1.
 *  Appelée après une erreur, par exemple quand un module est débranché. */
void board_i2cRecover(void);

/** @brief SPI lent (< 400 kHz) pour l'initialisation de la carte SD,
 *  ou rapide (5 MHz) ensuite. */
void board_sdSpiFast(bool fast);

/** @brief Attente active courte (quelques dizaines de µs au plus). */
void board_busyWaitUs(uint32_t us);

/** @brief Fréquence du processeur en Hz (80 MHz, ou 4 MHz si la PLL échoue). */
unsigned long board_cpuHz(void);

#endif

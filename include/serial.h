/**
 * @file serial.h
 * @brief Réception par interruption (console et GPS), envoi de texte.
 */
#ifndef SERIAL_H
#define SERIAL_H

#include "types.h"

/** @brief Démarre la réception de la console (USART2). */
void serial_init(void);

/** @brief Démarre la réception d'un port déjà configuré (GPS : UART4). */
void serial_enable(SerialPort port);

/**
 * @brief Extrait une ligne complète du tampon de réception, sans bloquer.
 * @param port SERIAL_CONSOLE ou SERIAL_GPS
 * @param buf  destination, terminée par '\0', sans \r ni \n
 * @param len  taille de buf
 * @return true si une ligne complète a été extraite
 */
bool serial_readLine(SerialPort port, char *buf, size_t len);

/** @brief Envoie un texte sur la console (bloquant, < 10 ms pour 100 car.). */
void serial_write(const char *text);

/** @brief Octets perdus par débordement des tampons (diagnostic). */
uint32_t serial_overflows(SerialPort port);

#endif

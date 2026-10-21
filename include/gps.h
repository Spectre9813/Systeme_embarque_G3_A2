/**
 * @file gps.h
 * @brief Décodage des trames NMEA du GPS Air530Z.
 */
#ifndef GPS_H
#define GPS_H

#include "types.h"

/** @brief Configure UART4 (driver grove_gps_air530z) et lance la réception. */
void gps_init(void);

/** @brief Décode les trames reçues depuis le dernier appel. */
void gps_update(void);

/**
 * @brief Position reçue depuis sinceTick.
 * @return true si une trame GGA avec position valide est arrivée depuis sinceTick
 */
bool gps_getPosition(GPS_Data *out, uint32_t sinceTick);

/**
 * @brief Dernière position valide reçue, quelle que soit sa date.
 * @param ageMs reçoit son ancienneté en ms
 * @return false si le GPS n'a encore jamais eu de position
 */
bool gps_lastFix(GPS_Data *out, uint32_t *ageMs);

/** @brief Satellites utilisés pour la position (dernière GGA : 0 tant qu'il n'y a pas de position). */
uint8_t gps_satellites(void);

/** @brief Satellites en vue, toutes constellations (trames GSV des 5 dernières secondes). */
uint8_t gps_satellitesInView(void);

/** @brief Décode une trame GSV : constellation (2 lettres) et nombre de satellites en vue. */
bool gps_parseGSV(const char *sentence, char talker[2], uint8_t *inView);

/** @brief Une trame GGA (avec ou sans position) est arrivée depuis sinceTick. */
bool gps_hasAnswered(uint32_t sinceTick);

/** @brief Une trame NMEA quelconque est arrivée depuis sinceTick. */
bool gps_isAlive(uint32_t sinceTick);

/**
 * @brief Décode une trame GGA (fonction pure, testée sur PC).
 * @return true si la trame est une GGA dont la somme de contrôle est juste
 */
bool gps_parseGGA(const char *sentence, GPS_Data *out);

#endif

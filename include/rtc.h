/**
 * @file rtc.h
 * @brief Lecture et réglage de l'horloge DS1307.
 */
#ifndef RTC_H
#define RTC_H

#include "types.h"

/** @brief Vérifie la présence de la RTC et relance son oscillateur s'il est arrêté. */
bool rtc_init(void);

/** @brief Date et heure courantes. @return false si la RTC ne répond pas ou renvoie une valeur impossible */
bool rtc_now(RTC_DateTime *dt);

bool rtc_setTime(uint8_t hours, uint8_t minutes, uint8_t seconds);
bool rtc_setDate(uint8_t month, uint8_t day, uint16_t year);
/** @param dayOfWeek 1 = MON ... 7 = SUN */
bool rtc_setDay(uint8_t dayOfWeek);

/** @brief Contrôle d'une date (mois, jour selon le mois et les années bissextiles). */
bool rtc_isValidDate(uint8_t month, uint8_t day, uint16_t year);

#endif

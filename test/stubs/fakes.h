/**
 * @file fakes.h
 * @brief Matériel simulé pour les tests sur PC : temps, boutons, RTC, GPS,
 *        BME680, capteur de lumière, LED, liaison série, Flash.
 */
#ifndef FAKES_H
#define FAKES_H

#include <stdint.h>
#include <stdbool.h>
#include "cesi_types.h"

extern uint8_t g_fakeFlash[2048];
#define CONFIG_FLASH_ADDR ((uintptr_t)g_fakeFlash)

void fake_reset(void);                   /* tout sauf la Flash (coupure de courant) */
void fake_eraseFlash(void);              /* Flash vierge */
void fake_advance(uint32_t ms);          /* fait avancer HAL_GetTick() */
uint32_t fake_now(void);

/* Boutons : niveau électrique, avec appel de l'interruption au changement. */
void fake_setButtonLevel(int button1, int level);
extern int g_buttonIdle;                 /* 1 : actif à l'état bas, 0 : actif à l'état haut */

/* RTC */
extern bool g_rtcPresent;
extern RTC_DateTime g_rtc;

/* BME680 */
extern bool g_bmePresent;
extern float g_bmeTemp, g_bmeHum, g_bmePres;
extern bool g_bmeStuck;                 /* mesure jamais terminée (remis à zéro par une réinitialisation) */
extern int g_bmeInitCount;

/* BME280 sur le bus I2C simulé (utilisé quand le BME680 est absent) */
extern bool g_bme280Present;
extern uint8_t g_bme280Regs[256];
void fake_bme280Load(void);

/* Capteur de lumière (valeur brute 0..4095, 0xFFFF = erreur ADC) */
extern uint16_t g_lightRaw;
extern bool g_lightConnected;           /* false : entrée PA4 en l'air */

/* LED : dernière couleur envoyée */
extern uint8_t g_ledR, g_ledG, g_ledB;

/* Liaison série */
void fake_serialPush(int port, const char *line);  /* ligne reçue */
const char *fake_serialOut(void);                  /* tout ce qui a été envoyé */
void fake_serialClear(void);

#endif

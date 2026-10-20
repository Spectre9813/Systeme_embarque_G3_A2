/**
 * Test horloge RTC Grove v1.2 DS1307 (projet 3W, groupe 3)
 *
 * Environnement PlatformIO : test_rtc
 * Montage : module RTC sur un port Grove I2C (PB8 SCL, PB9 SDA), pile CR1225 en place.
 *           Le DS1307 doit être alimenté en 5 V : mettre l'interrupteur du shield sur 5V.
 *
 * Déroulé :
 *  1. Scan du bus I2C : liste les adresses qui répondent (0x68 = RTC, 0x76/0x77 = BME680).
 *  2. Lecture du registre 0 : si l'oscillateur est arrêté (bit CH) ou l'année invalide,
 *     l'heure est réglée automatiquement sur la date et l'heure de compilation.
 *     Pour forcer ce réglage, mettre RTC_FORCE_SET à 1, téléverser, puis le remettre à 0.
 *  3. Affichage de la date et de l'heure chaque seconde, avec vérification que
 *     les secondes avancent bien.
 *
 * Pour vérifier la pile : débrancher l'USB 1 minute, rebrancher. L'heure doit avoir continué.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "setup.h"
#include "board_pins.h"
#include "cesi_types.h"
#include "grove_rtc_ds1307.h"

#define RTC_FORCE_SET   0           /* 1 = régler l'heure à chaque démarrage */
#define DS1307_ADDR     (0x68 << 1)

extern I2C_HandleTypeDef GROVE_I2C_HANDLE;

static const char *DAY_NAMES[8] = { "???", "LUN", "MAR", "MER", "JEU", "VEN", "SAM", "DIM" };

static void i2c_scan(void)
{
    int found = 0;
    printf("Scan I2C :");
    for (uint8_t addr = 0x08; addr < 0x78; addr++)
    {
        if (HAL_I2C_IsDeviceReady(&GROVE_I2C_HANDLE, (uint16_t)(addr << 1), 1, 10) == HAL_OK)
        {
            const char *name = "";
            if (addr == 0x68) name = " (RTC DS1307)";
            else if (addr == 0x76 || addr == 0x77) name = " (BME680)";
            printf(" 0x%02X%s", addr, name);
            found++;
        }
    }
    printf(found ? "\r\n" : " aucun peripherique\r\n");
}

/* Jour de la semaine (algorithme de Sakamoto), 1 = lundi ... 7 = dimanche */
static uint8_t day_of_week(int y, int m, int d)
{
    static const int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    if (m < 3) y -= 1;
    int w = (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;   /* 0 = dimanche */
    return (uint8_t)(w == 0 ? 7 : w);
}

/* Date et heure de compilation : __DATE__ = "Oct 20 2026", __TIME__ = "10:42:07" */
static void compile_time(RTC_DateTime *dt)
{
    static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const char *d = __DATE__;
    const char *t = __TIME__;
    int month = 1;

    for (int i = 0; i < 12; i++)
        if (strncmp(&months[i * 3], d, 3) == 0) month = i + 1;

    dt->month     = (uint8_t)month;
    dt->day       = (uint8_t)atoi(&d[4]);
    dt->year      = (uint16_t)atoi(&d[7]);
    dt->hours     = (uint8_t)atoi(&t[0]);
    dt->minutes   = (uint8_t)atoi(&t[3]);
    dt->seconds   = (uint8_t)atoi(&t[6]);
    dt->dayOfWeek = day_of_week(dt->year, dt->month, dt->day);
}

static void print_dt(const RTC_DateTime *dt)
{
    printf("%s %02u/%02u/%04u %02u:%02u:%02u",
           DAY_NAMES[dt->dayOfWeek <= 7 ? dt->dayOfWeek : 0],
           dt->day, dt->month, dt->year, dt->hours, dt->minutes, dt->seconds);
}

int main(void)
{
    Global_Init();

    printf("\r\n=== Test RTC DS1307 (I2C 0x68) ===\r\n");
    i2c_scan();

    while (!GroveRTC_Init())
    {
        printf("RTC absente a 0x68 : verifier le port I2C, le cable et le 5 V du shield. Nouvel essai...\r\n");
        HAL_Delay(2000);
    }
    printf("RTC detectee.\r\n");

    /* Registre 0, bit 7 = CH (Clock Halt) : 1 = oscillateur arrêté */
    uint8_t reg0 = 0;
    bool halted = false;
    if (HAL_I2C_Mem_Read(&GROVE_I2C_HANDLE, DS1307_ADDR, 0x00, I2C_MEMADD_SIZE_8BIT, &reg0, 1, 100) == HAL_OK)
        halted = (reg0 & 0x80) != 0;

    RTC_DateTime dt;
    bool readable = GroveRTC_GetDateTime(&dt);
    bool invalid  = !readable || dt.month < 1 || dt.month > 12 || dt.day < 1 || dt.day > 31 || dt.year < 2025;

    if (readable)
    {
        printf("Heure lue au demarrage : ");
        print_dt(&dt);
        printf("%s\r\n", halted ? "  (oscillateur ARRETE)" : "");
    }

    if (RTC_FORCE_SET || halted || invalid)
    {
        compile_time(&dt);
        printf("Reglage sur l'heure de compilation : ");
        print_dt(&dt);
        printf(GroveRTC_SetDateTime(&dt) ? "  -> OK\r\n" : "  -> ECHEC d'ecriture\r\n");
    }
    else
    {
        printf("Heure conservee (RTC_FORCE_SET=1 pour la regler sur l'heure du PC).\r\n");
    }
    printf("\r\n");

    uint8_t  lastSec   = 0xFF;
    uint32_t lastTick  = HAL_GetTick();
    uint8_t  frozen    = 0;
    uint32_t errors    = 0;

    while (1)
    {
        if (!GroveRTC_GetDateTime(&dt))
        {
            errors++;
            printf("Erreur de lecture I2C (%lu)\r\n", errors);
            HAL_Delay(1000);
            continue;
        }

        if (dt.seconds != lastSec)
        {
            lastSec  = dt.seconds;
            lastTick = HAL_GetTick();
            frozen   = 0;
            print_dt(&dt);
            printf("\r\n");
        }
        else if ((HAL_GetTick() - lastTick) > 2000U && frozen == 0)
        {
            frozen = 1;
            printf("Les secondes n'avancent plus : oscillateur arrete ou quartz defectueux.\r\n");
        }

        HAL_Delay(100);
    }
}

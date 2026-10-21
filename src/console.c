/**
 * @file console.c
 * @brief Commandes de configuration (tableau 7 du livrable 2).
 *
 * Une commande par ligne. Les espaces sont ignorés et les minuscules
 * acceptées. Réponses : "OK", "ERREUR : valeur hors domaine",
 * "ERREUR : commande inconnue". Les commandes ne sont exécutées qu'en mode
 * configuration ; chacune relance le délai de 30 minutes.
 */
#include "console.h"
#include "mode_manager.h"
#include "serial.h"
#include "config.h"
#include "rtc.h"
#include "led.h"
#include "constants.h"
#include "version.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MSG_OK        "OK\r\n"
#define MSG_RANGE     "ERREUR : valeur hors domaine\r\n"
#define MSG_UNKNOWN   "ERREUR : commande inconnue\r\n"
#define MSG_RTC       "ERREUR : horloge RTC inaccessible\r\n"

void console_init(void)
{
}

void console_print(const char *text)
{
    serial_write(text);
}

/** @brief Supprime les espaces et passe en majuscules, sur place. */
static void console_normalize(char *s)
{
    char *w = s;
    for (; *s != '\0'; s++)
        if (!isspace((unsigned char)*s))
            *w++ = (char)toupper((unsigned char)*s);
    *w = '\0';
}

/** @brief Entier signé décimal, sans rien après. */
static bool console_parseInt(const char *s, int32_t *out)
{
    char *end;
    long v;
    if (*s == '\0')
        return false;
    v = strtol(s, &end, 10);
    if (*end != '\0' || v < -2147483647L || v > 2147483647L)
        return false;
    *out = (int32_t)v;
    return true;
}

/** @brief Lit n entiers séparés par sep ("12:30:00", "10,20,2026"). */
static bool console_parseList(const char *arg, char sep, long *v, int n)
{
    int i;
    char *end;
    for (i = 0; i < n; i++)
    {
        if (!isdigit((unsigned char)*arg))
            return false;
        v[i] = strtol(arg, &end, 10);
        if (i < n - 1)
        {
            if (*end != sep)
                return false;
            arg = end + 1;
        }
        else if (*end != '\0')
            return false;
    }
    return true;
}

static const char *console_rtcResult(bool ok)
{
    led_setError(STATUS_ERR_RTC, !ok);
    return ok ? MSG_OK : MSG_RTC;
}

static const char *console_parseClock(const char *arg)
{
    long v[3];
    if (!console_parseList(arg, ':', v, 3) || v[0] > 23 || v[1] > 59 || v[2] > 59)
        return MSG_RANGE;
    return console_rtcResult(rtc_setTime((uint8_t)v[0], (uint8_t)v[1], (uint8_t)v[2]));
}

static const char *console_parseDate(const char *arg)
{
    long v[3];    /* mois, jour, année */
    if (!console_parseList(arg, ',', v, 3) || v[2] > 2099 ||
        !rtc_isValidDate((uint8_t)v[0], (uint8_t)v[1], (uint16_t)v[2]))
        return MSG_RANGE;
    return console_rtcResult(rtc_setDate((uint8_t)v[0], (uint8_t)v[1], (uint16_t)v[2]));
}

static const char *console_parseDay(const char *arg)
{
    static const char *const days[7] = {"MON", "TUE", "WED", "THU", "FRI", "SAT", "SUN"};
    uint8_t i;
    for (i = 0; i < 7U; i++)
        if (strcmp(arg, days[i]) == 0)
            return console_rtcResult(rtc_setDay((uint8_t)(i + 1U)));
    return MSG_RANGE;
}

static void console_show(void)
{
    char msg[48];
    const char *name;
    int32_t value;
    uint8_t i;
    RTC_DateTime dt;

    for (i = 0; config_getByIndex(i, &name, &value); i++)
    {
        snprintf(msg, sizeof(msg), "%s=%ld\r\n", name, (long)value);
        console_print(msg);
    }
    if (rtc_now(&dt))
        snprintf(msg, sizeof(msg), "RTC : %04u-%02u-%02u %02u:%02u:%02u (jour %u)\r\n",
                 dt.year, dt.month, dt.day, dt.hours, dt.minutes, dt.seconds, dt.dayOfWeek);
    else
        snprintf(msg, sizeof(msg), "RTC : inaccessible\r\n");
    console_print(msg);
}

static void console_help(void)
{
    console_print("Commandes : PARAMETRE=valeur (LOG_INTERVAL, FILE_MAX_SIZE, TIMEOUT,\r\n"
                  "  LUMIN, LUMIN_LOW, LUMIN_HIGH, TEMP_AIR, MIN_TEMP_AIR, MAX_TEMP_AIR,\r\n"
                  "  HYGR, HYGR_MINT, HYGR_MAXT, PRESSURE, PRESSURE_MIN, PRESSURE_MAX)\r\n"
                  "  CLOCK=hh:mm:ss  DATE=mm,jj,aaaa  DAY=MON..SUN\r\n"
                  "  RESET  VERSION  SHOW (valeurs actuelles)  HELP\r\n");
}

void console_execute(char *line)
{
    char *eq;
    int32_t value;
    char msg[96];

    console_normalize(line);
    if (line[0] == '\0')
        return;

    if (strcmp(line, "VERSION") == 0)
    {
        snprintf(msg, sizeof(msg), "Version %s (%s) - lot %s\r\n", FW_VERSION, FW_VARIANT, LOT_NUMBER);
        console_print(msg);
        return;
    }
    if (strcmp(line, "RESET") == 0)
    {
        config_reset();
        console_print(MSG_OK);
        return;
    }
    if (strcmp(line, "SHOW") == 0)
    {
        console_show();
        return;
    }
    if (strcmp(line, "HELP") == 0)
    {
        console_help();
        return;
    }

    eq = strchr(line, '=');
    if (eq == NULL)
    {
        console_print(MSG_UNKNOWN);
        return;
    }
    *eq = '\0';

    if (strcmp(line, "CLOCK") == 0) { console_print(console_parseClock(eq + 1)); return; }
    if (strcmp(line, "DATE") == 0)  { console_print(console_parseDate(eq + 1));  return; }
    if (strcmp(line, "DAY") == 0)   { console_print(console_parseDay(eq + 1));   return; }

    if (!console_parseInt(eq + 1, &value))
    {
        /* Nom connu mais valeur illisible : hors domaine ; sinon inconnue. */
        console_print(config_exists(line) ? MSG_RANGE : MSG_UNKNOWN);
        return;
    }
    switch (config_set(line, value))
    {
    case CFG_OK:           console_print(MSG_OK);      break;
    case CFG_OUT_OF_RANGE: console_print(MSG_RANGE);   break;
    default:               console_print(MSG_UNKNOWN); break;
    }
}

void console_update(void)
{
    char line[CONSOLE_LINE_MAX];

    while (serial_readLine(SERIAL_CONSOLE, line, sizeof(line)))
    {
        if (mode_get() != MODE_CONFIG)
        {
            console_print("Console : commandes acceptees en mode configuration seulement\r\n"
                          "(redemarrer la station en maintenant le bouton rouge).\r\n");
            continue;
        }
        mode_notifyActivity();
        console_execute(line);
    }
}

/**
 * @file gps.c
 * @brief Décodage des trames GGA reçues par interruption.
 *
 * Le driver grove_gps_air530z sert à configurer UART4. Sa lecture, bloquante,
 * n'est pas utilisée ; son décodage non plus, car strtok() fusionne les champs
 * vides (",,") et décale les champs quand le GPS n'a pas de position.
 */
#include "gps.h"
#include "serial.h"
#include "constants.h"
#include "grove_gps_air530z.h"
#include "stm32l4xx_hal.h"
#include <string.h>
#include <stdlib.h>

#define GGA_FIELDS 15

static GPS_Data s_fix;            /* dernière position valide */
static uint32_t s_fixTick;        /* date de réception de s_fix */
static bool s_hasFix;
static uint32_t s_ggaTick;        /* dernière trame GGA, valide ou non */
static bool s_hasGga;
static uint8_t s_lastSats;        /* satellites utilisés (dernière GGA) */

/* Satellites en vue, par constellation (trames GSV : GP, GL, GA, GB/BD...). */
#define GSV_SYSTEMS 6
static char s_gsvTalker[GSV_SYSTEMS][2];
static uint8_t s_gsvCount[GSV_SYSTEMS];
static uint32_t s_gsvTick[GSV_SYSTEMS];
static uint32_t s_frameTick;      /* dernière trame NMEA quelconque */
static bool s_hasFrame;

static bool gps_after(uint32_t t, uint32_t since)
{
    return (int32_t)(t - since) >= 0;
}

static int gps_hex(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/** @brief Somme de contrôle : XOR des caractères entre '$' et '*'. */
static bool gps_checksumOk(const char *s)
{
    uint8_t sum = 0;
    const char *p;
    int hi, lo;

    if (s[0] != '$')
        return false;
    for (p = s + 1; *p != '\0' && *p != '*'; p++)
        sum ^= (uint8_t)*p;
    if (*p != '*')
        return false;
    hi = gps_hex(p[1]);
    lo = gps_hex(p[2]);
    if (hi < 0 || lo < 0)
        return false;
    return sum == (uint8_t)((hi << 4) | lo);
}

/** @brief "ddmm.mmmm" + hémisphère -> degrés décimaux. */
static double gps_toDecimal(const char *v, char hemi)
{
    double raw = atof(v);
    int deg = (int)(raw / 100.0);
    double d = deg + (raw - deg * 100.0) / 60.0;
    return (hemi == 'S' || hemi == 'W') ? -d : d;
}

bool gps_parseGSV(const char *sentence, char talker[2], uint8_t *inView)
{
    const char *p;
    int field = 0;

    if (sentence == NULL || strlen(sentence) < 7 || strncmp(sentence + 3, "GSV,", 4) != 0 ||
        !gps_checksumOk(sentence))
        return false;
    talker[0] = sentence[1];
    talker[1] = sentence[2];
    /* $xxGSV,nb_trames,num_trame,satellites_en_vue,... : champ 3 */
    for (p = sentence; *p != '\0' && field < 3; p++)
        if (*p == ',')
            field++;
    if (field < 3)
        return false;
    *inView = (uint8_t)atoi(p);
    return true;
}

/** @brief Mémorise le nombre de satellites en vue d'une constellation. */
static void gps_storeGsv(const char talker[2], uint8_t inView, uint32_t now)
{
    int i, freeSlot = -1;
    for (i = 0; i < GSV_SYSTEMS; i++)
    {
        if (s_gsvTalker[i][0] == talker[0] && s_gsvTalker[i][1] == talker[1])
            break;
        if (freeSlot < 0 && s_gsvTalker[i][0] == '\0')
            freeSlot = i;
    }
    if (i == GSV_SYSTEMS)
    {
        if (freeSlot < 0)
            return;
        i = freeSlot;
        s_gsvTalker[i][0] = talker[0];
        s_gsvTalker[i][1] = talker[1];
    }
    s_gsvCount[i] = inView;
    s_gsvTick[i] = now;
}

bool gps_parseGGA(const char *sentence, GPS_Data *out)
{
    char copy[NMEA_LINE_MAX];
    char *fields[GGA_FIELDS] = {0};
    char *p;
    int n = 0;

    if (sentence == NULL || out == NULL)
        return false;
    if (strncmp(sentence + 3, "GGA,", 4) != 0 || !gps_checksumOk(sentence))
        return false;

    strncpy(copy, sentence, sizeof(copy) - 1U);
    copy[sizeof(copy) - 1U] = '\0';
    p = strchr(copy, '*');
    if (p != NULL)
        *p = '\0';

    /* Découpage qui garde les champs vides. */
    p = copy;
    while (n < GGA_FIELDS)
    {
        fields[n++] = p;
        p = strchr(p, ',');
        if (p == NULL)
            break;
        *p++ = '\0';
    }
    if (n < 8)
        return false;

    memset(out, 0, sizeof(*out));
    strncpy(out->raw_sentence, sentence, sizeof(out->raw_sentence) - 1U);
    out->fix_quality = (uint8_t)atoi(fields[6]);
    out->satellites = (uint8_t)atoi(fields[7]);
    out->valid = out->fix_quality > 0U && fields[2][0] != '\0' && fields[4][0] != '\0';
    if (out->valid)
    {
        out->latitude = gps_toDecimal(fields[2], fields[3][0]);
        out->longitude = gps_toDecimal(fields[4], fields[5][0]);
    }
    return true;
}

void gps_init(void)
{
    s_hasFix = s_hasGga = s_hasFrame = false;
    s_lastSats = 0;
    memset(s_gsvTalker, 0, sizeof(s_gsvTalker));
    GroveGPS_Init();              /* UART4, 9 600 bauds, broches inversées */
    serial_enable(SERIAL_GPS);
}

void gps_update(void)
{
    char line[NMEA_LINE_MAX];
    GPS_Data d;

    while (serial_readLine(SERIAL_GPS, line, sizeof(line)))
    {
        uint32_t now = HAL_GetTick();

        if (!gps_checksumOk(line))
            continue;
        s_frameTick = now;
        s_hasFrame = true;

        {
            char talker[2];
            uint8_t inView;
            if (gps_parseGSV(line, talker, &inView))
                gps_storeGsv(talker, inView, now);
        }

        if (gps_parseGGA(line, &d))
        {
            s_ggaTick = now;
            s_hasGga = true;
            s_lastSats = d.satellites;
            if (d.valid)
            {
                s_fix = d;
                s_fixTick = now;
                s_hasFix = true;
            }
        }
    }
}

bool gps_getPosition(GPS_Data *out, uint32_t sinceTick)
{
    if (!s_hasFix || !gps_after(s_fixTick, sinceTick))
        return false;
    *out = s_fix;
    return true;
}

bool gps_lastFix(GPS_Data *out, uint32_t *ageMs)
{
    if (!s_hasFix)
        return false;
    *out = s_fix;
    *ageMs = HAL_GetTick() - s_fixTick;
    return true;
}

uint8_t gps_satellites(void)
{
    return s_lastSats;
}

uint8_t gps_satellitesInView(void)
{
    uint32_t now = HAL_GetTick();
    unsigned sum = 0;
    int i;
    for (i = 0; i < GSV_SYSTEMS; i++)
        if (s_gsvTalker[i][0] != '\0' && now - s_gsvTick[i] <= GSV_MAX_AGE_MS)
            sum += s_gsvCount[i];
    return (uint8_t)(sum > 255U ? 255U : sum);
}

bool gps_hasAnswered(uint32_t sinceTick)
{
    return s_hasGga && gps_after(s_ggaTick, sinceTick);
}

bool gps_isAlive(uint32_t sinceTick)
{
    return s_hasFrame && gps_after(s_frameTick, sinceTick);
}

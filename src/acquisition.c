/**
 * @file acquisition.c
 * @brief Cycle de mesure non bloquant (figure 4 du livrable 2).
 *
 *  ATTENTE --(intervalle écoulé)--> MESURE --(tout a répondu ou TIMEOUT)--> ATTENTE
 *                                     |  à la sortie : ligne construite puis
 *                                     |  écrite sur la carte (ou envoyée sur la
 *                                     |  liaison série en maintenance)
 */
#include "acquisition.h"
#include "mode_manager.h"
#include "sensors.h"
#include "gps.h"
#include "rtc.h"
#include "storage.h"
#include "serial.h"
#include "config.h"
#include "led.h"
#include "constants.h"
#include "stm32l4xx_hal.h"
#include <stdio.h>
#include <string.h>

typedef enum { ACQ_IDLE, ACQ_WAIT, ACQ_MEASURE } AcqState;

static AcqState s_state = ACQ_IDLE;
static uint32_t s_t0;              /* début du cycle en cours */
static uint32_t s_lastCycle;       /* début du cycle précédent */
static bool s_firstCycle;
static uint32_t s_cycleCount;      /* GPS un cycle sur deux en économique */
static RTC_DateTime s_dt;
static bool s_dtOk;
static bool s_gpsWanted;
static char s_header[LOG_LINE_MAX];

void acquisition_header(char *buf, size_t len)
{
    uint8_t i;
    size_t n = (size_t)snprintf(buf, len, "DATE;HEURE;LATITUDE;LONGITUDE");

    for (i = 0; i < sensors_count() && n < len; i++)
    {
        n += (size_t)snprintf(buf + n, len - n, ";%s", sensors_get(i)->name);
        if (i == SENSOR_LUMIN && n < len)
            n += (size_t)snprintf(buf + n, len - n, ";LUMIN_NIV");
    }
}

void acquisition_init(void)
{
    acquisition_header(s_header, sizeof(s_header));
    storage_setHeader(s_header);
    s_state = ACQ_IDLE;
}

void acquisition_start(void)
{
    if (s_state != ACQ_IDLE)
        return;
    s_state = ACQ_WAIT;
    s_firstCycle = true;           /* première mesure tout de suite */
}

void acquisition_stop(void)
{
    s_state = ACQ_IDLE;
}

void acquisition_measureNow(void)
{
    if (s_state == ACQ_WAIT)
        s_firstCycle = true;
}

bool acquisition_isRunning(void)
{
    return s_state != ACQ_IDLE;
}

static uint32_t acquisition_intervalMs(void)
{
    uint32_t ms;
    if (mode_get() == MODE_MAINTENANCE)
        return LIVE_PERIOD_MS;                       /* affichage en direct */
    ms = (uint32_t)config_get()->logInterval * LOG_UNIT_MS;
    return (mode_get() == MODE_ECO) ? ms * ECO_INTERVAL_FACTOR : ms;
}

static uint32_t acquisition_timeoutMs(void)
{
    uint32_t ms = (uint32_t)config_get()->timeout * TIMEOUT_UNIT_MS;
    /* En direct, une mesure ne doit pas retarder la suivante (toutes les 2 s). */
    if (mode_get() == MODE_MAINTENANCE && ms > LIVE_TIMEOUT_MS)
        ms = LIVE_TIMEOUT_MS;
    return ms;
}

static void acquisition_beginCycle(void)
{
    s_t0 = HAL_GetTick();
    s_lastCycle = s_t0;
    s_firstCycle = false;
    s_cycleCount++;

    s_dtOk = rtc_now(&s_dt);
    led_setError(STATUS_ERR_RTC, !s_dtOk);

    s_gpsWanted = !(mode_get() == MODE_ECO && (s_cycleCount % ECO_GPS_EVERY) != 0U);
    sensors_beginCycle();
    s_state = ACQ_MEASURE;
}

static bool acquisition_isComplete(void)
{
    bool sensorsDone = sensors_poll();
    return sensorsDone && (!s_gpsWanted || gps_hasAnswered(s_t0));
}

/** @brief Ajoute ";valeur", ";NA" ou ";" (champ vide) selon l'état du capteur. */
static size_t acquisition_field(char *buf, size_t len, const Sensor *s)
{
    switch (s->state)
    {
    case SENSOR_OK:      return (size_t)snprintf(buf, len, ";%.*f", s->decimals, (double)s->value);
    case SENSOR_SKIPPED: return (size_t)snprintf(buf, len, ";");
    default:             return (size_t)snprintf(buf, len, ";NA");
    }
}

static void acquisition_formatLine(char *buf, size_t len)
{
    GPS_Data pos;
    size_t n;
    uint8_t i;

    if (s_dtOk)
        n = (size_t)snprintf(buf, len, "%04u-%02u-%02u;%02u:%02u:%02u",
                             s_dt.year, s_dt.month, s_dt.day, s_dt.hours, s_dt.minutes, s_dt.seconds);
    else
        n = (size_t)snprintf(buf, len, "NA;NA");

    if (!s_gpsWanted)
        n += (size_t)snprintf(buf + n, len - n, ";;");          /* sauté exprès */
    else if (gps_getPosition(&pos, s_t0))
        n += (size_t)snprintf(buf + n, len - n, ";%.5f;%.5f", pos.latitude, pos.longitude);
    else
        n += (size_t)snprintf(buf + n, len - n, ";NA;NA");

    for (i = 0; i < sensors_count() && n < len; i++)
    {
        const Sensor *s = sensors_get(i);
        n += acquisition_field(buf + n, len - n, s);
        if (i == SENSOR_LUMIN && n < len)
        {
            if (s->state == SENSOR_OK)
                n += (size_t)snprintf(buf + n, len - n, ";%s",
                                      sensors_luminLevelName(sensors_luminLevel(s->value)));
            else
                n += (size_t)snprintf(buf + n, len - n, s->state == SENSOR_SKIPPED ? ";" : ";NA");
        }
    }
}

/** @brief Une grandeur pour l'affichage en direct : valeur et unité, ou son état. */
static size_t acquisition_liveField(char *buf, size_t len, const char *label, const Sensor *s,
                                    const char *unit)
{
    switch (s->state)
    {
    case SENSOR_OK:
        return (size_t)snprintf(buf, len, " | %s %.*f%s", label, s->decimals, (double)s->value, unit);
    case SENSOR_SKIPPED:
        return (size_t)snprintf(buf, len, " | %s %s", label, *s->enabled ? "ignoree" : "desactive");
    case SENSOR_INCOHERENT:
        return (size_t)snprintf(buf, len, " | %s %.*f%s HORS SEUILS", label, s->decimals, (double)s->value, unit);
    default:
        return (size_t)snprintf(buf, len, " | %s pas de reponse", label);
    }
}

/**
 * @brief Ligne lisible du mode maintenance : chaque grandeur avec son unité.
 * Sans position GPS en cours, la dernière position connue est donnée avec son
 * ancienneté, et le nombre de satellites vus.
 */
static void acquisition_formatLive(char *buf, size_t len)
{
    const Sensor *lum = sensors_get(SENSOR_LUMIN);
    GPS_Data pos;
    uint32_t age;
    size_t n;

    if (s_dtOk)
        n = (size_t)snprintf(buf, len, "%04u-%02u-%02u %02u:%02u:%02u",
                             s_dt.year, s_dt.month, s_dt.day, s_dt.hours, s_dt.minutes, s_dt.seconds);
    else
        n = (size_t)snprintf(buf, len, "heure inconnue (RTC)");

    if (gps_getPosition(&pos, s_t0))
        n += (size_t)snprintf(buf + n, len - n, " | GPS %.5f %.5f (%u sat)",
                              pos.latitude, pos.longitude, pos.satellites);
    else if (!gps_isAlive(s_t0))
        n += (size_t)snprintf(buf + n, len - n, " | GPS muet");
    else if (gps_lastFix(&pos, &age))
        n += (size_t)snprintf(buf + n, len - n, " | GPS recherche (%u sat en vue), derniere %.5f %.5f il y a %lu s",
                              gps_satellitesInView(), pos.latitude, pos.longitude, (unsigned long)(age / 1000U));
    else
        n += (size_t)snprintf(buf + n, len - n, " | GPS recherche (%u sat en vue)", gps_satellitesInView());

    if (n < len) n += acquisition_liveField(buf + n, len - n, "T", sensors_get(SENSOR_TEMP_AIR), " C");
    if (n < len) n += acquisition_liveField(buf + n, len - n, "H", sensors_get(SENSOR_HYGR), " %");
    if (n < len) n += acquisition_liveField(buf + n, len - n, "P", sensors_get(SENSOR_PRESSURE), " hPa");
    if (n < len) n += acquisition_liveField(buf + n, len - n, "L", lum, "");
    if (n < len && lum->state == SENSOR_OK)
        snprintf(buf + n, len - n, " %s", sensors_luminLevelName(sensors_luminLevel(lum->value)));
}

static void acquisition_output(const char *line)
{
    char msg[LOG_LINE_MAX + 40];
    char name[FILE_NAME_MAX + 1];

    if (storage_writeLine(s_dtOk ? &s_dt : NULL, line, name))
        snprintf(msg, sizeof(msg), "[%s] %s <- %s\r\n", mode_name(mode_get()), name, line);
    else
        snprintf(msg, sizeof(msg), "[%s] NON ENREGISTREE : %s\r\n", mode_name(mode_get()), line);
    if (TRACE_ENABLED)
        serial_write(msg);
}

static void acquisition_finish(void)
{
    char line[LOG_LINE_MAX];

    sensors_endCycle();
    if (s_gpsWanted)
        led_setError(STATUS_ERR_GPS, !gps_isAlive(s_t0));   /* GPS muet pendant tout le cycle */

    if (mode_get() == MODE_MAINTENANCE)
    {
        char live[LIVE_LINE_MAX];
        acquisition_formatLive(live, sizeof(live));
        serial_write("[LIVE] ");
        serial_write(live);
        serial_write("\r\n");
    }
    else
    {
        acquisition_formatLine(line, sizeof(line));
        acquisition_output(line);
    }
    s_state = ACQ_WAIT;
}

void acquisition_update(void)
{
    uint32_t now = HAL_GetTick();

    switch (s_state)
    {
    case ACQ_WAIT:
        if (s_firstCycle || now - s_lastCycle >= acquisition_intervalMs())
            acquisition_beginCycle();
        break;

    case ACQ_MEASURE:
        if (acquisition_isComplete() || now - s_t0 >= acquisition_timeoutMs())
            acquisition_finish();
        break;

    default:
        break;
    }
}

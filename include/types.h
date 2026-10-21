/**
 * @file types.h
 * @brief Types propres au projet 3W, partagés par plusieurs modules.
 *
 * Les types du dépôt de base (RTC_DateTime, GPS_Data, Env_Data) restent
 * dans cesi_types.h. Ce fichier ne contient que des déclarations : aucun
 * module n'y expose ses variables.
 */
#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "cesi_types.h"

/** Modes de fonctionnement (exclusifs). */
typedef enum
{
    MODE_STANDARD,
    MODE_CONFIG,
    MODE_MAINTENANCE,
    MODE_ECO
} Mode;

/** Événements qui déclenchent un changement de mode. */
typedef enum
{
    EVT_NONE,
    EVT_RED_LONG,      /* appui de 5 s sur le bouton rouge */
    EVT_GREEN_LONG,    /* appui de 5 s sur le bouton vert */
    EVT_INACTIVITY     /* 30 min sans commande en configuration */
} Event;

/** Erreurs, de la moins prioritaire à la plus prioritaire. */
typedef enum
{
    STATUS_OK,
    STATUS_ERR_SENSOR,            /* 2 mesures de suite en timeout */
    STATUS_ERR_SENSOR_INCOHERENT, /* valeur hors de [MIN ; MAX] */
    STATUS_ERR_GPS,
    STATUS_ERR_RTC,
    STATUS_ERR_SD_FULL,
    STATUS_ERR_SD_ACCESS,
    STATUS_COUNT
} SystemStatus;

/** État d'un capteur pendant et après un cycle de mesure. */
typedef enum
{
    SENSOR_PENDING,     /* pas encore de réponse */
    SENSOR_OK,
    SENSOR_NA,          /* pas de réponse avant TIMEOUT */
    SENSOR_INCOHERENT,  /* valeur hors seuils : notée NA */
    SENSOR_SKIPPED      /* pas mesuré exprès : champ vide */
} SensorState;

/** Règle appliquée à la valeur en fin de cycle. */
typedef enum
{
    RULE_NONE,          /* aucune vérification (luminosité) */
    RULE_RANGE,         /* hors [min ; max] -> incohérent */
    RULE_TEMP_GATE      /* ignoré si la température sort de [min ; max] */
} SensorRule;

typedef enum
{
    LUMIN_LEVEL_LOW,
    LUMIN_LEVEL_MEDIUM,
    LUMIN_LEVEL_HIGH
} LuminLevel;

typedef enum
{
    CFG_OK,
    CFG_UNKNOWN,
    CFG_OUT_OF_RANGE
} ConfigResult;

typedef enum
{
    SERIAL_CONSOLE,
    SERIAL_GPS,
    SERIAL_PORT_COUNT
} SerialPort;

/** Un capteur du tableau de sensors.c. Un module tiers = une ligne de plus. */
typedef struct
{
    const char *name;           /* nom de la colonne dans le fichier LOG */
    const uint8_t *enabled;     /* paramètre d'activation dans Config */
    const int16_t *min, *max;   /* seuils dans Config, NULL si aucun */
    SensorRule rule;
    uint8_t decimals;           /* chiffres après la virgule dans le fichier */
    bool (*init)(void);
    bool (*read)(float *value); /* false si pas (encore) de réponse */
    float value;
    SensorState state;
    uint8_t timeoutCount;       /* mesures consécutives en timeout */
} Sensor;

/** Paramètres de la station, sauvegardés en Flash. */
typedef struct
{
    uint32_t magic;             /* reconnaît une page déjà écrite */
    uint16_t logInterval;       /* minutes */
    uint16_t fileMaxSize;       /* octets */
    uint16_t timeout;           /* secondes */
    uint8_t  lumin;
    int16_t  luminLow, luminHigh;
    uint8_t  tempAir;
    int16_t  minTempAir, maxTempAir;
    uint8_t  hygr;
    int16_t  hygrMinT, hygrMaxT;
    uint8_t  pressure;
    int16_t  pressureMin, pressureMax;
    uint16_t crc;               /* détecte des données abîmées */
} Config;

#endif /* TYPES_H */

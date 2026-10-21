/**
 * @file config.c
 * @brief Paramètres : table nom / champ / domaine / défaut, sauvegarde en Flash.
 *
 * Le STM32L476 n'a pas d'EEPROM : la dernière page de la banque 2 (2 Ko à
 * 0x080FF800) reçoit les paramètres. Le programme tourne dans la banque 1,
 * il n'est donc pas bloqué pendant l'effacement (environ 25 ms).
 */
#include "config.h"
#include "constants.h"
#include "stm32l4xx_hal.h"
#include <string.h>

typedef enum { PT_U8, PT_U16, PT_I16 } ParamType;

typedef struct
{
    const char *name;
    uint16_t offset;      /* position du champ dans Config */
    ParamType type;
    int32_t min, max, def;
} ParamDesc;

#define P(n, f, t, mn, mx, d) { n, (uint16_t)offsetof(Config, f), t, mn, mx, d }

/* Un paramètre de plus = une ligne de plus. */
static const ParamDesc s_params[] = {
    P("LOG_INTERVAL",  logInterval, PT_U16, 1,   1440,  CFG_DEF_LOG_INTERVAL),
    P("FILE_MAX_SIZE", fileMaxSize, PT_U16, 512, 65535, CFG_DEF_FILE_MAX_SIZE),
    P("TIMEOUT",       timeout,     PT_U16, 1,   60,    CFG_DEF_TIMEOUT),
    P("LUMIN",         lumin,       PT_U8,  0,   1,     CFG_DEF_LUMIN),
    P("LUMIN_LOW",     luminLow,    PT_I16, 0,   1023,  CFG_DEF_LUMIN_LOW),
    P("LUMIN_HIGH",    luminHigh,   PT_I16, 0,   1023,  CFG_DEF_LUMIN_HIGH),
    P("TEMP_AIR",      tempAir,     PT_U8,  0,   1,     CFG_DEF_TEMP_AIR),
    P("MIN_TEMP_AIR",  minTempAir,  PT_I16, -40, 85,    CFG_DEF_MIN_TEMP_AIR),
    P("MAX_TEMP_AIR",  maxTempAir,  PT_I16, -40, 85,    CFG_DEF_MAX_TEMP_AIR),
    P("HYGR",          hygr,        PT_U8,  0,   1,     CFG_DEF_HYGR),
    P("HYGR_MINT",     hygrMinT,    PT_I16, -40, 85,    CFG_DEF_HYGR_MINT),
    P("HYGR_MAXT",     hygrMaxT,    PT_I16, -40, 85,    CFG_DEF_HYGR_MAXT),
    P("PRESSURE",      pressure,    PT_U8,  0,   1,     CFG_DEF_PRESSURE),
    P("PRESSURE_MIN",  pressureMin, PT_I16, 300, 1100,  CFG_DEF_PRESSURE_MIN),
    P("PRESSURE_MAX",  pressureMax, PT_I16, 300, 1100,  CFG_DEF_PRESSURE_MAX),
};
#define PARAM_COUNT ((uint8_t)(sizeof(s_params) / sizeof(s_params[0])))

/* Couples (bas, haut) qui doivent rester dans l'ordre. */
static const char *const s_pairs[][2] = {
    {"LUMIN_LOW", "LUMIN_HIGH"},
    {"MIN_TEMP_AIR", "MAX_TEMP_AIR"},
    {"HYGR_MINT", "HYGR_MAXT"},
    {"PRESSURE_MIN", "PRESSURE_MAX"},
};

static Config s_config;

static uint16_t config_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFU;          /* CRC-16/CCITT-FALSE */
    size_t i;
    uint8_t b;

    for (i = 0; i < len; i++)
    {
        crc ^= (uint16_t)data[i] << 8;
        for (b = 0; b < 8U; b++)
            crc = (crc & 0x8000U) ? (uint16_t)((crc << 1) ^ 0x1021U) : (uint16_t)(crc << 1);
    }
    return crc;
}

static uint16_t config_computeCrc(const Config *c)
{
    return config_crc16((const uint8_t *)c, offsetof(Config, crc));
}

static int32_t config_read(const Config *c, const ParamDesc *p)
{
    const uint8_t *f = (const uint8_t *)c + p->offset;
    uint8_t u8; uint16_t u16; int16_t i16;

    switch (p->type)
    {
    case PT_U8:  memcpy(&u8, f, 1);  return u8;
    case PT_U16: memcpy(&u16, f, 2); return u16;
    default:     memcpy(&i16, f, 2); return i16;
    }
}

static void config_write(Config *c, const ParamDesc *p, int32_t v)
{
    uint8_t *f = (uint8_t *)c + p->offset;
    uint8_t u8 = (uint8_t)v; uint16_t u16 = (uint16_t)v; int16_t i16 = (int16_t)v;

    switch (p->type)
    {
    case PT_U8:  memcpy(f, &u8, 1);  break;
    case PT_U16: memcpy(f, &u16, 2); break;
    default:     memcpy(f, &i16, 2); break;
    }
}

static const ParamDesc *config_find(const char *name)
{
    uint8_t i;
    if (strcmp(name, "LOG_INTERVALL") == 0)       /* orthographe du cahier des charges */
        name = "LOG_INTERVAL";
    for (i = 0; i < PARAM_COUNT; i++)
        if (strcmp(s_params[i].name, name) == 0)
            return &s_params[i];
    return NULL;
}

/** @brief Tous les champs dans leur domaine et les couples dans l'ordre. */
static bool config_isCoherent(const Config *c)
{
    uint8_t i;
    for (i = 0; i < PARAM_COUNT; i++)
    {
        int32_t v = config_read(c, &s_params[i]);
        if (v < s_params[i].min || v > s_params[i].max)
            return false;
    }
    for (i = 0; i < sizeof(s_pairs) / sizeof(s_pairs[0]); i++)
        if (config_read(c, config_find(s_pairs[i][0])) > config_read(c, config_find(s_pairs[i][1])))
            return false;
    return true;
}

static void config_loadDefaults(void)
{
    uint8_t i;
    memset(&s_config, 0, sizeof(s_config));
    s_config.magic = CONFIG_MAGIC;
    for (i = 0; i < PARAM_COUNT; i++)
        config_write(&s_config, &s_params[i], s_params[i].def);
}

/* ---- Accès à la Flash : isolés pour pouvoir tester le reste sur PC ---- */

static void config_flashRead(Config *out)
{
    memcpy(out, (const void *)CONFIG_FLASH_ADDR, sizeof(*out));
}

static bool config_flashWrite(const Config *c)
{
    uint64_t buf[(sizeof(Config) + 7U) / 8U];
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t pageError = 0;
    HAL_StatusTypeDef st;
    size_t i;

    memset(buf, 0xFF, sizeof(buf));
    memcpy(buf, c, sizeof(*c));

    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);   /* OPTVERR est souvent à 1 au reset */

    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.Banks = FLASH_BANK_2;
    erase.Page = (CONFIG_FLASH_ADDR - FLASH_BASE - FLASH_BANK_SIZE) / FLASH_PAGE_SIZE;
    erase.NbPages = 1;
    st = HAL_FLASHEx_Erase(&erase, &pageError);

    for (i = 0; st == HAL_OK && i < sizeof(buf) / sizeof(buf[0]); i++)
        st = HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, CONFIG_FLASH_ADDR + 8U * i, buf[i]);

    HAL_FLASH_Lock();
    return st == HAL_OK && memcmp((const void *)CONFIG_FLASH_ADDR, c, sizeof(*c)) == 0;
}

/* ---- Interface publique ------------------------------------------------ */

void config_init(void)
{
    Config stored;
    config_flashRead(&stored);

    if (stored.magic == CONFIG_MAGIC && stored.crc == config_computeCrc(&stored) &&
        config_isCoherent(&stored))
    {
        s_config = stored;
        return;
    }
    config_loadDefaults();      /* page vierge, abîmée ou d'une autre version */
    config_save();
}

const Config *config_get(void)
{
    return &s_config;
}

ConfigResult config_set(const char *name, int32_t value)
{
    const ParamDesc *p = config_find(name);
    Config candidate;

    if (p == NULL)
        return CFG_UNKNOWN;
    if (value < p->min || value > p->max)
        return CFG_OUT_OF_RANGE;

    candidate = s_config;
    config_write(&candidate, p, value);
    if (!config_isCoherent(&candidate))    /* ex. MIN_TEMP_AIR > MAX_TEMP_AIR */
        return CFG_OUT_OF_RANGE;

    s_config = candidate;
    config_save();
    return CFG_OK;
}

bool config_exists(const char *name)
{
    return config_find(name) != NULL;
}

void config_reset(void)
{
    config_loadDefaults();
    config_save();
}

bool config_save(void)
{
    s_config.magic = CONFIG_MAGIC;
    s_config.crc = config_computeCrc(&s_config);
    return config_flashWrite(&s_config);
}

uint8_t config_count(void)
{
    return PARAM_COUNT;
}

bool config_getByIndex(uint8_t index, const char **name, int32_t *value)
{
    if (index >= PARAM_COUNT)
        return false;
    *name = s_params[index].name;
    *value = config_read(&s_config, &s_params[index]);
    return true;
}

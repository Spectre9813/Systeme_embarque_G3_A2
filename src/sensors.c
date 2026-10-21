/**
 * @file sensors.c
 * @brief Tableau des capteurs, mesure non bloquante du capteur d'ambiance, seuils.
 *
 * Capteur d'ambiance reconnu automatiquement sur le port I2C (0x76 ou 0x77) :
 * - BME680 : API Bosch (lib/bme68x), sans le chauffage du capteur de gaz ;
 * - BME280 (ou BMP280, sans hygrométrie) : pilote lib/bme280.
 * Dans les deux cas la mesure, en mode forcé, dure une dizaine de ms : elle est
 * lancée au début du cycle, puis sensors_poll() regarde à chaque tour de
 * boucle si le résultat est prêt, sans jamais attendre.
 */
#include "sensors.h"
#include "config.h"
#include "led.h"
#include "board.h"
#include "serial.h"
#include <stdio.h>
#include "constants.h"
#include "grove_light.h"
#include "board_pins.h"
#include "bme68x.h"
#include "bme280.h"
#include "stm32l4xx_hal.h"

extern I2C_HandleTypeDef GROVE_I2C_HANDLE;

typedef enum { ENV_NONE, ENV_BME680, ENV_BME280 } EnvChip;

static EnvChip s_chip = ENV_NONE;
static bme280_dev s_dev280;
static bool s_hasHumidity;
static struct bme68x_dev s_dev;
static struct bme68x_conf s_bmeConf;
static uint8_t s_bmeAddr;
static bool s_bmeReady;         /* initialisé et calibré */
static bool s_bmeRunning;       /* mesure lancée dans ce cycle */
static bool s_bmeHasData;       /* résultat du cycle reçu */
static uint32_t s_bmeStart, s_bmeDurMs;
static Env_Data s_env;          /* dernière mesure du capteur d'ambiance */
static bool s_bmeWasLost;       /* capteur perdu depuis la dernière détection */

/* ---- Interface I2C demandée par l'API Bosch ---------------------------- */

static BME68X_INTF_RET_TYPE bme_i2cRead(uint8_t reg, uint8_t *data, uint32_t len, void *intf)
{
    uint8_t addr = *(uint8_t *)intf;
    return HAL_I2C_Mem_Read(&GROVE_I2C_HANDLE, (uint16_t)(addr << 1), reg, I2C_MEMADD_SIZE_8BIT,
                            data, (uint16_t)len, I2C_TIMEOUT_MS) == HAL_OK ? BME68X_OK : BME68X_E_COM_FAIL;
}

static BME68X_INTF_RET_TYPE bme_i2cWrite(uint8_t reg, const uint8_t *data, uint32_t len, void *intf)
{
    uint8_t addr = *(uint8_t *)intf;
    return HAL_I2C_Mem_Write(&GROVE_I2C_HANDLE, (uint16_t)(addr << 1), reg, I2C_MEMADD_SIZE_8BIT,
                             (uint8_t *)data, (uint16_t)len, I2C_TIMEOUT_MS) == HAL_OK ? BME68X_OK : BME68X_E_COM_FAIL;
}

static void bme_delayUs(uint32_t period, void *intf)
{
    (void)intf;
    /* Utilisé seulement par bme68x_init (reset logiciel de 10 ms). */
    HAL_Delay((period + 999U) / 1000U);
}

static bool bme680_setup(void);

static bool bme280_i2cRead(uint8_t reg, uint8_t *data, uint8_t len)
{
    return HAL_I2C_Mem_Read(&GROVE_I2C_HANDLE, (uint16_t)(s_bmeAddr << 1), reg, I2C_MEMADD_SIZE_8BIT,
                            data, len, I2C_TIMEOUT_MS) == HAL_OK;
}

static bool bme280_i2cWrite(uint8_t reg, uint8_t value)
{
    return HAL_I2C_Mem_Write(&GROVE_I2C_HANDLE, (uint16_t)(s_bmeAddr << 1), reg, I2C_MEMADD_SIZE_8BIT,
                             &value, 1, I2C_TIMEOUT_MS) == HAL_OK;
}

/** @brief Cherche un BME280 / BMP280 en 0x76 puis 0x77. */
static bool bme280_setup(void)
{
    static const uint8_t addrs[2] = {BME68X_I2C_ADDR_LOW, BME68X_I2C_ADDR_HIGH};
    uint8_t i;

    s_dev280.read = bme280_i2cRead;
    s_dev280.write = bme280_i2cWrite;
    for (i = 0; i < 2U; i++)
    {
        s_bmeAddr = addrs[i];
        if (bme280_init(&s_dev280))
        {
            s_hasHumidity = (s_dev280.chipId == BME280_CHIP_ID);
            s_bmeDurMs = BME280_MEAS_MS;
            return true;
        }
    }
    return false;
}

/** @brief Capteur d'ambiance : BME680 d'abord, puis BME280 / BMP280. */
static bool bme_setup(void)
{
    s_chip = ENV_NONE;
    if (bme680_setup())
        s_chip = ENV_BME680;
    else if (bme280_setup())
        s_chip = ENV_BME280;
    return s_chip != ENV_NONE;
}

/** @brief Cherche le BME680 en 0x76 puis 0x77, lit sa calibration, le configure. */
static bool bme680_setup(void)
{
    static const uint8_t addrs[2] = {BME68X_I2C_ADDR_LOW, BME68X_I2C_ADDR_HIGH};
    struct bme68x_heatr_conf heatr = {0};
    uint8_t i;

    for (i = 0; i < 2U; i++)
    {
        s_bmeAddr = addrs[i];
        s_dev.intf = BME68X_I2C_INTF;
        s_dev.intf_ptr = &s_bmeAddr;
        s_dev.read = bme_i2cRead;
        s_dev.write = bme_i2cWrite;
        s_dev.delay_us = bme_delayUs;
        s_dev.amb_temp = 20;
        if (bme68x_init(&s_dev) == BME68X_OK)
            break;
    }
    if (i == 2U)
        return false;

    s_bmeConf.os_hum = BME68X_OS_2X;
    s_bmeConf.os_temp = BME68X_OS_4X;
    s_bmeConf.os_pres = BME68X_OS_4X;
    s_bmeConf.filter = BME68X_FILTER_OFF;
    s_bmeConf.odr = BME68X_ODR_NONE;
    if (bme68x_set_conf(&s_bmeConf, &s_dev) != BME68X_OK)
        return false;

    heatr.enable = BME68X_DISABLE;     /* pas de mesure de gaz */
    if (bme68x_set_heatr_conf(BME68X_FORCED_MODE, &heatr, &s_dev) != BME68X_OK)
        return false;

    s_bmeDurMs = bme68x_get_meas_dur(BME68X_FORCED_MODE, &s_bmeConf, &s_dev) / 1000U
                 + BME_MEAS_MARGIN_MS;
    s_hasHumidity = true;
    return true;
}

/** @brief Le capteur ne répond plus (débranché ?) : bus débloqué, recherche au cycle suivant. */
static void bme_lost(void)
{
    if (!s_bmeWasLost)
        serial_write("[CAPTEUR] capteur d'ambiance sans reponse : bus I2C reinitialise, nouvelle recherche a chaque mesure\r\n");
    s_bmeWasLost = true;
    s_bmeReady = false;
    s_bmeRunning = false;
    board_i2cRecover();
}

static void bme_poll(void)
{
    struct bme68x_data data;
    uint8_t status = 0, n = 0;

    if (!s_bmeRunning || s_bmeHasData || HAL_GetTick() - s_bmeStart < s_bmeDurMs)
        return;

    if (s_chip == ENV_BME280)
    {
        bool busError;
        float hum;
        if (!bme280_isReady(&s_dev280, &busError))
        {
            if (busError)
                bme_lost();
            return;
        }
        if (!bme280_read(&s_dev280, &s_env.temperature_c, &s_env.pressure_hpa, &hum))
        {
            bme_lost();
            return;
        }
        s_env.humidity_percent = hum;
        s_env.gas_resistance_ohm = 0.0f;
        s_bmeHasData = true;
        return;
    }

    /* Lecture du bit "nouvelle donnée" avant bme68x_get_data, qui attendrait
       10 ms par essai si la mesure n'était pas finie. */
    if (bme68x_get_regs(BME68X_REG_FIELD0, &status, 1, &s_dev) != BME68X_OK)
    {
        bme_lost();
        return;
    }
    if (!(status & BME68X_NEW_DATA_MSK))
        return;

    if (bme68x_get_data(BME68X_FORCED_MODE, &data, &n, &s_dev) != BME68X_OK || n == 0U)
    {
        bme_lost();
        return;
    }
    /* Compensation entière de l'API : °C x100, Pa, %HR x1000. */
    s_env.temperature_c = (float)data.temperature / 100.0f;
    s_env.pressure_hpa = (float)data.pressure / 100.0f;
    s_env.humidity_percent = (float)data.humidity / 1000.0f;
    s_env.gas_resistance_ohm = 0.0f;
    s_bmeHasData = true;
}

/* ---- Fonctions de lecture du tableau ----------------------------------- */

static void sensors_trace(const char *text)
{
#if TRACE_ENABLED
    serial_write(text);
#else
    (void)text;
#endif
}

static bool sensors_initBme(void)
{
    if (s_bmeReady)
        return true;
    s_bmeReady = bme_setup();
    if (!s_bmeReady)
        board_i2cRecover();          /* bus peut-être bloqué : libéré avant le prochain essai */
    else if (s_bmeWasLost)
    {
        char msg[64];
        snprintf(msg, sizeof(msg), "[CAPTEUR] %s retrouve\r\n", sensors_ambientName());
        sensors_trace(msg);
        s_bmeWasLost = false;
    }
    return s_bmeReady;
}

extern ADC_HandleTypeDef hadc1;          /* défini dans grove_light.c */

static bool sensors_initLight(void)
{
    GroveLight_Init();
    /* Calibration de l'ADC, ADC arrêté (reprise de test/materiel/test_lumiere.c). */
    HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED);
    return true;
}

static bool sensors_readTemperature(float *value)
{
    if (!s_bmeHasData) return false;
    *value = s_env.temperature_c;
    return true;
}

static bool sensors_readHumidity(float *value)
{
    if (!s_bmeHasData || !s_hasHumidity) return false;   /* BMP280 : pas d'hygrométrie */
    *value = s_env.humidity_percent;
    return true;
}

static bool sensors_readPressure(float *value)
{
    if (!s_bmeHasData) return false;
    *value = s_env.pressure_hpa;
    return true;
}

/**
 * @brief Le capteur de lumière est-il branché ?
 * Débranchée, l'entrée analogique flotte et l'ADC rend une valeur quelconque.
 * On passe un instant PA4 en entrée numérique : avec tirage vers le bas puis
 * vers le haut, une entrée en l'air suit le tirage ; reliée à la sortie du
 * capteur (amplificateur), elle garde le même niveau dans les deux cas.
 */
static bool sensors_lightConnected(void)
{
    GPIO_InitTypeDef gpio = {0};
    GPIO_PinState low, high;

    gpio.Pin = LIGHT_GPIO_PIN;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(LIGHT_GPIO_PORT, &gpio);
    board_busyWaitUs(LIGHT_PROBE_US);
    low = HAL_GPIO_ReadPin(LIGHT_GPIO_PORT, LIGHT_GPIO_PIN);

    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(LIGHT_GPIO_PORT, &gpio);
    board_busyWaitUs(LIGHT_PROBE_US);
    high = HAL_GPIO_ReadPin(LIGHT_GPIO_PORT, LIGHT_GPIO_PIN);

    /* Tirage retiré AVANT de repasser en analogique : en mode analogique, la
       HAL ne touche plus au tirage, qui fausserait la mesure (valeur au maximum). */
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(LIGHT_GPIO_PORT, &gpio);
    gpio.Mode = GPIO_MODE_ANALOG_ADC_CONTROL;      /* rendue à l'ADC, comme grove_light */
    HAL_GPIO_Init(LIGHT_GPIO_PORT, &gpio);

    return low == high;
}

static bool sensors_readLight(float *value)
{
    uint16_t raw;

    if (!sensors_lightConnected())
        return false;                              /* traité comme un capteur muet */
    raw = GroveLight_ReadRaw();
    if (raw == LIGHT_ADC_ERROR)
        return false;
    *value = (float)(raw / LIGHT_ADC_DIVIDER);   /* 0..4095 -> 0..1023 */
    return true;
}

/*
 * Tableau des capteurs, dans l'ordre des colonnes du fichier.
 * Les pointeurs enabled/min/max sont remplis par sensors_init() avec les
 * adresses des champs de Config : un seuil changé à la console est pris en
 * compte au cycle suivant. Exemple de module tiers (port A3, ADC1 voie 15) :
 *   { "TEMP_EAU", NULL, NULL, NULL, RULE_RANGE, 1, waterInit, waterRead },
 */
static Sensor s_sensors[SENSOR_COUNT] = {
    [SENSOR_TEMP_AIR] = { "TEMP_AIR", NULL, NULL, NULL, RULE_RANGE,     1, sensors_initBme,   sensors_readTemperature, 0, SENSOR_SKIPPED, 0 },
    [SENSOR_HYGR]     = { "HYGR",     NULL, NULL, NULL, RULE_TEMP_GATE, 1, sensors_initBme,   sensors_readHumidity,    0, SENSOR_SKIPPED, 0 },
    [SENSOR_PRESSURE] = { "PRESSURE", NULL, NULL, NULL, RULE_RANGE,     1, sensors_initBme,   sensors_readPressure,    0, SENSOR_SKIPPED, 0 },
    [SENSOR_LUMIN]    = { "LUMIN",    NULL, NULL, NULL, RULE_NONE,      0, sensors_initLight, sensors_readLight,       0, SENSOR_SKIPPED, 0 },
};

static bool sensors_bmeWanted(void)
{
    return *s_sensors[SENSOR_TEMP_AIR].enabled || *s_sensors[SENSOR_HYGR].enabled ||
           *s_sensors[SENSOR_PRESSURE].enabled;
}

bool sensors_init(void)
{
    const Config *c = config_get();
    uint8_t i;
    bool ok = true;

    s_sensors[SENSOR_TEMP_AIR].enabled = &c->tempAir;
    s_sensors[SENSOR_TEMP_AIR].min = &c->minTempAir;
    s_sensors[SENSOR_TEMP_AIR].max = &c->maxTempAir;
    s_sensors[SENSOR_HYGR].enabled = &c->hygr;
    s_sensors[SENSOR_HYGR].min = &c->hygrMinT;
    s_sensors[SENSOR_HYGR].max = &c->hygrMaxT;
    s_sensors[SENSOR_PRESSURE].enabled = &c->pressure;
    s_sensors[SENSOR_PRESSURE].min = &c->pressureMin;
    s_sensors[SENSOR_PRESSURE].max = &c->pressureMax;
    s_sensors[SENSOR_LUMIN].enabled = &c->lumin;

    s_bmeReady = false;
    for (i = 0; i < SENSOR_COUNT; i++)
    {
        s_sensors[i].timeoutCount = 0;
        s_sensors[i].state = SENSOR_SKIPPED;
        if (s_sensors[i].init != NULL && !s_sensors[i].init())
        {
            /* Absent au démarrage = premier timeout : l'erreur allumée par
               main() reste allumée si le capteur ne répond pas au 1er cycle. */
            s_sensors[i].timeoutCount = 1;
            ok = false;
        }
    }
    return ok;
}

void sensors_beginCycle(void)
{
    uint8_t i;

    for (i = 0; i < SENSOR_COUNT; i++)
    {
        s_sensors[i].state = *s_sensors[i].enabled ? SENSOR_PENDING : SENSOR_SKIPPED;
        s_sensors[i].value = 0.0f;
    }

    s_bmeRunning = false;
    s_bmeHasData = false;
    if (sensors_bmeWanted() && sensors_initBme())
    {
        bool started = (s_chip == ENV_BME280) ? bme280_trigger(&s_dev280)
                                              : bme68x_set_op_mode(BME68X_FORCED_MODE, &s_dev) == BME68X_OK;
        if (started)
        {
            s_bmeRunning = true;
            s_bmeStart = HAL_GetTick();
        }
        else
            bme_lost();
    }
}

bool sensors_poll(void)
{
    bool done = true;
    uint8_t i;

    bme_poll();
    for (i = 0; i < SENSOR_COUNT; i++)
    {
        Sensor *s = &s_sensors[i];
        if (s->state != SENSOR_PENDING)
            continue;
        if (s->read(&s->value))
            s->state = SENSOR_OK;
        else
            done = false;
    }
    return done;
}

void sensors_endCycle(void)
{
    const Sensor *temp = &s_sensors[SENSOR_TEMP_AIR];
    bool timeoutErr = false, incoherent = false;
    uint8_t i;

    for (i = 0; i < SENSOR_COUNT; i++)
    {
        Sensor *s = &s_sensors[i];

        if (s->state == SENSOR_PENDING)
        {
            s->state = SENSOR_NA;                     /* pas de réponse avant TIMEOUT */
            if (s->timeoutCount < 255U)
                s->timeoutCount++;
        }
        else if (s->state == SENSOR_OK)
            s->timeoutCount = 0;

        if (s->state == SENSOR_OK && s->rule == RULE_RANGE &&
            (s->value < *s->min || s->value > *s->max))
            s->state = SENSOR_INCOHERENT;
    }

    /* L'hygrométrie n'a de sens que si la température est dans [HYGR_MINT ; HYGR_MAXT]. */
    for (i = 0; i < SENSOR_COUNT; i++)
    {
        Sensor *s = &s_sensors[i];
        if (s->rule == RULE_TEMP_GATE && s->state == SENSOR_OK && temp->state == SENSOR_OK &&
            (temp->value < *s->min || temp->value > *s->max))
            s->state = SENSOR_SKIPPED;
        if (s->timeoutCount >= 2U)
            timeoutErr = true;
        if (s->state == SENSOR_INCOHERENT)
            incoherent = true;
    }

    /* Mesure lancée mais jamais reçue (capteur remis à zéro par une coupure,
       registre d'état bloqué...) : réinitialisation complète au prochain cycle. */
    if (s_bmeRunning && !s_bmeHasData)
        bme_lost();
    s_bmeRunning = false;
    /* Les erreurs restent actives tant que le problème dure. */
    led_setError(STATUS_ERR_SENSOR, timeoutErr);
    led_setError(STATUS_ERR_SENSOR_INCOHERENT, incoherent);
}

const char *sensors_ambientName(void)
{
    if (s_chip == ENV_BME680) return "BME680";
    if (s_chip == ENV_BME280) return s_hasHumidity ? "BME280" : "BMP280 (sans hygrometrie)";
    return "absent";
}

uint8_t sensors_count(void)
{
    return SENSOR_COUNT;
}

const Sensor *sensors_get(uint8_t index)
{
    return (index < SENSOR_COUNT) ? &s_sensors[index] : NULL;
}

LuminLevel sensors_luminLevel(float value)
{
    const Config *c = config_get();
    if (value < c->luminLow)
        return LUMIN_LEVEL_LOW;
    if (value > c->luminHigh)
        return LUMIN_LEVEL_HIGH;
    return LUMIN_LEVEL_MEDIUM;
}

const char *sensors_luminLevelName(LuminLevel level)
{
    static const char *const names[] = {"FAIBLE", "MOYENNE", "FORTE"};
    return names[level];
}

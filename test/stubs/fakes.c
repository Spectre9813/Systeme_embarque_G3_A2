/**
 * @file fakes.c
 * @brief Implémentation du matériel simulé (voir fakes.h).
 */
#include "fakes.h"
#include "stm32l4xx_hal.h"
#include "types.h"
#include "bme68x.h"
#include <string.h>
#include <stdio.h>

/* ---- Temps ------------------------------------------------------------- */
static uint32_t s_tick;
uint32_t HAL_GetTick(void) { return s_tick; }
void HAL_Delay(uint32_t ms) { s_tick += ms; }
void fake_advance(uint32_t ms) { s_tick += ms; }
uint32_t fake_now(void) { return s_tick; }

/* ---- GPIO / boutons ---------------------------------------------------- */
GPIO_TypeDef fakeGPIOA = {0}, fakeGPIOB = {1};
static int s_level[2] = {1, 1};   /* repos à 1 (module actif à l'état bas) */
int g_buttonIdle = 1;             /* niveau de repos remis par fake_reset */
void HAL_GPIO_EXTI_Callback(uint16_t pin);

bool g_lightConnected = true;
static uint32_t s_pa4Pull;
void HAL_GPIO_Init(GPIO_TypeDef *port, GPIO_InitTypeDef *init)
{
    /* Comme la vraie HAL : en mode analogique, le tirage n'est pas modifié. */
    if (port == GPIOA && init->Pin == GPIO_PIN_4 && init->Mode != GPIO_MODE_ANALOG_ADC_CONTROL)
        s_pa4Pull = init->Pull;
}
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState s) { (void)port; (void)pin; (void)s; }
void HAL_NVIC_SetPriority(IRQn_Type irq, uint32_t p, uint32_t s) { (void)irq; (void)p; (void)s; }
void HAL_NVIC_EnableIRQ(IRQn_Type irq) { (void)irq; }
void HAL_GPIO_EXTI_IRQHandler(uint16_t pin) { HAL_GPIO_EXTI_Callback(pin); }

GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin)
{
    /* Bouton 1 : PA10, bouton 2 : PB3 (board_pins.h) */
    if (port == GPIOA && pin == GPIO_PIN_10) return s_level[0] ? GPIO_PIN_SET : GPIO_PIN_RESET;
    if (port == GPIOB && pin == GPIO_PIN_3)  return s_level[1] ? GPIO_PIN_SET : GPIO_PIN_RESET;
    if (port == GPIOA && pin == GPIO_PIN_4)  /* capteur de lumière : sortie pilotée, ou entrée en l'air */
        return g_lightConnected ? (g_lightRaw > 2048 ? GPIO_PIN_SET : GPIO_PIN_RESET)
                                : (s_pa4Pull == GPIO_PULLUP ? GPIO_PIN_SET : GPIO_PIN_RESET);
    return GPIO_PIN_RESET;
}

void fake_setButtonLevel(int button1, int level)
{
    int i = button1 ? 0 : 1;
    if (s_level[i] != level)
    {
        s_level[i] = level;
        HAL_GPIO_EXTI_Callback(i == 0 ? GPIO_PIN_10 : GPIO_PIN_3);
    }
}

/* ---- I2C : seule la RTC (bit CH) passe directement par la HAL ---------- */
I2C_HandleTypeDef hi2c1;
bool g_rtcPresent = true;
RTC_DateTime g_rtc = {0, 30, 14, 2, 20, 10, 2026};

bool g_bme280Present = false;
uint8_t g_bme280Regs[256];

HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *h, uint16_t addr, uint16_t reg, uint16_t sz,
                                   uint8_t *data, uint16_t len, uint32_t timeout)
{
    (void)h; (void)sz; (void)timeout;
    if (addr == (0x76U << 1))                       /* BME280 simulé */
    {
        if (!g_bme280Present) return HAL_ERROR;
        memcpy(data, &g_bme280Regs[reg], len);
        return HAL_OK;
    }
    if (addr != (0x68U << 1) || !g_rtcPresent) return HAL_ERROR;
    memset(data, 0, len);                           /* RTC : bit CH à 0 */
    return HAL_OK;
}

HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *h, uint16_t addr, uint16_t reg, uint16_t sz,
                                    uint8_t *data, uint16_t len, uint32_t timeout)
{
    (void)h; (void)reg; (void)sz; (void)data; (void)len; (void)timeout;
    if (addr == (0x76U << 1)) return g_bme280Present ? HAL_OK : HAL_ERROR;
    return (addr == (0x68U << 1) && g_rtcPresent) ? HAL_OK : HAL_ERROR;
}

/* Registres d'un BME280 : calibration et mesure de l'exemple de la fiche
   technique Bosch (25,08 °C ; 1006,53 hPa) + humidité de 55,0 %HR. */
void fake_bme280Load(void)
{
    static const uint16_t c[12] = {27504, 26435, (uint16_t)-1000, 36477, (uint16_t)-10685, 3024,
                                   2855, 140, (uint16_t)-7, 15500, (uint16_t)-14600, 6000};
    const int16_t H2 = 362, H4 = 313, H5 = 50;
    const int32_t adcP = 415148, adcT = 519888, adcH = 30000;
    int i;
    memset(g_bme280Regs, 0, sizeof(g_bme280Regs));
    g_bme280Regs[0xD0] = 0x60;
    for (i = 0; i < 12; i++) { g_bme280Regs[0x88 + 2 * i] = (uint8_t)c[i]; g_bme280Regs[0x89 + 2 * i] = (uint8_t)(c[i] >> 8); }
    g_bme280Regs[0xA1] = 75;                        /* H1 */
    g_bme280Regs[0xE1] = (uint8_t)H2; g_bme280Regs[0xE2] = (uint8_t)(H2 >> 8);
    g_bme280Regs[0xE3] = 0;                         /* H3 */
    g_bme280Regs[0xE4] = (uint8_t)(H4 >> 4);
    g_bme280Regs[0xE5] = (uint8_t)((H4 & 0x0F) | ((H5 & 0x0F) << 4));
    g_bme280Regs[0xE6] = (uint8_t)(H5 >> 4);
    g_bme280Regs[0xE7] = 30;                        /* H6 */
    g_bme280Regs[0xF7] = (uint8_t)(adcP >> 12); g_bme280Regs[0xF8] = (uint8_t)(adcP >> 4); g_bme280Regs[0xF9] = (uint8_t)(adcP << 4);
    g_bme280Regs[0xFA] = (uint8_t)(adcT >> 12); g_bme280Regs[0xFB] = (uint8_t)(adcT >> 4); g_bme280Regs[0xFC] = (uint8_t)(adcT << 4);
    g_bme280Regs[0xFD] = (uint8_t)(adcH >> 8);  g_bme280Regs[0xFE] = (uint8_t)adcH;
}

/* ---- Drivers grove ----------------------------------------------------- */
bool GroveRTC_Init(void) { return g_rtcPresent; }
bool GroveRTC_GetDateTime(RTC_DateTime *dt)
{
    if (!g_rtcPresent) return false;
    *dt = g_rtc;
    return true;
}
bool GroveRTC_SetDateTime(const RTC_DateTime *dt)
{
    if (!g_rtcPresent) return false;
    g_rtc = *dt;
    return true;
}

uint8_t g_ledR, g_ledG, g_ledB;
void GroveRGB_Init(void) { }
void GroveRGB_SetColor(uint8_t r, uint8_t g, uint8_t b) { g_ledR = r; g_ledG = g; g_ledB = b; }
void GroveRGB_Off(void) { GroveRGB_SetColor(0, 0, 0); }

uint16_t g_lightRaw = 2048;
ADC_HandleTypeDef hadc1;
HAL_StatusTypeDef HAL_ADCEx_Calibration_Start(ADC_HandleTypeDef *h, uint32_t mode) { (void)h; (void)mode; return HAL_OK; }
void GroveLight_Init(void) { }
uint16_t GroveLight_ReadRaw(void)
{
    if (s_pa4Pull == GPIO_PULLUP) return 4095;                 /* tirage resté actif : mesure faussée */
    return g_lightConnected ? g_lightRaw : 1234;               /* en l'air : valeur quelconque */
}

void GroveGPS_Init(void) { }

/* ---- board ------------------------------------------------------------- */
void board_i2cRecover(void) { }
void board_busyWaitUs(uint32_t us) { (void)us; }
void board_sdSpiFast(bool fast) { (void)fast; }
unsigned long board_cpuHz(void) { return 80000000UL; }

/* ---- Liaison série ----------------------------------------------------- */
#define FAKE_LINES 32
static char s_in[2][FAKE_LINES][100];
static int s_inHead[2], s_inTail[2];
static char s_out[16384];
static size_t s_outLen;

void fake_serialPush(int port, const char *line)
{
    strncpy(s_in[port][s_inHead[port] % FAKE_LINES], line, 99);
    s_inHead[port]++;
}
const char *fake_serialOut(void) { return s_out; }
void fake_serialClear(void) { s_outLen = 0; s_out[0] = '\0'; }

void serial_init(void) { }
void serial_enable(SerialPort port) { (void)port; }
bool serial_readLine(SerialPort port, char *buf, size_t len)
{
    if (s_inTail[port] == s_inHead[port]) return false;
    strncpy(buf, s_in[port][s_inTail[port] % FAKE_LINES], len - 1);
    buf[len - 1] = '\0';
    s_inTail[port]++;
    return true;
}
void serial_write(const char *text)
{
    size_t n = strlen(text);
    if (s_outLen + n >= sizeof(s_out)) s_outLen = 0;   /* on garde la fin */
    memcpy(s_out + s_outLen, text, n + 1);
    s_outLen += n;
}
uint32_t serial_overflows(SerialPort port) { (void)port; return 0; }

/* ---- Flash ------------------------------------------------------------- */
uint8_t g_fakeFlash[2048];
HAL_StatusTypeDef HAL_FLASH_Unlock(void) { return HAL_OK; }
HAL_StatusTypeDef HAL_FLASH_Lock(void) { return HAL_OK; }
HAL_StatusTypeDef HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef *e, uint32_t *err)
{
    (void)e; *err = 0xFFFFFFFFU;
    memset(g_fakeFlash, 0xFF, sizeof(g_fakeFlash));
    return HAL_OK;
}
HAL_StatusTypeDef HAL_FLASH_Program(uint32_t type, uintptr_t addr, uint64_t data)
{
    (void)type;
    memcpy((void *)addr, &data, 8);
    return HAL_OK;
}

/* ---- API Bosch BME68x simulée ------------------------------------------ */
bool g_bmePresent = true;
bool g_bmeStuck = false;            /* répond mais ne finit jamais sa mesure */
float g_bmeTemp = 21.5f, g_bmeHum = 45.0f, g_bmePres = 1013.2f;
static uint32_t s_bmeStart;

int g_bmeInitCount;
int8_t bme68x_init(struct bme68x_dev *dev) { (void)dev; g_bmeInitCount++; g_bmeStuck = false; return g_bmePresent ? BME68X_OK : BME68X_E_COM_FAIL; }
int8_t bme68x_set_conf(struct bme68x_conf *c, struct bme68x_dev *d) { (void)c; (void)d; return g_bmePresent ? BME68X_OK : BME68X_E_COM_FAIL; }
int8_t bme68x_set_heatr_conf(uint8_t m, const struct bme68x_heatr_conf *c, struct bme68x_dev *d) { (void)m; (void)c; (void)d; return g_bmePresent ? BME68X_OK : BME68X_E_COM_FAIL; }
uint32_t bme68x_get_meas_dur(const uint8_t m, struct bme68x_conf *c, struct bme68x_dev *d) { (void)m; (void)c; (void)d; return 12000; }
int8_t bme68x_set_op_mode(const uint8_t m, struct bme68x_dev *d)
{
    (void)m; (void)d;
    s_bmeStart = s_tick;
    return g_bmePresent ? BME68X_OK : BME68X_E_COM_FAIL;
}
int8_t bme68x_get_regs(uint8_t reg, uint8_t *data, uint32_t len, struct bme68x_dev *d)
{
    (void)reg; (void)len; (void)d;
    if (!g_bmePresent) return BME68X_E_COM_FAIL;
    *data = (!g_bmeStuck && s_tick - s_bmeStart >= 12U) ? BME68X_NEW_DATA_MSK : 0;
    return BME68X_OK;
}
int8_t bme68x_get_data(uint8_t m, struct bme68x_data *data, uint8_t *n, struct bme68x_dev *d)
{
    (void)m; (void)d;
    if (!g_bmePresent) return BME68X_E_COM_FAIL;
    memset(data, 0, sizeof(*data));
    data->temperature = (int16_t)(g_bmeTemp * 100.0f);
    data->pressure = (uint32_t)(g_bmePres * 100.0f);
    data->humidity = (uint32_t)(g_bmeHum * 1000.0f);
    *n = 1;
    return BME68X_OK;
}

void fake_eraseFlash(void)
{
    memset(g_fakeFlash, 0xFF, sizeof(g_fakeFlash));
}

void fake_reset(void)
{
    s_tick = 1000;
    s_level[0] = s_level[1] = g_buttonIdle;
    g_rtcPresent = true;
    g_rtc = (RTC_DateTime){0, 30, 14, 2, 20, 10, 2026};
    g_bmePresent = true;
    g_bmeStuck = false;
    g_bme280Present = false;
    g_bmeTemp = 21.5f; g_bmeHum = 45.0f; g_bmePres = 1013.2f;
    g_lightRaw = 2048;
    g_lightConnected = true;
    memset(s_inHead, 0, sizeof(s_inHead));
    memset(s_inTail, 0, sizeof(s_inTail));
    fake_serialClear();
}

/**
 * Test capteur BME280 (projet 3W, groupe 3)
 *
 * Environnement PlatformIO : test_bme280
 * Montage : module sur un port Grove I2C (PB8 SCL, PB9 SDA), adresse 0x76 ou 0x77.
 *
 * Le capteur fourni répond avec l'ID 0x60 : c'est un BME280, pas un BME680 (ID 0x61).
 * Il mesure les mêmes grandeurs utiles au projet (température, humidité, pression),
 * sans capteur de gaz, mais ses registres et ses formules sont différents :
 * le driver grove_bme680 du dépôt ne le reconnaît donc pas.
 *
 * Déroulé :
 *  1. détection à 0x76 puis 0x77 (ID 0x60 attendu) ;
 *  2. lecture des coefficients de calibration ;
 *  3. toutes les 2 s : mesure en mode forcé et compensation de la datasheet Bosch
 *     (section 8.1, version en virgule flottante).
 *
 * À vérifier : température proche de celle de la salle, humidité entre 30 et 70 %
 * en intérieur, pression entre 980 et 1040 hPa. Souffler dessus fait monter l'humidité.
 */

#include <stdio.h>
#include <stdbool.h>
#include "setup.h"
#include "board_pins.h"

#define PERIOD_MS        2000U

#define REG_CHIP_ID      0xD0
#define REG_RESET        0xE0
#define REG_CTRL_HUM     0xF2
#define REG_STATUS       0xF3
#define REG_CTRL_MEAS    0xF4
#define REG_CONFIG       0xF5
#define REG_DATA         0xF7

#define BME280_ID        0x60

/* Suréchantillonnage : humidité x1, température x2, pression x16, mode forcé */
#define OSRS_H           0x01
#define CTRL_MEAS_FORCED ((0x02 << 5) | (0x05 << 2) | 0x01)

extern I2C_HandleTypeDef GROVE_I2C_HANDLE;

static uint16_t s_addr;

typedef struct
{
    uint16_t t1; int16_t t2; int16_t t3;
    uint16_t p1; int16_t p2; int16_t p3; int16_t p4; int16_t p5;
    int16_t  p6; int16_t p7; int16_t p8; int16_t p9;
    uint8_t  h1; int16_t h2; uint8_t h3; int16_t h4; int16_t h5; int8_t h6;
} Calib;

static Calib s_cal;

static bool rd(uint8_t reg, uint8_t *buf, uint16_t len)
{
    return HAL_I2C_Mem_Read(&GROVE_I2C_HANDLE, s_addr, reg, I2C_MEMADD_SIZE_8BIT, buf, len, 100) == HAL_OK;
}

static bool wr(uint8_t reg, uint8_t value)
{
    return HAL_I2C_Mem_Write(&GROVE_I2C_HANDLE, s_addr, reg, I2C_MEMADD_SIZE_8BIT, &value, 1, 100) == HAL_OK;
}

static bool find_address(void)
{
    const uint8_t candidates[2] = { 0x76, 0x77 };
    for (int i = 0; i < 2; i++)
    {
        uint8_t id = 0;
        s_addr = (uint16_t)(candidates[i] << 1);
        if (rd(REG_CHIP_ID, &id, 1))
        {
            const char *name = id == 0x60 ? "(BME280)" : id == 0x61 ? "(BME680)" :
                               id == 0x58 ? "(BMP280, sans humidite)" : "(inconnu)";
            printf("Adresse 0x%02X : ID = 0x%02X %s\r\n", candidates[i], id, name);
            if (id == BME280_ID) return true;
        }
        else
        {
            printf("Adresse 0x%02X : pas de reponse\r\n", candidates[i]);
        }
    }
    return false;
}

static int16_t s16(const uint8_t *p) { return (int16_t)((p[1] << 8) | p[0]); }
static uint16_t u16(const uint8_t *p) { return (uint16_t)((p[1] << 8) | p[0]); }

static bool read_calibration(void)
{
    uint8_t a[26];   /* registres 0x88 à 0xA1 */
    uint8_t b[7];    /* registres 0xE1 à 0xE7 */

    if (!rd(0x88, a, sizeof(a)) || !rd(0xE1, b, sizeof(b)))
        return false;

    s_cal.t1 = u16(&a[0]);  s_cal.t2 = s16(&a[2]);  s_cal.t3 = s16(&a[4]);
    s_cal.p1 = u16(&a[6]);  s_cal.p2 = s16(&a[8]);  s_cal.p3 = s16(&a[10]);
    s_cal.p4 = s16(&a[12]); s_cal.p5 = s16(&a[14]); s_cal.p6 = s16(&a[16]);
    s_cal.p7 = s16(&a[18]); s_cal.p8 = s16(&a[20]); s_cal.p9 = s16(&a[22]);
    s_cal.h1 = a[25];

    s_cal.h2 = s16(&b[0]);
    s_cal.h3 = b[2];
    s_cal.h4 = (int16_t)(((int8_t)b[3] * 16) | (b[4] & 0x0F));
    s_cal.h5 = (int16_t)(((int8_t)b[5] * 16) | (b[4] >> 4));
    s_cal.h6 = (int8_t)b[6];

    return s_cal.t1 != 0 && s_cal.t1 != 0xFFFF && s_cal.p1 != 0 && s_cal.p1 != 0xFFFF;
}

static bool configure(void)
{
    if (!wr(REG_RESET, 0xB6)) return false;   /* reset logiciel */
    HAL_Delay(10);
    return wr(REG_CONFIG, 0x00);              /* filtre IIR désactivé */
}

static bool measure(float *t_c, float *h_pct, float *p_hpa)
{
    /* ctrl_hum doit être écrit avant ctrl_meas pour être pris en compte */
    if (!wr(REG_CTRL_HUM, OSRS_H) || !wr(REG_CTRL_MEAS, CTRL_MEAS_FORCED))
        return false;

    /* Mesure d'environ 45 ms, puis attente de la fin (bit 3 "measuring" de status) */
    HAL_Delay(50);
    uint32_t start = HAL_GetTick();
    uint8_t status = 0;
    do
    {
        if (!rd(REG_STATUS, &status, 1)) return false;
        if (HAL_GetTick() - start > 500U) return false;
    } while (status & 0x08);

    uint8_t d[8];   /* 0xF7 à 0xFE : pression, température, humidité */
    if (!rd(REG_DATA, d, sizeof(d))) return false;

    int32_t p_adc = (int32_t)(((uint32_t)d[0] << 12) | ((uint32_t)d[1] << 4) | (d[2] >> 4));
    int32_t t_adc = (int32_t)(((uint32_t)d[3] << 12) | ((uint32_t)d[4] << 4) | (d[5] >> 4));
    int32_t h_adc = (int32_t)(((uint32_t)d[6] << 8) | d[7]);

    if (t_adc == 0x80000) return false;   /* valeur de reset : pas de mesure */

    /* Température */
    double v1 = ((double)t_adc / 16384.0 - (double)s_cal.t1 / 1024.0) * (double)s_cal.t2;
    double v2 = (double)t_adc / 131072.0 - (double)s_cal.t1 / 8192.0;
    v2 = v2 * v2 * (double)s_cal.t3;
    double t_fine = v1 + v2;
    double temp   = t_fine / 5120.0;

    /* Pression (Pa) */
    v1 = t_fine / 2.0 - 64000.0;
    v2 = v1 * v1 * (double)s_cal.p6 / 32768.0;
    v2 = v2 + v1 * (double)s_cal.p5 * 2.0;
    v2 = v2 / 4.0 + (double)s_cal.p4 * 65536.0;
    v1 = ((double)s_cal.p3 * v1 * v1 / 524288.0 + (double)s_cal.p2 * v1) / 524288.0;
    v1 = (1.0 + v1 / 32768.0) * (double)s_cal.p1;
    double press = 0.0;
    if (v1 != 0.0)
    {
        press = 1048576.0 - (double)p_adc;
        press = (press - v2 / 4096.0) * 6250.0 / v1;
        v1 = (double)s_cal.p9 * press * press / 2147483648.0;
        v2 = press * (double)s_cal.p8 / 32768.0;
        press = press + (v1 + v2 + (double)s_cal.p7) / 16.0;
    }

    /* Humidité (%) */
    double h = t_fine - 76800.0;
    h = ((double)h_adc - ((double)s_cal.h4 * 64.0 + (double)s_cal.h5 / 16384.0 * h)) *
        ((double)s_cal.h2 / 65536.0 *
         (1.0 + (double)s_cal.h6 / 67108864.0 * h * (1.0 + (double)s_cal.h3 / 67108864.0 * h)));
    h = h * (1.0 - (double)s_cal.h1 * h / 524288.0);
    if (h > 100.0) h = 100.0;
    if (h < 0.0)   h = 0.0;

    *t_c   = (float)temp;
    *p_hpa = (float)(press / 100.0);
    *h_pct = (float)h;
    return true;
}

int main(void)
{
    Global_Init();

    printf("\r\n=== Test BME280 (I2C 0x76 / 0x77) ===\r\n");

    while (!find_address())
    {
        printf("BME280 non detecte, nouvel essai...\r\n");
        HAL_Delay(2000);
    }

    if (!configure() || !read_calibration())
    {
        printf("Configuration ou lecture de la calibration impossible.\r\n");
        while (1);
    }
    printf("Calibration : T1=%u T2=%d T3=%d P1=%u H1=%u H2=%d\r\n\r\n",
           s_cal.t1, s_cal.t2, s_cal.t3, s_cal.p1, s_cal.h1, s_cal.h2);

    uint32_t n = 0, errors = 0;
    while (1)
    {
        float t, h, p;
        n++;

        if (measure(&t, &h, &p))
        {
            bool ok = t > -40.0f && t < 85.0f && h >= 0.0f && h <= 100.0f && p > 300.0f && p < 1100.0f;
            printf("#%-4lu T = %6.2f C   H = %5.1f %%   P = %7.2f hPa  %s\r\n",
                   n, t, h, p, ok ? "" : "<- hors plage, valeurs suspectes");
        }
        else
        {
            errors++;
            printf("#%-4lu Erreur de mesure (%lu au total)\r\n", n, errors);
        }

        HAL_Delay(PERIOD_MS);
    }
}

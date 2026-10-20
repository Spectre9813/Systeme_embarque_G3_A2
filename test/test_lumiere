/**
 * Test capteur de luminosité Grove v1.3 (projet 3W, groupe 3)
 *
 * Environnement PlatformIO : test_lumiere
 * Montage : capteur sur le port Grove A2 (PA4, ADC1 voie 9).
 *
 * Affiche toutes les 500 ms :
 *  - la valeur brute de l'ADC (0 à 4095) et la tension correspondante ;
 *  - la valeur ramenée sur 0 à 1023 (division par 4, comme prévu au livrable 2) ;
 *  - le niveau faible / moyen / fort avec LUMIN_LOW = 255 et LUMIN_HIGH = 768 ;
 *  - une barre pour voir la variation d'un coup d'oeil.
 *
 * À vérifier : cacher le capteur avec la main fait baisser la valeur,
 * une lampe de téléphone la fait monter. Le min et le max observés sont affichés.
 *
 * Attention : si le shield est réglé en 5 V, la sortie du capteur peut dépasser 3,3 V
 * en pleine lumière (valeur bloquée à 4095). PA4 ne supporte pas 5 V.
 */

#include <stdio.h>
#include "setup.h"
#include "board_pins.h"
#include "grove_light.h"

#define LUMIN_LOW     255
#define LUMIN_HIGH    768
#define PERIOD_MS     500U
#define BAR_WIDTH     30

extern ADC_HandleTypeDef hadc1;   /* défini dans grove_light.c */

static const char *level_name(uint16_t v)
{
    if (v < LUMIN_LOW)  return "FAIBLE ";
    if (v > LUMIN_HIGH) return "FORTE  ";
    return "MOYENNE";
}

int main(void)
{
    Global_Init();
    GroveLight_Init();

    /* Calibration de l'ADC (ADC désactivé à ce stade) : améliore la précision. */
    if (HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED) != HAL_OK)
        printf("Calibration ADC echouee (mesures quand meme possibles)\r\n");

    printf("\r\n=== Test capteur de luminosite (A2 / PA4) ===\r\n");
    printf("Cachez le capteur puis eclairez-le pour verifier la variation.\r\n\r\n");

    uint16_t vmin = 0xFFFF, vmax = 0;
    uint32_t errors = 0;

    while (1)
    {
        uint16_t raw = GroveLight_ReadRaw();

        if (raw == 0xFFFF)
        {
            errors++;
            printf("Erreur de lecture ADC (%lu)\r\n", errors);
            HAL_Delay(PERIOD_MS);
            continue;
        }

        uint16_t v    = raw / 4;                         /* 0..1023 */
        uint32_t mv   = (uint32_t)raw * 3300U / 4095U;
        if (v < vmin) vmin = v;
        if (v > vmax) vmax = v;

        char bar[BAR_WIDTH + 1];
        int n = (int)((uint32_t)v * BAR_WIDTH / 1023U);
        for (int i = 0; i < BAR_WIDTH; i++) bar[i] = (i < n) ? '#' : '.';
        bar[BAR_WIDTH] = '\0';

        printf("brut=%4u  %4lu mV  val=%4u  %s [%s]  min=%u max=%u\r\n",
               raw, mv, v, level_name(v), bar, vmin, vmax);

        if (raw >= 4090)
            printf("  -> saturation : capteur au maximum (ou tension > 3,3 V)\r\n");

        HAL_Delay(PERIOD_MS);
    }
}

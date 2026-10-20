/**
 * Test GPS Grove Air530Z (projet 3W, groupe 3)
 *
 * Environnement PlatformIO : test_gps
 * Montage : GPS sur le port Grove A0 (PA0, PA1 -> UART4, 9600 bauds).
 *
 * Déroulé :
 *  1. Initialisation par le driver du dépôt (GroveGPS_Init, broches PA0/PA1 inversées).
 *  2. Réception des octets par interruption (aucun caractère perdu pendant les printf).
 *     Si rien n'arrive en 3 s, le test inverse automatiquement RX/TX et réessaie :
 *     la console indique quel réglage fonctionne.
 *  3. Chaque trame NMEA complète est affichée avec le résultat de son checksum.
 *     Les trames GGA sont décodées : heure UTC, qualité du fix, satellites, position.
 *  4. Toutes les 5 s : statistiques (octets reçus, trames valides, erreurs).
 *
 * Le décodage gère les champs vides (",,,") que le driver actuel décale à cause de strtok :
 * sans fix, GroveGPS_ReadGGA() lit donc de mauvaises valeurs. À corriger dans le module gps.
 *
 * Premier fix : à l'extérieur ou près d'une fenêtre, de 30 s à quelques minutes.
 * En intérieur, les trames arrivent mais sans position (fix = 0) : c'est normal.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "setup.h"
#include "board_pins.h"
#include "grove_gps_air530z.h"

#define LINE_MAX        96
#define RING_SIZE       256
#define STATS_MS        5000U
#define NO_DATA_MS      3000U
#define MIN_BYTES       50U
#define SHOW_ALL_LINES  1        /* 0 = n'afficher que les trames GGA */

extern UART_HandleTypeDef GPS_UART_HANDLE;   /* défini dans grove_gps_air530z.c */

static volatile uint8_t  s_ring[RING_SIZE];
static volatile uint16_t s_head, s_tail;
static volatile uint32_t s_bytes, s_overruns, s_frameErrors;

/* Interruption UART4 : range chaque octet reçu dans le tampon circulaire */
void UART4_IRQHandler(void)
{
    uint32_t isr = UART4->ISR;

    if (isr & USART_ISR_ORE) { s_overruns++;    UART4->ICR = USART_ICR_ORECF; }
    if (isr & USART_ISR_FE)  { s_frameErrors++; UART4->ICR = USART_ICR_FECF;  }
    if (isr & USART_ISR_NE)  {                  UART4->ICR = USART_ICR_NECF;  }

    if (isr & USART_ISR_RXNE)
    {
        uint8_t c = (uint8_t)UART4->RDR;
        uint16_t next = (uint16_t)((s_head + 1) % RING_SIZE);
        if (next != s_tail)
        {
            s_ring[s_head] = c;
            s_head = next;
        }
        s_bytes++;
    }
}

static bool ring_pop(uint8_t *c)
{
    if (s_tail == s_head) return false;
    *c = s_ring[s_tail];
    s_tail = (uint16_t)((s_tail + 1) % RING_SIZE);
    return true;
}

static void start_rx(bool swap)
{
    HAL_NVIC_DisableIRQ(UART4_IRQn);
    if (GPS_UART_HANDLE.Instance != NULL)   /* déjà initialisé : on repart de zéro */
        HAL_UART_DeInit(&GPS_UART_HANDLE);

    GroveGPS_Init();   /* driver du dépôt : swap activé */
    if (!swap)
    {
        HAL_UART_DeInit(&GPS_UART_HANDLE);
        GPS_UART_HANDLE.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
        HAL_UART_Init(&GPS_UART_HANDLE);   /* swap désactivé (registre remis à 0 par DeInit) */
    }

    s_head = s_tail = 0;
    __HAL_UART_ENABLE_IT(&GPS_UART_HANDLE, UART_IT_RXNE);
    HAL_NVIC_SetPriority(UART4_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(UART4_IRQn);

    printf("UART4 9600 bauds, %s\r\n",
           swap ? "swap ACTIVE (PA0 = RX), reglage du driver" : "swap DESACTIVE (PA1 = RX)");
}

/* Checksum NMEA : XOR des caractères entre '$' et '*' */
static bool nmea_checksum_ok(const char *s)
{
    if (s[0] != '$') return false;
    const char *star = strchr(s, '*');
    if (star == NULL || strlen(star) < 3) return false;

    uint8_t sum = 0;
    for (const char *p = s + 1; p < star; p++) sum ^= (uint8_t)*p;
    return sum == (uint8_t)strtol(star + 1, NULL, 16);
}

/* Découpe en champs en conservant les champs vides (contrairement à strtok) */
static int split_fields(char *s, char *fields[], int max)
{
    int n = 0;
    char *star = strchr(s, '*');
    if (star) *star = '\0';

    fields[n++] = s;
    for (char *p = s; *p && n < max; p++)
    {
        if (*p == ',')
        {
            *p = '\0';
            fields[n++] = p + 1;
        }
    }
    return n;
}

static double nmea_to_deg(const char *v, char hemi)
{
    if (v[0] == '\0') return 0.0;
    double raw = atof(v);
    int deg = (int)(raw / 100.0);
    double d = deg + (raw - deg * 100.0) / 60.0;
    return (hemi == 'S' || hemi == 'W') ? -d : d;
}

static void decode_gga(const char *line)
{
    char copy[LINE_MAX];
    char *f[16];
    strncpy(copy, line, sizeof(copy) - 1);
    copy[sizeof(copy) - 1] = '\0';

    int n = split_fields(copy, f, 16);
    if (n < 8) return;

    /* f[1]=heure f[2]=lat f[3]=N/S f[4]=lon f[5]=E/W f[6]=fix f[7]=satellites */
    const char *t = f[1];
    int fix  = atoi(f[6]);
    int sats = atoi(f[7]);

    if (strlen(t) >= 6)
        printf("  -> GGA : %.2s:%.2s:%.2s UTC, fix=%d, satellites=%d", t, t + 2, t + 4, fix, sats);
    else
        printf("  -> GGA : heure inconnue, fix=%d, satellites=%d", fix, sats);

    if (fix > 0)
        printf(", position %.5f, %.5f\r\n",
               nmea_to_deg(f[2], f[3][0]), nmea_to_deg(f[4], f[5][0]));
    else
        printf(", pas encore de position\r\n");
}

int main(void)
{
    Global_Init();

    printf("\r\n=== Test GPS Air530Z (port A0, UART4) ===\r\n");

    bool swap = true;
    start_rx(swap);

    char     line[LINE_MAX];
    uint16_t len = 0;
    uint32_t valid = 0, badSum = 0, tooLong = 0;
    uint32_t lastStats = HAL_GetTick();
    uint32_t rxStart   = HAL_GetTick();
    uint8_t  attempts  = 1;
    uint32_t bytesAtStart = 0;

    while (1)
    {
        uint8_t c;
        while (ring_pop(&c))
        {
            if (c == '\r') continue;

            if (c == '\n')
            {
                line[len] = '\0';
                if (len > 0)
                {
                    bool ok    = nmea_checksum_ok(line);
                    bool isGGA = strncmp(line + 3, "GGA", 3) == 0;
                    if (ok) valid++; else badSum++;

                    if (SHOW_ALL_LINES || isGGA)
                        printf("%s %s\r\n", ok ? "[OK] " : "[ERR]", line);
                    if (ok && isGGA)
                        decode_gga(line);
                }
                len = 0;
            }
            else if (len < LINE_MAX - 1)
            {
                line[len++] = (char)c;
            }
            else
            {
                tooLong++;
                len = 0;
            }
        }

        uint32_t now = HAL_GetTick();

        /* Pas de flux NMEA (moins de MIN_BYTES octets en 3 s, alors qu'un GPS en
         * envoie environ 400 par seconde) : on essaie l'autre sens RX/TX.
         * Un octet parasite isolé, par exemple au changement de configuration,
         * ne bloque pas la détection. */
        if (valid == 0 && (now - rxStart) >= NO_DATA_MS)
        {
            uint32_t got = s_bytes - bytesAtStart;
            if (got < MIN_BYTES)
            {
                if (attempts < 4)
                {
                    swap = !swap;
                    attempts++;
                    printf("Seulement %lu octet(s) en %u s. Essai avec l'autre sens...\r\n",
                           got, NO_DATA_MS / 1000U);
                    start_rx(swap);
                }
                else if (attempts == 4)
                {
                    attempts++;
                    printf("Aucun flux GPS dans les deux sens : verifier le port A0, le cable Grove "
                           "et l'alimentation du GPS.\r\n");
                }
            }
            rxStart = now;
            bytesAtStart = s_bytes;
        }

        if ((now - lastStats) >= STATS_MS)
        {
            lastStats = now;
            printf("[stats] octets=%lu trames ok=%lu checksum faux=%lu trop longues=%lu overrun=%lu erreurs trame=%lu\r\n",
                   s_bytes, valid, badSum, tooLong, s_overruns, s_frameErrors);
            if (s_frameErrors > 10 && valid == 0)
                printf("  -> beaucoup d'erreurs de trame : mauvais debit ? (9600 attendu)\r\n");
        }
    }
}

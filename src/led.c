/**
 * @file led.c
 * @brief Couleur du mode, ou erreur prioritaire en clignotement à 1 Hz.
 *
 * La LED n'est réécrite que lorsque la couleur change : l'envoi d'une
 * couleur au P9813 se fait bit à bit par le driver grove_rgb.
 */
#include "led.h"
#include "constants.h"
#include "grove_rgb.h"
#include "stm32l4xx_hal.h"

typedef struct { uint8_t r, g, b; } Rgb;

typedef struct
{
    Rgb second;          /* couleur alternée avec le rouge */
    uint16_t redMs;      /* durée du rouge sur une période de 1 s */
} ErrorCode;

static const Rgb RGB_RED    = {255, 0, 0};
static const Rgb RGB_GREEN  = {0, 255, 0};
static const Rgb RGB_YELLOW = {255, 160, 0};
static const Rgb RGB_ORANGE = {255, 50, 0};
static const Rgb RGB_BLUE   = {0, 0, 255};

/* Pendant un appui : couleur du mode visé, en plus sombre (même teinte, sans
   blanc, pour que les trois restent bien distinctes). Indexé par Mode. */
static const Rgb s_previewColors[] = {
    [MODE_STANDARD]    = {0, 60, 0},     /* vert sombre */
    [MODE_CONFIG]      = {60, 40, 0},    /* jamais visé par un bouton */
    [MODE_MAINTENANCE] = {90, 18, 0},    /* orange sombre */
    [MODE_ECO]         = {0, 0, 60},     /* bleu sombre */
};

/* Indexé par SystemStatus (tableau 8 du livrable 2). */
static const ErrorCode s_codes[STATUS_COUNT] = {
    [STATUS_ERR_SENSOR]            = {{0, 255, 0},     500},  /* vert, durées égales */
    [STATUS_ERR_SENSOR_INCOHERENT] = {{0, 255, 0},     333},  /* vert 2x plus long */
    [STATUS_ERR_GPS]               = {{255, 160, 0},   500},  /* jaune */
    [STATUS_ERR_RTC]               = {{0, 0, 255},     500},  /* bleu */
    [STATUS_ERR_SD_FULL]           = {{255, 255, 255}, 500},  /* blanc */
    [STATUS_ERR_SD_ACCESS]         = {{255, 255, 255}, 333},  /* blanc 2x plus long */
};

static Mode s_ledMode = MODE_STANDARD;
static uint8_t s_errors;              /* un bit par SystemStatus */
static uint32_t s_shown = 0xFFFFFFFFUL;
static uint32_t s_lastWrite;
static bool s_preview;               /* appui en cours vers un autre mode */
static Mode s_previewMode;

static uint32_t led_pack(Rgb c)
{
    return ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | c.b;
}

static Rgb led_modeColor(Mode m)
{
    switch (m)
    {
    case MODE_CONFIG:      return RGB_YELLOW;
    case MODE_MAINTENANCE: return RGB_ORANGE;
    case MODE_ECO:         return RGB_BLUE;
    default:               return RGB_GREEN;
    }
}

void led_init(void)
{
    GroveRGB_Init();
    s_errors = 0;
    s_shown = 0xFFFFFFFFUL;
}

void led_setMode(Mode m)
{
    s_ledMode = m;
}

void led_setError(SystemStatus s, bool active)
{
    if (s == STATUS_OK || s >= STATUS_COUNT)
        return;
    if (active)
        s_errors |= (uint8_t)(1U << s);
    else
        s_errors &= (uint8_t)~(1U << s);
}

void led_setPreview(bool active, Mode target)
{
    s_preview = active;
    s_previewMode = target;
}

void led_clearErrors(void)
{
    s_errors = 0;
}

SystemStatus led_highestError(void)
{
    int s;
    for (s = STATUS_COUNT - 1; s > STATUS_OK; s--)
        if (s_errors & (1U << s))
            return (SystemStatus)s;
    return STATUS_OK;
}

void led_update(void)
{
    SystemStatus e = led_highestError();
    Rgb c;
    uint32_t packed;

    if (s_preview)       /* l'appui passe avant les erreurs, le temps de l'appui */
        c = s_previewColors[s_previewMode];
    else if (e == STATUS_OK)
        c = led_modeColor(s_ledMode);
    else
        c = ((HAL_GetTick() % LED_PERIOD_MS) < s_codes[e].redMs) ? RGB_RED : s_codes[e].second;

    packed = led_pack(c);
    /* Réécrite au changement, et au moins une fois par seconde : une trame
       perturbée sur les deux fils ne laisse pas une mauvaise couleur affichée. */
    if (packed != s_shown || HAL_GetTick() - s_lastWrite >= LED_PERIOD_MS)
    {
        GroveRGB_SetColor(c.r, c.g, c.b);
        s_shown = packed;
        s_lastWrite = HAL_GetTick();
    }
}

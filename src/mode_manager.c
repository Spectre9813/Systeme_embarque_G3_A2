/**
 * @file mode_manager.c
 * @brief Seul module qui change de mode (avenant du 28/09/2026) :
 *        chaque bouton ouvre et ferme son propre mode, depuis le standard.
 */
#include "mode_manager.h"
#include "buttons.h"
#include "led.h"
#include "acquisition.h"
#include "storage.h"
#include "serial.h"
#include "constants.h"
#include "stm32l4xx_hal.h"
#include <stdio.h>

static Mode s_mode = MODE_STANDARD;
static uint32_t s_lastActivity;      /* dernière commande de la console */

const char *mode_name(Mode m)
{
    switch (m)
    {
    case MODE_CONFIG:      return "CONFIGURATION";
    case MODE_MAINTENANCE: return "MAINTENANCE";
    case MODE_ECO:         return "ECONOMIQUE";
    default:               return "STANDARD";
    }
}

Mode mode_next(Mode current, Event e)
{
    switch (current)
    {
    case MODE_STANDARD:
        if (e == EVT_RED_LONG)   return MODE_MAINTENANCE;
        if (e == EVT_GREEN_LONG) return MODE_ECO;
        break;
    case MODE_MAINTENANCE:
        if (e == EVT_RED_LONG)   return MODE_STANDARD;
        break;
    case MODE_ECO:
        if (e == EVT_GREEN_LONG) return MODE_STANDARD;
        break;
    case MODE_CONFIG:
        if (e == EVT_INACTIVITY) return MODE_STANDARD;
        break;
    }
    return current;      /* les autres événements sont ignorés */
}

static void mode_exit(Mode m)
{
    if (m == MODE_MAINTENANCE)
    {
        serial_write("[MODE] Carte remontee, ecriture reprise\r\n");
        storage_mount();     /* en cas d'échec, l'erreur s'affiche et l'écriture réessaie */
    }
}

static void mode_enter(Mode m)
{
    char msg[160];

    led_setMode(m);
    snprintf(msg, sizeof(msg), "\r\n[MODE] -> %s\r\n", mode_name(m));
    serial_write(msg);

    switch (m)
    {
    case MODE_CONFIG:
        acquisition_stop();
        /* Plus de mesure : leurs erreurs n'ont plus de sens. */
        led_setError(STATUS_ERR_SENSOR, false);
        led_setError(STATUS_ERR_SENSOR_INCOHERENT, false);
        led_setError(STATUS_ERR_GPS, false);
        s_lastActivity = HAL_GetTick();
        serial_write("Acquisition arretee. Tapez HELP pour la liste des commandes.\r\n");
        break;

    case MODE_MAINTENANCE:
        storage_unmount();   /* efface aussi les erreurs de carte */
        serial_write("La carte SD peut etre retiree. Mesures en direct toutes les 2 s :\r\n");
        acquisition_measureNow();
        break;

    default:                 /* standard ou économique : acquisition en marche */
        acquisition_start();
        break;
    }
}

void mode_init(bool redHeldAtBoot)
{
    s_mode = redHeldAtBoot ? MODE_CONFIG : MODE_STANDARD;
    mode_enter(s_mode);
}

/** @brief Bouton tenu qui mènera à un autre mode : la LED annonce ce mode. */
static void mode_preview(void)
{
    Mode viaRed = mode_next(s_mode, EVT_RED_LONG);
    Mode viaGreen = mode_next(s_mode, EVT_GREEN_LONG);

    if (buttons_heldMs(0) > 0U && viaRed != s_mode)
        led_setPreview(true, viaRed);
    else if (buttons_heldMs(1) > 0U && viaGreen != s_mode)
        led_setPreview(true, viaGreen);
    else
        led_setPreview(false, s_mode);   /* rien d'appuyé, ou appui sans effet ici */
}

void mode_update(void)
{
    Event e = buttons_getEvent();
    Mode next;

    if (e == EVT_NONE && s_mode == MODE_CONFIG && HAL_GetTick() - s_lastActivity >= CONFIG_IDLE_MS)
        e = EVT_INACTIVITY;

    next = mode_next(s_mode, e);
    if (next != s_mode)
    {
        mode_exit(s_mode);
        s_mode = next;
        mode_enter(next);
    }
    mode_preview();
}

Mode mode_get(void)
{
    return s_mode;
}

void mode_notifyActivity(void)
{
    s_lastActivity = HAL_GetTick();
}

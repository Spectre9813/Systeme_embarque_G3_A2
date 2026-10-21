/**
 * @file main.c
 * @brief Station météo embarquée 3W : initialisation puis boucle principale.
 *
 * La boucle ne bloque jamais : chaque module avance d'un pas puis rend la
 * main. Un tour dure quelques millisecondes, ce qui suffit pour mesurer un
 * appui de 5 s et faire clignoter la LED à 1 Hz pendant une mesure.
 */
#include <stdio.h>
#include "board.h"
#include "constants.h"
#include "version.h"
#include "serial.h"
#include "led.h"
#include "buttons.h"
#include "config.h"
#include "rtc.h"
#include "gps.h"
#include "sensors.h"
#include "storage.h"
#include "acquisition.h"
#include "console.h"
#include "mode_manager.h"

static void setup(void)
{
    char msg[120];
    bool redHeld, rtcOk, sensorsOk, sdOk;

    serial_init();
    snprintf(msg, sizeof(msg), "\r\n=== Station 3W - version %s (%s) - lot %s - %lu MHz ===\r\n",
             FW_VERSION, FW_VARIANT, LOT_NUMBER, board_cpuHz() / 1000000UL);
    serial_write(msg);

    led_init();
    buttons_init();
    redHeld = buttons_redHeldAtBoot();

    config_init();

    rtcOk = rtc_init();
    led_setError(STATUS_ERR_RTC, !rtcOk);

    gps_init();

    sensorsOk = sensors_init();
    led_setError(STATUS_ERR_SENSOR, !sensorsOk);

    sdOk = storage_init();              /* l'erreur de carte est gérée par storage */

    acquisition_init();
    console_init();

    snprintf(msg, sizeof(msg), "Init : RTC %s, capteur d'ambiance %s, carte SD %s\r\n",
             rtcOk ? "OK" : "ERREUR", sensors_ambientName(), sdOk ? "OK" : "ERREUR");
    serial_write(msg);

    mode_init(redHeld);                 /* la station démarre même si un composant manque */
}

static void loop(void)
{
    buttons_update();
    mode_update();
    gps_update();
    console_update();
    acquisition_update();
    led_update();                       /* en dernier : affiche les erreurs du tour */
}

int main(void)
{
    board_init();
    setup();
    for (;;)
        loop();
}

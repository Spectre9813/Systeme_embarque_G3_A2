/**
 * @file test_station/test_main.c
 * @brief Tests sur PC du comportement de la station, matériel simulé :
 *        modes, boutons, cycle de mesure, erreurs, LED, console, Flash.
 *
 * Les vrais modules sont compilés tels quels ; seuls la HAL, les drivers
 * grove, la liaison série, l'API Bosch et la carte SD sont simulés.
 * Lancement : pio test -e native   (ou scripts/run_tests.sh)
 */
#include "fakes.h"
#include <unity.h>
#include <string.h>
#include <stdio.h>

#include "../../src/config.c"
#include "../../src/led.c"
#include "../../src/buttons.c"
#include "../../src/rtc.c"
#include "../../src/gps.c"
#include "../../src/sensors.c"
#include "../../src/acquisition.c"
#include "../../src/mode_manager.c"
#include "../../src/console.c"
#include "../../lib/bme280/bme280.c"
#include "../stubs/fakes.c"

/* ---- Carte SD simulée -------------------------------------------------- */
static char g_lastLine[200];
static int g_lines;
static bool g_mounted;
static bool g_lastDtNull;
static int g_mountCalls, g_unmountCalls;

bool storage_init(void) { g_mounted = true; return true; }
bool storage_mount(void) { g_mountCalls++; g_mounted = true; return true; }
void storage_unmount(void) { g_unmountCalls++; g_mounted = false; }
void storage_setHeader(const char *h) { (void)h; }
bool storage_isMounted(void) { return g_mounted; }
bool storage_writeLine(const RTC_DateTime *dt, const char *line, char *name)
{
    if (!g_mounted) return false;
    strncpy(g_lastLine, line, sizeof(g_lastLine) - 1);
    g_lines++;
    g_lastDtNull = (dt == NULL);
    if (name) strcpy(name, "261020_0.LOG");
    return true;
}

/* ---- GPS simulé -------------------------------------------------------- */
typedef enum { GPS_OFF, GPS_NOFIX, GPS_FIX } GpsSim;
static GpsSim g_gps;

static void nmea(char *out, const char *body)
{
    uint8_t cs = 0;
    const char *p;
    for (p = body; *p; p++) cs ^= (uint8_t)*p;
    sprintf(out, "$%s*%02X", body, cs);
}

static void gps_tick(void)
{
    char s[100];
    if (g_gps == GPS_OFF) return;
    if (g_gps == GPS_FIX)
        nmea(s, "GNGGA,143000.00,4907.1580,N,00610.5420,E,1,08,1.0,200.0,M,47.0,M,,");
    else
    {
        nmea(s, "GPGSV,2,1,05,01,40,083,,02,17,308,,03,07,344,,04,22,228,");
        fake_serialPush(SERIAL_GPS, s);
        nmea(s, "BDGSV,1,1,02,05,40,083,,06,17,308,");
        fake_serialPush(SERIAL_GPS, s);
        nmea(s, "GNGGA,143000.00,,,,,0,00,99.99,,,,,,");
    }
    fake_serialPush(SERIAL_GPS, s);
}

/* ---- Boucle principale simulée ----------------------------------------- */
static void run(uint32_t ms)
{
    while (ms--)
    {
        fake_advance(1);
        if (fake_now() % 1000U == 0U) gps_tick();
        buttons_update();
        mode_update();
        gps_update();
        console_update();
        acquisition_update();
        led_update();
    }
}

static void boot(bool redHeld)
{
    fake_reset();
    g_lines = 0; g_lastLine[0] = '\0'; g_mountCalls = g_unmountCalls = 0;
    g_gps = GPS_FIX;
    if (redHeld) fake_setButtonLevel(1, !g_buttonIdle);
    config_init();
    led_init();
    buttons_init();
    led_setError(STATUS_ERR_RTC, !rtc_init());
    gps_init();
    led_setError(STATUS_ERR_SENSOR, !sensors_init());
    storage_init();
    acquisition_init();
    mode_init(buttons_redHeldAtBoot());
}

static void hold(int redButton, uint32_t ms)
{
    fake_setButtonLevel(redButton ? 1 : 0, !g_buttonIdle);
    run(ms);
    fake_setButtonLevel(redButton ? 1 : 0, g_buttonIdle);
    run(100);
}

#define MIN_MS 60000UL

void setUp(void) { g_buttonIdle = 1; fake_eraseFlash(); boot(false); }
void tearDown(void) { }

/* ---- Mode standard ----------------------------------------------------- */

static void test_premiere_mesure_complete(void)
{
    run(1500);
    TEST_ASSERT_EQUAL(MODE_STANDARD, mode_get());
    TEST_ASSERT_EQUAL(1, g_lines);
    TEST_ASSERT_EQUAL_STRING("2026-10-20;14:30:00;49.11930;6.17570;21.5;45.0;1013.2;512;MOYENNE", g_lastLine);
    TEST_ASSERT_EQUAL(STATUS_OK, led_highestError());
    TEST_ASSERT_EQUAL_UINT8(255, g_ledG);           /* vert : standard */
    TEST_ASSERT_EQUAL_UINT8(0, g_ledR);
}

static void test_intervalle_10_minutes(void)
{
    run(1500);
    run(10 * MIN_MS - 2000);
    TEST_ASSERT_EQUAL(1, g_lines);                  /* pas avant 10 min */
    run(3000);
    TEST_ASSERT_EQUAL(2, g_lines);
}

/* ---- Modes et boutons (avenant) ---------------------------------------- */

static void test_boutons_actifs_etat_haut(void)
{
    g_buttonIdle = 0;                               /* module actif à l'état haut */
    boot(false);
    TEST_ASSERT_EQUAL(MODE_STANDARD, mode_get());   /* repos non pris pour un appui */
    hold(0, 5100);
    TEST_ASSERT_EQUAL(MODE_ECO, mode_get());
    hold(0, 5100);
    TEST_ASSERT_EQUAL(MODE_STANDARD, mode_get());   /* on sort bien de l'économique */
    hold(1, 5100);
    TEST_ASSERT_EQUAL(MODE_MAINTENANCE, mode_get());
    hold(1, 5100);
    TEST_ASSERT_EQUAL(MODE_STANDARD, mode_get());
    boot(true);
    TEST_ASSERT_EQUAL(MODE_CONFIG, mode_get());     /* rouge tenu au démarrage */
}

static void test_couleur_pendant_appui(void)
{
    fake_setButtonLevel(0, !g_buttonIdle);          /* vert tenu en standard */
    run(200);
    TEST_ASSERT_EQUAL_UINT8(0, g_ledR);             /* bleu sombre : économique visé */
    TEST_ASSERT_EQUAL_UINT8(0, g_ledG);
    TEST_ASSERT_EQUAL_UINT8(60, g_ledB);
    run(4900);
    TEST_ASSERT_EQUAL(MODE_ECO, mode_get());
    run(300);
    TEST_ASSERT_EQUAL_UINT8(0, g_ledR);             /* bleu franc, bouton encore tenu */
    TEST_ASSERT_EQUAL_UINT8(255, g_ledB);
    fake_setButtonLevel(0, g_buttonIdle);
    fake_setButtonLevel(1, !g_buttonIdle);          /* rouge en économique : sans effet */
    run(1000);
    TEST_ASSERT_EQUAL_UINT8(255, g_ledB);
    TEST_ASSERT_EQUAL_UINT8(0, g_ledR);
    fake_setButtonLevel(1, g_buttonIdle);
}

static void test_maintenance_gps_sans_position(void)
{
    run(1500);                                      /* position connue à 14:30 */
    g_gps = GPS_NOFIX;
    hold(1, 5100);
    fake_serialClear();
    run(2500);
    TEST_ASSERT_NOT_NULL(strstr(fake_serialOut(), "| GPS recherche (7 sat en vue), derniere 49.11930 6.17570 il y a"));
    g_gps = GPS_OFF;
    g_bmePresent = false;
    fake_serialClear();
    run(4000);
    TEST_ASSERT_NOT_NULL(strstr(fake_serialOut(), "| GPS muet | T pas de reponse | H pas de reponse | P pas de reponse | L 512 MOYENNE"));
}

static void test_capteur_lumiere_debranche(void)
{
    run(1500);
    TEST_ASSERT_NOT_NULL(strstr(g_lastLine, ";512;MOYENNE"));
    g_lightConnected = false;                       /* entrée en l'air */
    run(10 * MIN_MS + 31000);                       /* attend TIMEOUT comme un capteur muet */
    TEST_ASSERT_NOT_NULL(strstr(g_lastLine, ";1013.2;NA;NA"));
    run(10 * MIN_MS);
    TEST_ASSERT_EQUAL(STATUS_ERR_SENSOR, led_highestError());   /* 2e timeout */
    g_lightConnected = true;
    run(10 * MIN_MS);
    TEST_ASSERT_NOT_NULL(strstr(g_lastLine, ";512;MOYENNE"));
    TEST_ASSERT_EQUAL(STATUS_OK, led_highestError());
}

static void test_lumiere_pas_faussee_par_le_test(void)
{
    g_lightRaw = 100;                               /* noir : 100 / 4 = 25 */
    run(1500);
    TEST_ASSERT_NOT_NULL(strstr(g_lastLine, ";25;FAIBLE"));
    run(10 * MIN_MS);                               /* et ça reste juste ensuite */
    TEST_ASSERT_NOT_NULL(strstr(g_lastLine, ";25;FAIBLE"));
}

static void test_capteur_bloque_se_reinitialise(void)
{
    run(1500);
    g_bmeStuck = true;                              /* mesure jamais terminée */
    run(10 * MIN_MS + 31000);
    TEST_ASSERT_NOT_NULL(strstr(g_lastLine, ";NA;NA;NA;"));
    run(10 * MIN_MS);                               /* réinitialisé au cycle suivant */
    TEST_ASSERT_NOT_NULL(strstr(g_lastLine, ";21.5;45.0;1013.2;"));
    TEST_ASSERT_EQUAL(STATUS_OK, led_highestError());
    TEST_ASSERT_NOT_NULL(strstr(fake_serialOut(), "retrouve"));
}

static void test_gsv_satellites_en_vue(void)
{
    char s[100], t[2];
    uint8_t n = 0;
    nmea(s, "GPGSV,3,1,11,01,40,083,46,02,17,308,41,12,07,344,39,14,22,228,45");
    TEST_ASSERT_TRUE(gps_parseGSV(s, t, &n));
    TEST_ASSERT_EQUAL_UINT8(11, n);
    TEST_ASSERT_EQUAL_CHAR('G', t[0]);
    TEST_ASSERT_EQUAL_CHAR('P', t[1]);
}

static void test_appui_court_ignore(void)
{
    hold(0, 4000);
    TEST_ASSERT_EQUAL(MODE_STANDARD, mode_get());
}

static void test_vert_5s_economique_puis_retour(void)
{
    run(1500);
    hold(0, 5100);
    TEST_ASSERT_EQUAL(MODE_ECO, mode_get());
    TEST_ASSERT_EQUAL_UINT8(255, g_ledB);           /* bleu */
    TEST_ASSERT_EQUAL_UINT8(0, g_ledG);
    /* intervalle doublé : rien avant 20 min après la 1re mesure */
    run(19 * MIN_MS - 7000);
    TEST_ASSERT_EQUAL(1, g_lines);
    run(MIN_MS + 2000);
    TEST_ASSERT_EQUAL(2, g_lines);
    hold(0, 5100);
    TEST_ASSERT_EQUAL(MODE_STANDARD, mode_get());
}

static void test_economique_gps_un_cycle_sur_deux(void)
{
    char l1[200], l2[200];
    run(1500);
    hold(0, 5100);
    run(20 * MIN_MS);
    strcpy(l1, g_lastLine);
    run(20 * MIN_MS);
    strcpy(l2, g_lastLine);
    /* une des deux lignes a la position, l'autre deux champs vides */
    TEST_ASSERT_TRUE(strstr(l1, "49.11930") != NULL || strstr(l2, "49.11930") != NULL);
    TEST_ASSERT_TRUE(strstr(l1, ":00;;;") != NULL || strstr(l2, ":00;;;") != NULL);
}

static void test_vert_depuis_maintenance_ignore(void)
{
    hold(1, 5100);
    TEST_ASSERT_EQUAL(MODE_MAINTENANCE, mode_get());
    hold(0, 5100);                                  /* avenant : sans effet */
    TEST_ASSERT_EQUAL(MODE_MAINTENANCE, mode_get());
}

static void test_rouge_depuis_economique_ignore(void)
{
    hold(0, 5100);
    TEST_ASSERT_EQUAL(MODE_ECO, mode_get());
    hold(1, 5100);
    TEST_ASSERT_EQUAL(MODE_ECO, mode_get());
}

static void test_maintenance_direct_sans_carte(void)
{
    run(1500);
    hold(1, 5100);
    TEST_ASSERT_EQUAL(MODE_MAINTENANCE, mode_get());
    TEST_ASSERT_EQUAL(1, g_unmountCalls);
    TEST_ASSERT_EQUAL_UINT8(255, g_ledR);           /* orange */
    TEST_ASSERT_EQUAL_UINT8(50, g_ledG);
    fake_serialClear();
    run(1500);                                      /* 1re mesure dès l'entrée */
    TEST_ASSERT_NOT_NULL(strstr(fake_serialOut(),
        "[LIVE] 2026-10-20 14:30:00 | GPS 49.11930 6.17570 (8 sat) | T 21.5 C | H 45.0 % | P 1013.2 hPa | L 512 MOYENNE"));
    {
        int n = 0;
        const char *p = fake_serialOut();
        fake_serialClear();
        run(10000);
        for (p = fake_serialOut(); (p = strstr(p, "[LIVE]")) != NULL; p++) n++;
        TEST_ASSERT_INT_WITHIN(1, 5, n);            /* une mesure toutes les 2 s */
    }
    TEST_ASSERT_EQUAL(1, g_lines);                  /* rien sur la carte */
    hold(1, 5100);
    TEST_ASSERT_EQUAL(MODE_STANDARD, mode_get());
    TEST_ASSERT_EQUAL(1, g_mountCalls);
}

/* ---- Mode configuration ------------------------------------------------- */

static void test_config_au_demarrage_et_inactivite(void)
{
    boot(true);
    run(10);
    TEST_ASSERT_EQUAL(MODE_CONFIG, mode_get());
    TEST_ASSERT_EQUAL_UINT8(255, g_ledR);           /* jaune */
    TEST_ASSERT_EQUAL_UINT8(160, g_ledG);
    fake_setButtonLevel(1, g_buttonIdle);
    run(10 * MIN_MS);
    TEST_ASSERT_EQUAL(0, g_lines);                  /* acquisition arrêtée */
    fake_serialPush(SERIAL_CONSOLE, "log_interval = 5");
    run(10);
    TEST_ASSERT_NOT_NULL(strstr(fake_serialOut(), "OK"));
    run(30 * MIN_MS - 100);
    TEST_ASSERT_EQUAL(MODE_CONFIG, mode_get());     /* commande = délai relancé */
    run(200);
    TEST_ASSERT_EQUAL(MODE_STANDARD, mode_get());
    run(1500);
    TEST_ASSERT_EQUAL(1, g_lines);
    TEST_ASSERT_EQUAL(5, config_get()->logInterval);
}

static void test_bouton_rouge_tenu_au_demarrage_pas_de_maintenance(void)
{
    boot(true);
    run(6000);                                      /* toujours appuyé après 5 s */
    TEST_ASSERT_EQUAL(MODE_CONFIG, mode_get());
}

static void test_parametres_conserves_apres_coupure(void)
{
    boot(true);
    fake_serialPush(SERIAL_CONSOLE, "FILE_MAX_SIZE=4096");
    fake_serialPush(SERIAL_CONSOLE, "MIN_TEMP_AIR=-20");
    run(10);
    /* coupure : la Flash simulée est gardée, la RAM repart de zéro */
    memset(&s_config, 0, sizeof(s_config));
    config_init();
    TEST_ASSERT_EQUAL(4096, config_get()->fileMaxSize);
    TEST_ASSERT_EQUAL(-20, config_get()->minTempAir);
}

static void test_flash_abimee_valeurs_par_defaut(void)
{
    boot(true);
    fake_serialPush(SERIAL_CONSOLE, "TIMEOUT=12");
    run(10);
    g_fakeFlash[6] ^= 0x55;                         /* un octet corrompu */
    config_init();
    TEST_ASSERT_EQUAL(CFG_DEF_TIMEOUT, config_get()->timeout);
}

/* ---- Console ----------------------------------------------------------- */

static const char *cmd(const char *c)
{
    char line[64];
    fake_serialClear();
    strcpy(line, c);
    console_execute(line);
    return fake_serialOut();
}

static void test_console_reponses(void)
{
    TEST_ASSERT_EQUAL_STRING("OK\r\n", cmd("LOG_INTERVAL=1440"));
    TEST_ASSERT_EQUAL_STRING("ERREUR : valeur hors domaine\r\n", cmd("LOG_INTERVAL=1441"));
    TEST_ASSERT_EQUAL_STRING("ERREUR : valeur hors domaine\r\n", cmd("LOG_INTERVAL=0"));
    TEST_ASSERT_EQUAL_STRING("ERREUR : valeur hors domaine\r\n", cmd("LUMIN=abc"));
    TEST_ASSERT_EQUAL(1, config_get()->lumin);      /* inchangé */
    TEST_ASSERT_EQUAL_STRING("OK\r\n", cmd("log_intervall = 15"));
    TEST_ASSERT_EQUAL(15, config_get()->logInterval);
    TEST_ASSERT_EQUAL_STRING("ERREUR : commande inconnue\r\n", cmd("FOO=3"));
    TEST_ASSERT_EQUAL_STRING("ERREUR : commande inconnue\r\n", cmd("BONJOUR"));
    TEST_ASSERT_EQUAL_STRING("ERREUR : valeur hors domaine\r\n", cmd("PRESSURE_MIN=1090")); /* > MAX */
    TEST_ASSERT_EQUAL_STRING("OK\r\n", cmd("TEMP_AIR=0"));
    TEST_ASSERT_EQUAL_STRING("OK\r\n", cmd("RESET"));
    TEST_ASSERT_EQUAL(1, config_get()->tempAir);
    TEST_ASSERT_EQUAL(10, config_get()->logInterval);
    TEST_ASSERT_NOT_NULL(strstr(cmd("VERSION"), "1.0.0"));
    TEST_ASSERT_NOT_NULL(strstr(cmd("version"), "G3-2026-01"));
}

static void test_console_horloge(void)
{
    TEST_ASSERT_EQUAL_STRING("OK\r\n", cmd("CLOCK=08:05:09"));
    TEST_ASSERT_EQUAL(8, g_rtc.hours);
    TEST_ASSERT_EQUAL(5, g_rtc.minutes);
    TEST_ASSERT_EQUAL(9, g_rtc.seconds);
    TEST_ASSERT_EQUAL_STRING("ERREUR : valeur hors domaine\r\n", cmd("CLOCK=24:00:00"));
    TEST_ASSERT_EQUAL_STRING("OK\r\n", cmd("DATE=02,29,2028"));
    TEST_ASSERT_EQUAL(2, g_rtc.month);
    TEST_ASSERT_EQUAL(29, g_rtc.day);
    TEST_ASSERT_EQUAL(2028, g_rtc.year);
    TEST_ASSERT_EQUAL_STRING("ERREUR : valeur hors domaine\r\n", cmd("DATE=02,29,2027"));
    TEST_ASSERT_EQUAL_STRING("ERREUR : valeur hors domaine\r\n", cmd("DATE=13,01,2026"));
    TEST_ASSERT_EQUAL_STRING("OK\r\n", cmd("day=sun"));
    TEST_ASSERT_EQUAL(7, g_rtc.dayOfWeek);
    TEST_ASSERT_EQUAL_STRING("ERREUR : valeur hors domaine\r\n", cmd("DAY=LUN"));
}

static void test_console_hors_configuration(void)
{
    fake_serialPush(SERIAL_CONSOLE, "LOG_INTERVAL=3");
    run(10);
    TEST_ASSERT_EQUAL(10, config_get()->logInterval);
    TEST_ASSERT_NOT_NULL(strstr(fake_serialOut(), "mode configuration seulement"));
}

/* ---- Erreurs ----------------------------------------------------------- */

static void test_gps_muet_na_et_erreur_apres_timeout(void)
{
    g_gps = GPS_OFF;
    run(29000);
    TEST_ASSERT_EQUAL(0, g_lines);                  /* attend TIMEOUT = 30 s */
    run(1500);
    TEST_ASSERT_EQUAL(1, g_lines);
    TEST_ASSERT_EQUAL_STRING("2026-10-20;14:30:00;NA;NA;21.5;45.0;1013.2;512;MOYENNE", g_lastLine);
    TEST_ASSERT_EQUAL(STATUS_ERR_GPS, led_highestError());
    g_gps = GPS_FIX;
    run(10 * MIN_MS);
    TEST_ASSERT_EQUAL(STATUS_OK, led_highestError()); /* l'erreur disparaît avec le problème */
}

static void test_gps_sans_position_na_sans_erreur(void)
{
    g_gps = GPS_NOFIX;
    run(1500);
    TEST_ASSERT_EQUAL(1, g_lines);
    TEST_ASSERT_NOT_NULL(strstr(g_lastLine, ";NA;NA;21.5"));
    TEST_ASSERT_EQUAL(STATUS_OK, led_highestError());
}

static void test_capteur_muet_erreur_au_2e_timeout(void)
{
    g_bmePresent = false;
    run(31000);
    TEST_ASSERT_EQUAL(1, g_lines);
    TEST_ASSERT_EQUAL_STRING("2026-10-20;14:30:00;49.11930;6.17570;NA;NA;NA;512;MOYENNE", g_lastLine);
    TEST_ASSERT_EQUAL(STATUS_OK, led_highestError()); /* 1er timeout : NA seulement */
    run(10 * MIN_MS);
    TEST_ASSERT_EQUAL(2, g_lines);
    TEST_ASSERT_EQUAL(STATUS_ERR_SENSOR, led_highestError());
    g_bmePresent = true;                            /* rebranché */
    run(10 * MIN_MS);
    TEST_ASSERT_EQUAL(STATUS_OK, led_highestError());
    TEST_ASSERT_NOT_NULL(strstr(g_lastLine, ";21.5;45.0;1013.2;"));
}

static void test_capteur_absent_au_demarrage(void)
{
    fake_reset();
    g_bmePresent = false;
    config_init(); led_init(); buttons_init(); rtc_init(); gps_init();
    led_setError(STATUS_ERR_SENSOR, !sensors_init());
    acquisition_init(); mode_init(false);
    g_gps = GPS_FIX;
    TEST_ASSERT_EQUAL(STATUS_ERR_SENSOR, led_highestError());
    run(31000);
    TEST_ASSERT_EQUAL(STATUS_ERR_SENSOR, led_highestError());   /* reste allumée */
}

static void test_bme280_reconnu(void)
{
    fake_reset();
    g_bmePresent = false;                           /* pas de BME680... */
    g_bme280Present = true;                         /* ...mais un BME280 */
    fake_bme280Load();
    config_init(); led_init(); buttons_init(); rtc_init(); gps_init();
    led_setError(STATUS_ERR_SENSOR, !sensors_init());
    acquisition_init(); mode_init(false);
    g_gps = GPS_FIX; g_lines = 0;
    TEST_ASSERT_EQUAL_STRING("BME280", sensors_ambientName());
    run(1500);
    TEST_ASSERT_EQUAL_STRING("2026-10-20;14:30:00;49.11930;6.17570;25.1;55.0;1006.5;512;MOYENNE", g_lastLine);
    TEST_ASSERT_EQUAL(STATUS_OK, led_highestError());
}

static void test_bmp280_sans_hygrometrie(void)
{
    fake_reset();
    g_bmePresent = false;
    g_bme280Present = true;
    fake_bme280Load();
    g_bme280Regs[0xD0] = 0x58;                      /* BMP280 */
    config_init(); led_init(); buttons_init(); rtc_init(); gps_init();
    sensors_init();
    acquisition_init(); mode_init(false);
    g_gps = GPS_FIX; g_lines = 0;
    TEST_ASSERT_EQUAL_STRING("BMP280 (sans hygrometrie)", sensors_ambientName());
    run(31000);                                     /* l'hygrométrie attend TIMEOUT */
    TEST_ASSERT_EQUAL_STRING("2026-10-20;14:30:00;49.11930;6.17570;25.1;NA;1006.5;512;MOYENNE", g_lastLine);
}

static void test_temperature_incoherente(void)
{
    boot(true);
    cmd("MIN_TEMP_AIR=30");
    boot(false);
    run(1500);
    TEST_ASSERT_EQUAL_STRING("2026-10-20;14:30:00;49.11930;6.17570;NA;45.0;1013.2;512;MOYENNE", g_lastLine);
    TEST_ASSERT_EQUAL(STATUS_ERR_SENSOR_INCOHERENT, led_highestError());
    /* vert deux fois plus long que le rouge */
    run(1000 - (fake_now() % 1000) + 100);
    TEST_ASSERT_EQUAL_UINT8(255, g_ledR); TEST_ASSERT_EQUAL_UINT8(0, g_ledG);
    run(300);
    TEST_ASSERT_EQUAL_UINT8(0, g_ledR);   TEST_ASSERT_EQUAL_UINT8(255, g_ledG);
}

static void test_hygrometrie_ignoree_hors_plage(void)
{
    boot(true);
    cmd("HYGR_MAXT=15");
    boot(false);
    run(1500);
    TEST_ASSERT_EQUAL_STRING("2026-10-20;14:30:00;49.11930;6.17570;21.5;;1013.2;512;MOYENNE", g_lastLine);
    TEST_ASSERT_EQUAL(STATUS_OK, led_highestError());
}

static void test_capteur_desactive_champ_vide(void)
{
    boot(true);
    cmd("LUMIN=0");
    cmd("PRESSURE=0");
    boot(false);
    run(1500);
    TEST_ASSERT_EQUAL_STRING("2026-10-20;14:30:00;49.11930;6.17570;21.5;45.0;;;", g_lastLine);
}

static void test_luminosite_niveaux(void)
{
    g_lightRaw = 400;      /* 100 < 255 */
    run(1500);
    TEST_ASSERT_NOT_NULL(strstr(g_lastLine, ";100;FAIBLE"));
    g_lightRaw = 4000;     /* 1000 > 768 */
    run(10 * MIN_MS);
    TEST_ASSERT_NOT_NULL(strstr(g_lastLine, ";1000;FORTE"));
}

static void test_rtc_muette(void)
{
    g_rtcPresent = false;
    run(1500);
    TEST_ASSERT_EQUAL_STRING("NA;NA;49.11930;6.17570;21.5;45.0;1013.2;512;MOYENNE", g_lastLine);
    TEST_ASSERT_TRUE(g_lastDtNull);
    TEST_ASSERT_EQUAL(STATUS_ERR_RTC, led_highestError());
    run(1000 - (fake_now() % 1000) + 600);          /* 2e moitié : bleu */
    TEST_ASSERT_EQUAL_UINT8(0, g_ledR); TEST_ASSERT_EQUAL_UINT8(255, g_ledB);
}

static void test_priorite_des_erreurs(void)
{
    g_rtcPresent = false;
    g_gps = GPS_OFF;
    g_bmePresent = false;
    run(31000);
    run(10 * MIN_MS);
    /* RTC (4) > GPS (3) > capteur (1) */
    TEST_ASSERT_EQUAL(STATUS_ERR_RTC, led_highestError());
    g_rtcPresent = true;
    run(10 * MIN_MS);
    TEST_ASSERT_EQUAL(STATUS_ERR_GPS, led_highestError());
}

static void test_gga_champs_vides(void)
{
    GPS_Data d;
    char s[100];
    nmea(s, "GNGGA,143000.00,,,,,0,00,99.99,,,,,,");
    TEST_ASSERT_TRUE(gps_parseGGA(s, &d));
    TEST_ASSERT_FALSE(d.valid);
    nmea(s, "GPGGA,123519,4807.038,S,01131.000,W,1,08,0.9,545.4,M,46.9,M,,");
    TEST_ASSERT_TRUE(gps_parseGGA(s, &d));
    TEST_ASSERT_TRUE(d.valid);
    TEST_ASSERT_FLOAT_WITHIN(1e-4, -48.1173, d.latitude);
    TEST_ASSERT_FLOAT_WITHIN(1e-4, -11.51667, d.longitude);
    s[10] = 'X';                                    /* somme de contrôle fausse */
    TEST_ASSERT_FALSE(gps_parseGGA(s, &d));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_premiere_mesure_complete);
    RUN_TEST(test_intervalle_10_minutes);
    RUN_TEST(test_boutons_actifs_etat_haut);
    RUN_TEST(test_couleur_pendant_appui);
    RUN_TEST(test_maintenance_gps_sans_position);
    RUN_TEST(test_capteur_lumiere_debranche);
    RUN_TEST(test_lumiere_pas_faussee_par_le_test);
    RUN_TEST(test_capteur_bloque_se_reinitialise);
    RUN_TEST(test_gsv_satellites_en_vue);
    RUN_TEST(test_appui_court_ignore);
    RUN_TEST(test_vert_5s_economique_puis_retour);
    RUN_TEST(test_economique_gps_un_cycle_sur_deux);
    RUN_TEST(test_vert_depuis_maintenance_ignore);
    RUN_TEST(test_rouge_depuis_economique_ignore);
    RUN_TEST(test_maintenance_direct_sans_carte);
    RUN_TEST(test_config_au_demarrage_et_inactivite);
    RUN_TEST(test_bouton_rouge_tenu_au_demarrage_pas_de_maintenance);
    RUN_TEST(test_parametres_conserves_apres_coupure);
    RUN_TEST(test_flash_abimee_valeurs_par_defaut);
    RUN_TEST(test_console_reponses);
    RUN_TEST(test_console_horloge);
    RUN_TEST(test_console_hors_configuration);
    RUN_TEST(test_gps_muet_na_et_erreur_apres_timeout);
    RUN_TEST(test_gps_sans_position_na_sans_erreur);
    RUN_TEST(test_capteur_muet_erreur_au_2e_timeout);
    RUN_TEST(test_capteur_absent_au_demarrage);
    RUN_TEST(test_bme280_reconnu);
    RUN_TEST(test_bmp280_sans_hygrometrie);
    RUN_TEST(test_temperature_incoherente);
    RUN_TEST(test_hygrometrie_ignoree_hors_plage);
    RUN_TEST(test_capteur_desactive_champ_vide);
    RUN_TEST(test_luminosite_niveaux);
    RUN_TEST(test_rtc_muette);
    RUN_TEST(test_priorite_des_erreurs);
    RUN_TEST(test_gga_champs_vides);
    return UNITY_END();
}

/**
 * @file constants.h
 * @brief Délais, tailles et valeurs par défaut. Aucun nombre magique ailleurs.
 *
 * Les valeurs entourées de #ifndef peuvent être remplacées à la compilation
 * (build_flags de platformio.ini). L'environnement "demo" s'en sert pour
 * accélérer la démonstration sans toucher au code.
 */
#ifndef CONSTANTS_H
#define CONSTANTS_H

/* ---- Boutons ---------------------------------------------------------- */
#define LONG_PRESS_MS          5000U      /* appui long */
#define DEBOUNCE_MS            50U        /* anti-rebond */
#define BOOT_SAMPLE_MS         30U        /* lecture du bouton rouge au démarrage */
#ifndef BUTTON_PRESSED_LEVEL
#define BUTTON_PRESSED_LEVEL   (-1)       /* niveau appuyé : -1 = mesuré au démarrage, ou 0 / 1 */
#endif
#ifndef RED_IS_BUTTON1
#define RED_IS_BUTTON1         1          /* 1 : rouge sur PA10, vert sur PB3 */
#endif

/* ---- Temps ------------------------------------------------------------ */
#ifndef LOG_UNIT_MS
#define LOG_UNIT_MS            60000UL    /* LOG_INTERVAL est en minutes */
#endif
#define TIMEOUT_UNIT_MS        1000UL     /* TIMEOUT est en secondes */
#ifndef CONFIG_IDLE_MS
#define CONFIG_IDLE_MS         1800000UL  /* 30 min sans commande */
#endif
#define ECO_INTERVAL_FACTOR    2U         /* LOG_INTERVAL x 2 en économique */
#define ECO_GPS_EVERY          2U         /* GPS lu un cycle sur 2 */
#define LIVE_PERIOD_MS         2000UL     /* maintenance : une mesure toutes les 2 s */
#define GSV_MAX_AGE_MS         5000UL     /* satellites en vue : trames GSV récentes */
#define LIGHT_PROBE_US         50U        /* test de présence du capteur de lumière */
#define LIVE_TIMEOUT_MS        1500UL     /* maintenance : attente maximale d'une mesure */
#define LED_PERIOD_MS          1000U      /* clignotement à 1 Hz */

/* ---- Tailles ---------------------------------------------------------- */
#define LOG_LINE_MAX           128
#define LIVE_LINE_MAX          200        /* ligne lisible du mode maintenance */
#define CONSOLE_LINE_MAX       64
#define NMEA_LINE_MAX          96
#define RX_BUFFER_SIZE         128        /* par tampon, puissance de 2 */
#define FILE_NAME_MAX          16         /* "AAMMJJ_65535.LOG" */
#define LOG_EOL                "\r\n"     /* fichiers lisibles sous Windows */

/* ---- Liaisons --------------------------------------------------------- */
#define I2C_TIMEOUT_MS         10U
#define UART_TX_TIMEOUT_MS     100U
#define LIGHT_ADC_DIVIDER      4U         /* 0..4095 -> 0..1023 */
#define LIGHT_ADC_ERROR        0xFFFFU    /* valeur renvoyée par grove_light */
#define BME_MEAS_MARGIN_MS     2U
#define BME280_MEAS_MS         10U        /* mesure forcée x1 : 9,3 ms au plus */

/* ---- Flash ------------------------------------------------------------ */
#ifndef CONFIG_FLASH_ADDR
#define CONFIG_FLASH_ADDR      0x080FF800UL  /* dernière page (banque 2) */
#endif
#ifdef DEMO_BUILD
#define CONFIG_MAGIC           0x33570D01UL  /* défauts "démo" au 1er démarrage */
#else
#define CONFIG_MAGIC           0x33570101UL
#endif

/* ---- Valeurs par défaut des paramètres -------------------------------- */
#define CFG_DEF_LOG_INTERVAL   10
#define CFG_DEF_FILE_MAX_SIZE  2048
#ifndef CFG_DEF_TIMEOUT
#define CFG_DEF_TIMEOUT        30
#endif
#define CFG_DEF_LUMIN          1
#define CFG_DEF_LUMIN_LOW      255
#define CFG_DEF_LUMIN_HIGH     768
#define CFG_DEF_TEMP_AIR       1
#define CFG_DEF_MIN_TEMP_AIR   (-10)
#define CFG_DEF_MAX_TEMP_AIR   60
#define CFG_DEF_HYGR           1
#define CFG_DEF_HYGR_MINT      0
#define CFG_DEF_HYGR_MAXT      50
#define CFG_DEF_PRESSURE       1
#define CFG_DEF_PRESSURE_MIN   850
#define CFG_DEF_PRESSURE_MAX   1080

/* ---- Traces sur la console -------------------------------------------- */
#ifndef TRACE_ENABLED
#define TRACE_ENABLED          1          /* recopie chaque ligne enregistrée */
#endif

#endif /* CONSTANTS_H */

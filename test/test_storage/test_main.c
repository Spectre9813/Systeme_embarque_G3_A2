/**
 * @file test_storage/test_main.c
 * @brief Tests sur PC du module storage avec le vrai FatFs, sur une carte SD
 *        simulée en RAM : noms, en-tête, archivage, carte pleine, retrait.
 */
#include "fakes.h"
#include <unity.h>
#include <string.h>
#include <stdio.h>

#include "../../src/config.c"
#include "../../src/led.c"
#include "../../src/storage.c"
#include "../../lib/fatfs/ff.c"
#include "../../lib/fatfs/ffunicode.c"
#include "../stubs/fakes.c"

/* ---- Carte SD en RAM ----------------------------------------------------- */
#define DISK_SECTORS 1024U              /* 512 Ko */
static uint8_t s_disk[DISK_SECTORS][512];
static bool s_present = true;

void sd_diskio_release(void) { }
void HAL_GPIO_EXTI_Callback(uint16_t pin) { (void)pin; }   /* pas de boutons ici */
DSTATUS disk_status(BYTE p) { (void)p; return s_present ? 0 : STA_NOINIT; }
DSTATUS disk_initialize(BYTE p) { (void)p; return s_present ? 0 : STA_NOINIT; }
DRESULT disk_read(BYTE p, BYTE *b, LBA_t s, UINT n)
{
    (void)p;
    if (!s_present) return RES_NOTRDY;
    memcpy(b, s_disk[s], n * 512U);
    return RES_OK;
}
DRESULT disk_write(BYTE p, const BYTE *b, LBA_t s, UINT n)
{
    (void)p;
    if (!s_present) return RES_NOTRDY;
    memcpy(s_disk[s], b, n * 512U);
    return RES_OK;
}
DRESULT disk_ioctl(BYTE p, BYTE cmd, void *buff)
{
    (void)p;
    if (cmd == GET_SECTOR_COUNT) { *(LBA_t *)buff = DISK_SECTORS; return RES_OK; }
    if (cmd == GET_BLOCK_SIZE) { *(DWORD *)buff = 1; return RES_OK; }
    return RES_OK;
}

static void format(void)
{
    static FATFS fs;
    BYTE work[FF_MAX_SS];
    MKFS_PARM opt = {FM_FAT, 0, 0, 0, 512};    /* clusters de 512 octets */
    s_present = true;
    TEST_ASSERT_EQUAL(FR_OK, f_mkfs("", &opt, work, sizeof(work)));
    (void)fs;
}

static RTC_DateTime day(uint8_t d)
{
    RTC_DateTime dt = {0, 30, 14, 2, d, 10, 2026};
    return dt;
}

static bool exists(const char *name, FSIZE_t *size)
{
    FILINFO fno;
    if (f_stat(name, &fno) != FR_OK) return false;
    if (size) *size = fno.fsize;
    return true;
}

static void readFile(const char *name, char *buf, size_t len)
{
    FIL f; UINT n;
    TEST_ASSERT_EQUAL(FR_OK, f_open(&f, name, FA_READ));
    f_read(&f, buf, (UINT)len - 1, &n);
    buf[n] = '\0';
    f_close(&f);
}

static const char *LINE = "2026-10-20;14:30:00;49.11930;6.17570;12.4;81.2;1013.2;512;MOYENNE";
static const char *HEADER = "DATE;HEURE;LATITUDE;LONGITUDE;TEMP_AIR;HYGR;PRESSURE;LUMIN;LUMIN_NIV";

void setUp(void)
{
    fake_reset();
    fake_eraseFlash();
    config_init();
    led_init();
    format();
    storage_setHeader(HEADER);
    TEST_ASSERT_TRUE(storage_init());
}
void tearDown(void) { }

static void test_premiere_ligne_avec_entete(void)
{
    RTC_DateTime dt = day(20);
    char name[20], buf[600];
    TEST_ASSERT_TRUE(storage_writeLine(&dt, LINE, name));
    TEST_ASSERT_EQUAL_STRING("261020_0.LOG", name);
    readFile("261020_0.LOG", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("DATE;HEURE;LATITUDE;LONGITUDE;TEMP_AIR;HYGR;PRESSURE;LUMIN;LUMIN_NIV\r\n"
                             "2026-10-20;14:30:00;49.11930;6.17570;12.4;81.2;1013.2;512;MOYENNE\r\n", buf);
}

static void test_archivage_taille_max(void)
{
    RTC_DateTime dt = day(20);
    FSIZE_t s0, s1, s2;
    char buf[600];
    int i;

    config_set("FILE_MAX_SIZE", 512);
    const FSIZE_t h = strlen(HEADER) + 2, l = strlen(LINE) + 2;   /* 70 et 67 octets avec \r\n */
    /* en-tête + 6 lignes = 472 ; la 7e ferait 539 > 512 */
    for (i = 0; i < 6; i++) TEST_ASSERT_TRUE(storage_writeLine(&dt, LINE, NULL));
    TEST_ASSERT_FALSE(exists("261020_1.LOG", NULL));
    TEST_ASSERT_TRUE(storage_writeLine(&dt, LINE, NULL));
    TEST_ASSERT_TRUE(exists("261020_1.LOG", &s1));
    TEST_ASSERT_TRUE(exists("261020_0.LOG", &s0));
    TEST_ASSERT_EQUAL(h + 6 * l, s1);
    TEST_ASSERT_EQUAL(h + l, s0);                       /* nouvel en-tête + 1 ligne */
    for (i = 0; i < 6; i++) TEST_ASSERT_TRUE(storage_writeLine(&dt, LINE, NULL));
    TEST_ASSERT_TRUE(exists("261020_2.LOG", &s2));
    TEST_ASSERT_TRUE(s2 <= 512);
    readFile("261020_0.LOG", buf, sizeof(buf));
    TEST_ASSERT_EQUAL(0, strncmp(buf, HEADER, strlen(HEADER)));   /* chaque fichier a son en-tête */
}

static void test_numeros_au_dela_de_9(void)
{
    RTC_DateTime dt = day(20);
    int i;
    config_set("FILE_MAX_SIZE", 512);
    for (i = 0; i < 6 * 12; i++) TEST_ASSERT_TRUE(storage_writeLine(&dt, LINE, NULL));
    TEST_ASSERT_TRUE(exists("261020_11.LOG", NULL));    /* nom long (LFN) */
}

static void test_nouveau_fichier_chaque_jour(void)
{
    RTC_DateTime d1 = day(20), d2 = day(21);
    char name[20];
    TEST_ASSERT_TRUE(storage_writeLine(&d1, LINE, name));
    TEST_ASSERT_TRUE(storage_writeLine(&d2, LINE, name));
    TEST_ASSERT_EQUAL_STRING("261021_0.LOG", name);
    TEST_ASSERT_TRUE(exists("261020_0.LOG", NULL));
}

static void test_rtc_muette_dernier_fichier(void)
{
    RTC_DateTime d = day(20);
    char name[20];
    TEST_ASSERT_TRUE(storage_writeLine(&d, LINE, name));
    TEST_ASSERT_TRUE(storage_writeLine(NULL, "NA;NA;...", name));
    TEST_ASSERT_EQUAL_STRING("261020_0.LOG", name);
}

static void test_rtc_muette_des_le_depart(void)
{
    char name[20];
    s_lastBase[0] = '\0';                               /* comme après un démarrage */
    TEST_ASSERT_TRUE(storage_writeLine(NULL, "NA;NA;...", name));
    TEST_ASSERT_EQUAL_STRING("NODATE_0.LOG", name);
}

static void test_carte_pleine(void)
{
    RTC_DateTime d = day(20);
    FIL f; UINT w;
    static uint8_t chunk[1024];
    int n = 0;

    /* remplissage par un gros fichier, comme le script de préparation de la démo */
    TEST_ASSERT_EQUAL(FR_OK, f_open(&f, "FILL.BIN", FA_WRITE | FA_CREATE_ALWAYS));
    while (f_write(&f, chunk, sizeof(chunk), &w) == FR_OK && w == sizeof(chunk)) { }
    f_close(&f);

    while (storage_writeLine(&d, LINE, NULL) && n < 100) n++;
    TEST_ASSERT_TRUE(n < 100);
    TEST_ASSERT_EQUAL(STATUS_ERR_SD_FULL, led_highestError());

    f_unlink("FILL.BIN");                               /* place libérée */
    TEST_ASSERT_TRUE(storage_writeLine(&d, LINE, NULL));
    TEST_ASSERT_EQUAL(STATUS_OK, led_highestError());
}

static void test_repertoire_plein(void)
{
    /* FAT12 : répertoire racine limité à 512 entrées. Chaque archive au nom
       long (n >= 10) en prend 2 ou 3 : le répertoire sature avant la place. */
    RTC_DateTime d = day(20);
    int n = 0;
    config_set("FILE_MAX_SIZE", 512);
    while (storage_writeLine(&d, LINE, NULL) && n < 5000) n++;
    TEST_ASSERT_TRUE(n < 5000);
    TEST_ASSERT_EQUAL(STATUS_ERR_SD_FULL, led_highestError());   /* et non "accès" */
    TEST_ASSERT_TRUE(storage_isMounted());
}

static void test_carte_retiree_puis_remise(void)
{
    RTC_DateTime d = day(20);
    FSIZE_t s;
    TEST_ASSERT_TRUE(storage_writeLine(&d, LINE, NULL));
    s_present = false;
    TEST_ASSERT_FALSE(storage_writeLine(&d, LINE, NULL));
    TEST_ASSERT_EQUAL(STATUS_ERR_SD_ACCESS, led_highestError());
    TEST_ASSERT_FALSE(storage_writeLine(&d, LINE, NULL));   /* toujours absente */
    s_present = true;
    TEST_ASSERT_TRUE(storage_writeLine(&d, LINE, NULL));
    TEST_ASSERT_EQUAL(STATUS_OK, led_highestError());
    TEST_ASSERT_TRUE(exists("261020_0.LOG", &s));
    TEST_ASSERT_EQUAL(strlen(HEADER) + 2 + 2 * (strlen(LINE) + 2), s);   /* pas de ligne en double */
}

static void test_maintenance_demontage(void)
{
    RTC_DateTime d = day(20);
    storage_unmount();
    TEST_ASSERT_FALSE(storage_writeLine(&d, LINE, NULL));
    TEST_ASSERT_EQUAL(STATUS_OK, led_highestError());   /* retrait volontaire : pas d'erreur */
    TEST_ASSERT_TRUE(storage_mount());
    TEST_ASSERT_TRUE(storage_writeLine(&d, LINE, NULL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_premiere_ligne_avec_entete);
    RUN_TEST(test_archivage_taille_max);
    RUN_TEST(test_numeros_au_dela_de_9);
    RUN_TEST(test_nouveau_fichier_chaque_jour);
    RUN_TEST(test_rtc_muette_dernier_fichier);
    RUN_TEST(test_rtc_muette_des_le_depart);
    RUN_TEST(test_carte_pleine);
    RUN_TEST(test_repertoire_plein);
    RUN_TEST(test_carte_retiree_puis_remise);
    RUN_TEST(test_maintenance_demontage);
    return UNITY_END();
}

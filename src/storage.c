/**
 * @file storage.c
 * @brief Fichiers AAMMJJ_0.LOG : écriture ligne par ligne, archivage, retrait.
 *
 * - Le fichier est ouvert puis refermé à chaque ligne : une coupure ne fait
 *   perdre que la ligne en cours.
 * - Quand la ligne ferait dépasser FILE_MAX_SIZE, AAMMJJ_0.LOG est renommé
 *   AAMMJJ_n.LOG (premier n libre) et un nouveau _0 est créé : rien n'est
 *   recopié. Le prochain n libre est gardé en mémoire pour ne pas relire le
 *   répertoire à chaque archivage.
 */
#include "storage.h"
#include "config.h"
#include "led.h"
#include "constants.h"
#include "sd_diskio.h"
#include "ff.h"
#include <stdio.h>
#include <string.h>

#define NO_DATE_BASE "NODATE"
#define BASE_LEN     7          /* "AAMMJJ" + '\0' */

static FATFS s_fs;
static bool s_mounted;
static bool s_suspended;        /* démontée pour le retrait (maintenance) */
static const char *s_fileHeader = "";
static char s_lastBase[BASE_LEN];    /* dernier fichier utilisé */
static char s_revBase[BASE_LEN];     /* jour dont s_nextRev est connu */
static uint16_t s_nextRev;
static RTC_DateTime s_stamp;         /* date des fichiers pour FatFs */
static bool s_stampValid;

static void storage_fileName(const char *base, uint16_t revision, char *name)
{
    snprintf(name, FILE_NAME_MAX + 1, "%.6s_%u.LOG", base, (unsigned)revision);
}

/** @brief "AAMMJJ" à partir de la date (déjà contrôlée par rtc_now). */
static void storage_baseFromDate(const RTC_DateTime *dt, char *base)
{
    const uint8_t v[3] = {(uint8_t)(dt->year % 100U), (uint8_t)(dt->month % 100U), (uint8_t)(dt->day % 100U)};
    uint8_t i;
    for (i = 0; i < 3U; i++)
    {
        base[2U * i] = (char)('0' + v[i] / 10U);
        base[2U * i + 1U] = (char)('0' + v[i] % 10U);
    }
    base[6] = '\0';
}

static void storage_fail(void)
{
    /* Carte retirée ou abîmée : on la remontera à la prochaine écriture. */
    f_mount(NULL, "", 0);
    sd_diskio_release();
    s_mounted = false;
    s_revBase[0] = '\0';
    led_setError(STATUS_ERR_SD_ACCESS, true);
}

bool storage_mount(void)
{
    s_suspended = false;
    sd_diskio_release();
    if (f_mount(&s_fs, "", 1) != FR_OK)
    {
        storage_fail();
        return false;
    }
    s_mounted = true;
    s_revBase[0] = '\0';         /* la carte a pu changer */
    led_setError(STATUS_ERR_SD_ACCESS, false);
    return true;
}

bool storage_init(void)
{
    return storage_mount();
}

void storage_unmount(void)
{
    f_mount(NULL, "", 0);
    sd_diskio_release();
    s_mounted = false;
    s_suspended = true;
    led_setError(STATUS_ERR_SD_ACCESS, false);
    led_setError(STATUS_ERR_SD_FULL, false);
}

bool storage_isMounted(void)
{
    return s_mounted;
}

void storage_setHeader(const char *header)
{
    s_fileHeader = header;
}

/**
 * @brief Place restante pour ajouter bytes octets à un fichier de fileSize octets.
 * Un fichier occupe des clusters entiers : on compte la place libre au bout
 * de son dernier cluster, puis les clusters libres de la carte.
 */
static bool storage_hasFreeSpace(uint32_t fileSize, uint32_t bytes)
{
    DWORD freeClusters;
    FATFS *fs;
    uint32_t clusterBytes, room, needed;

    if (f_getfree("", &freeClusters, &fs) != FR_OK)
        return false;
    clusterBytes = (uint32_t)fs->csize * FF_MAX_SS;
    room = (fileSize % clusterBytes == 0U) ? 0U : clusterBytes - fileSize % clusterBytes;
    if (bytes <= room)
        return true;
    needed = (bytes - room + clusterBytes - 1U) / clusterBytes;
    return freeClusters >= needed;
}

/**
 * @brief Renomme base_0.LOG en base_n.LOG, n étant le premier numéro libre.
 * @return FR_OK, FR_DENIED si le répertoire est plein, ou une autre erreur FatFs
 */
static FRESULT storage_archive(const char *base)
{
    char from[FILE_NAME_MAX + 1], to[FILE_NAME_MAX + 1];
    FILINFO fno;
    FRESULT fr;

    if (strcmp(s_revBase, base) != 0)
    {
        strcpy(s_revBase, base);
        s_nextRev = 1;
    }
    for (;;)
    {
        storage_fileName(base, s_nextRev, to);
        fr = f_stat(to, &fno);
        if (fr == FR_NO_FILE)
            break;
        if (fr != FR_OK)
            return fr;
        if (s_nextRev == 0xFFFFU)
            return FR_DENIED;
        s_nextRev++;
    }
    storage_fileName(base, 0, from);
    fr = f_rename(from, to);
    if (fr == FR_OK)
        s_nextRev++;
    return fr;
}

/** @brief Carte ou répertoire plein (FR_DENIED) : erreur "carte pleine", sinon erreur d'accès. */
static bool storage_error(FRESULT fr)
{
    if (fr == FR_DENIED)
        led_setError(STATUS_ERR_SD_FULL, true);
    else
        storage_fail();
    return false;
}

bool storage_writeLine(const RTC_DateTime *dt, const char *line, char *nameOut)
{
    char base[BASE_LEN], name[FILE_NAME_MAX + 1];
    FILINFO fno;
    FIL fil;
    FRESULT fr;
    UINT written;
    uint32_t size = 0, lineLen, headLen, need;
    bool ok;

    if (s_suspended)
        return false;
    if (!s_mounted && !storage_mount())
        return false;

    if (dt != NULL)
    {
        storage_baseFromDate(dt, base);
        s_stamp = *dt;
        s_stampValid = true;
    }
    else    /* RTC muette : ligne ajoutée au dernier fichier utilisé */
        strcpy(base, s_lastBase[0] ? s_lastBase : NO_DATE_BASE);

    storage_fileName(base, 0, name);
    lineLen = (uint32_t)(strlen(line) + strlen(LOG_EOL));
    headLen = (uint32_t)(strlen(s_fileHeader) + strlen(LOG_EOL));

    fr = f_stat(name, &fno);
    if (fr == FR_OK)
        size = (uint32_t)fno.fsize;
    else if (fr != FR_NO_FILE)
    {
        storage_fail();
        return false;
    }

    if (size > 0U && size + lineLen > config_get()->fileMaxSize)
    {
        fr = storage_archive(base);
        if (fr != FR_OK)
            return storage_error(fr);
        size = 0;
    }

    need = lineLen + (size == 0U ? headLen : 0U);
    if (!storage_hasFreeSpace(size, need))
    {
        led_setError(STATUS_ERR_SD_FULL, true);
        return false;
    }
    led_setError(STATUS_ERR_SD_FULL, false);

    fr = f_open(&fil, name, FA_WRITE | FA_OPEN_APPEND);
    if (fr != FR_OK)
        return storage_error(fr);      /* répertoire plein : FR_DENIED */
    ok = true;
    if (size == 0U)
        ok = f_write(&fil, s_fileHeader, (UINT)strlen(s_fileHeader), &written) == FR_OK &&
             f_write(&fil, LOG_EOL, (UINT)strlen(LOG_EOL), &written) == FR_OK;
    ok = ok && f_write(&fil, line, (UINT)strlen(line), &written) == FR_OK && written == strlen(line);
    ok = ok && f_write(&fil, LOG_EOL, (UINT)strlen(LOG_EOL), &written) == FR_OK;
    ok = (f_close(&fil) == FR_OK) && ok;      /* f_close écrit les données sur la carte */

    if (!ok)
    {
        storage_fail();
        return false;
    }
    strcpy(s_lastBase, base);
    led_setError(STATUS_ERR_SD_ACCESS, false);
    if (nameOut != NULL)
        strcpy(nameOut, name);
    return true;
}

/** @brief Date des fichiers demandée par FatFs : celle de la dernière mesure. */
DWORD get_fattime(void)
{
    if (!s_stampValid)
        return ((DWORD)(2026 - 1980) << 25) | (1UL << 21) | (1UL << 16);
    return ((DWORD)(s_stamp.year - 1980U) << 25) | ((DWORD)s_stamp.month << 21) |
           ((DWORD)s_stamp.day << 16) | ((DWORD)s_stamp.hours << 11) |
           ((DWORD)s_stamp.minutes << 5) | ((DWORD)s_stamp.seconds >> 1);
}

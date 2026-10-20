/**
 * Test lecteur de carte SD, SD Card Shield Seeed v4.3 (projet 3W, groupe 3)
 *
 * Environnement PlatformIO : test_sd
 * Montage : shield SD, fils Dupont de l'ICSP du shield vers la Nucleo :
 *           SCK -> D13 (PA5), MISO -> D12 (PA6), MOSI -> D11 (PA7), CS sur D4 (PB5).
 *           Carte microSD/SD insérée, formatée en FAT32.
 *
 * Déroulé :
 *  1. Initialisation par le driver du dépôt (SDLogger_Init), qui affiche son diagnostic.
 *  2. En cas d'échec : test CMD0 en bit-bang (SDLogger_BitBangCMD0Test) pour savoir
 *     si le problème vient du câblage ou de la configuration SPI, puis nouvel essai toutes les 5 s.
 *  3. En cas de succès : lecture du secteur 0 (lecture seule, la carte n'est pas modifiée)
 *     et analyse du format : MBR, type de partition, système de fichiers.
 *     FatFs (prévu au livrable 2) gère FAT16 et FAT32 ; une carte en exFAT est à reformater.
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "setup.h"
#include "board_pins.h"
#include "sd_logger.h"

#define SD_CMD17        17
#define RETRY_MS        5000U

extern SPI_HandleTypeDef SD_SPI_HANDLE;

static uint8_t s_block[512];

static uint8_t spi_xfer(uint8_t tx)
{
    uint8_t rx = 0xFF;
    (void)HAL_SPI_TransmitReceive(&SD_SPI_HANDLE, &tx, &rx, 1, 100);
    return rx;
}

static void cs(bool low)
{
    HAL_GPIO_WritePin(SD_CS_PORT, SD_CS_PIN, low ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

/* Lecture d'un bloc de 512 octets (CMD17). lba = numéro de secteur. */
static bool sd_read_block(uint32_t lba, uint8_t *buf)
{
    uint32_t arg = (SDLogger_GetCardType() == SDLOGGER_CARD_SDHC) ? lba : lba * 512U;
    uint8_t r1 = 0xFF, token = 0xFF;
    uint32_t start;

    cs(true);
    (void)spi_xfer(0xFF);

    start = HAL_GetTick();
    while (spi_xfer(0xFF) != 0xFF)                       /* carte occupée */
        if (HAL_GetTick() - start > 500U) goto fail;

    (void)spi_xfer(0x40 | SD_CMD17);
    (void)spi_xfer((uint8_t)(arg >> 24));
    (void)spi_xfer((uint8_t)(arg >> 16));
    (void)spi_xfer((uint8_t)(arg >> 8));
    (void)spi_xfer((uint8_t)arg);
    (void)spi_xfer(0x01);                                /* CRC ignoré en mode SPI */

    for (int i = 0; i < 10 && (r1 & 0x80); i++) r1 = spi_xfer(0xFF);
    if (r1 != 0x00)
    {
        printf("CMD17 : reponse R1 = 0x%02X\r\n", r1);
        goto fail;
    }

    start = HAL_GetTick();
    do
    {
        token = spi_xfer(0xFF);
    } while (token == 0xFF && HAL_GetTick() - start < 500U);

    if (token != 0xFE)
    {
        printf("CMD17 : jeton de donnees 0x%02X (0xFE attendu)\r\n", token);
        goto fail;
    }

    for (int i = 0; i < 512; i++) buf[i] = spi_xfer(0xFF);
    (void)spi_xfer(0xFF);                                /* CRC des données */
    (void)spi_xfer(0xFF);

    cs(false);
    (void)spi_xfer(0xFF);
    return true;

fail:
    cs(false);
    (void)spi_xfer(0xFF);
    return false;
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static const char *fs_name(const uint8_t *boot)
{
    if (memcmp(&boot[0x52], "FAT32", 5) == 0) return "FAT32 (compatible FatFs)";
    if (memcmp(&boot[0x36], "FAT16", 5) == 0) return "FAT16 (compatible FatFs)";
    if (memcmp(&boot[0x36], "FAT12", 5) == 0) return "FAT12 (compatible FatFs)";
    if (memcmp(&boot[0x03], "EXFAT", 5) == 0) return "exFAT -> a reformater en FAT32";
    if (memcmp(&boot[0x03], "NTFS", 4) == 0)  return "NTFS -> a reformater en FAT32";
    return "inconnu";
}

static void analyse_card(void)
{
    printf("\r\nLecture du secteur 0...\r\n");
    if (!sd_read_block(0, s_block))
    {
        printf("Lecture impossible.\r\n");
        return;
    }

    printf("Premiers octets :");
    for (int i = 0; i < 16; i++) printf(" %02X", s_block[i]);
    printf("\r\nSignature 0x1FE : %02X %02X %s\r\n", s_block[510], s_block[511],
           (s_block[510] == 0x55 && s_block[511] == 0xAA) ? "(OK)" : "(absente : carte non formatee ?)");

    if (s_block[510] != 0x55 || s_block[511] != 0xAA) return;

    /* Secteur de démarrage directement en secteur 0 (carte sans table de partitions) */
    if ((s_block[0] == 0xEB || s_block[0] == 0xE9) && strcmp(fs_name(s_block), "inconnu") != 0)
    {
        printf("Pas de table de partitions, systeme de fichiers : %s\r\n", fs_name(s_block));
        return;
    }

    /* MBR : première entrée de partition à 0x1BE */
    uint8_t  type  = s_block[0x1C2];
    uint32_t first = le32(&s_block[0x1C6]);
    uint32_t count = le32(&s_block[0x1CA]);

    printf("Partition 1 : type 0x%02X, debut secteur %lu, taille %lu Mo\r\n",
           type, first, count / 2048U);

    if (type == 0)
    {
        printf("Aucune partition : formater la carte en FAT32.\r\n");
        return;
    }

    if (!sd_read_block(first, s_block))
    {
        printf("Lecture du secteur de demarrage impossible.\r\n");
        return;
    }
    printf("Systeme de fichiers : %s\r\n", fs_name(s_block));
}

static const char *status_name(SDLoggerInitStatus s)
{
    switch (s)
    {
        case SDLOGGER_INIT_OK:                 return "OK";
        case SDLOGGER_INIT_ERR_CMD0:           return "pas de reponse a CMD0 (cablage, CS, carte absente)";
        case SDLOGGER_INIT_ERR_CMD8_PATTERN:   return "reponse incorrecte a CMD8";
        case SDLOGGER_INIT_ERR_ACMD41_TIMEOUT: return "ACMD41 : la carte ne sort pas de l'etat idle";
        case SDLOGGER_INIT_ERR_CMD58:          return "CMD58 : lecture de l'OCR impossible";
        case SDLOGGER_INIT_ERR_CMD1_TIMEOUT:   return "CMD1 : ancienne carte qui ne repond pas";
        default:                               return "inconnu";
    }
}

int main(void)
{
    Global_Init();

    printf("\r\n=== Test carte SD (SPI1, CS sur D4/PB5) ===\r\n");

    bool bitbangDone = false;

    while (1)
    {
        if (SDLogger_Init())
        {
            SDLoggerCardType type = SDLogger_GetCardType();
            printf("Initialisation OK, carte %s\r\n",
                   type == SDLOGGER_CARD_SDHC ? "SDHC/SDXC (adressage par bloc)" : "SDSC (adressage par octet)");
            analyse_card();
            printf("\r\nTest termine. Appuyer sur RESET pour recommencer.\r\n");
            while (1);
        }

        printf("Initialisation echouee : %s\r\n", status_name(SDLogger_GetLastInitStatus()));

        if (!bitbangDone)
        {
            bitbangDone = true;
            printf("\r\nTest CMD0 en bit-bang (sans le peripherique SPI) :\r\n");
            SDLogger_BitBangCMD0Test();
        }

        printf("Nouvel essai dans %u s...\r\n\r\n", RETRY_MS / 1000U);
        HAL_Delay(RETRY_MS);
    }
}

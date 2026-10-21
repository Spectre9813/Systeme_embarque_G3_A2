/**
 * @file sd_diskio.c
 * @brief Carte SD en mode SPI : fonctions disk_xxx() demandées par FatFs.
 *
 * L'initialisation reprend la séquence de sd_logger.c (CMD0, CMD8, ACMD41,
 * CMD58) sans ses messages de diagnostic, et ajoute la lecture et l'écriture
 * de blocs de 512 octets (CMD17, CMD24) qui manquaient au dépôt de base.
 */
#include "sd_diskio.h"
#include "board.h"
#include "board_pins.h"
#include "ff.h"
#include "diskio.h"
#include "stm32l4xx_hal.h"
#include <string.h>

extern SPI_HandleTypeDef SD_SPI_HANDLE;

#define CMD0    0U    /* reset */
#define CMD1    1U    /* init MMC / anciennes cartes */
#define CMD8    8U    /* tension (SD v2) */
#define CMD9    9U    /* lecture du CSD */
#define CMD16   16U   /* taille de bloc */
#define CMD17   17U   /* lecture d'un bloc */
#define CMD24   24U   /* écriture d'un bloc */
#define CMD55   55U   /* préfixe ACMD */
#define CMD58   58U   /* lecture de l'OCR */
#define ACMD41  41U   /* init SD */

#define TOKEN_START   0xFEU
#define INIT_TIMEOUT  1000U   /* ms */
#define READ_TIMEOUT  200U
#define WRITE_TIMEOUT 500U
#define SPI_TIMEOUT   100U

#define CT_SD1   0x01U        /* SD v1 */
#define CT_SD2   0x02U        /* SD v2 */
#define CT_BLOCK 0x04U        /* adressage par bloc (SDHC/SDXC) */

static volatile DSTATUS s_stat = STA_NOINIT;
static uint8_t s_cardType;

/* ---- SPI --------------------------------------------------------------- */

static uint8_t sd_xfer(uint8_t tx)
{
    uint8_t rx = 0xFF;
    HAL_SPI_TransmitReceive(&SD_SPI_HANDLE, &tx, &rx, 1, SPI_TIMEOUT);
    return rx;
}

static void sd_select(void)
{
    HAL_GPIO_WritePin(SD_CS_PORT, SD_CS_PIN, GPIO_PIN_RESET);
    sd_xfer(0xFF);
}

static void sd_deselect(void)
{
    HAL_GPIO_WritePin(SD_CS_PORT, SD_CS_PIN, GPIO_PIN_SET);
    sd_xfer(0xFF);                 /* la carte libère MISO au coup d'horloge suivant */
}

static bool sd_waitReady(uint32_t timeoutMs)
{
    uint32_t t0 = HAL_GetTick();
    do
    {
        if (sd_xfer(0xFF) == 0xFF)
            return true;
    } while (HAL_GetTick() - t0 < timeoutMs);
    return false;
}

/** @brief Envoie une commande ; la carte reste sélectionnée. @return réponse R1 */
static uint8_t sd_cmd(uint8_t cmd, uint32_t arg)
{
    uint8_t frame[6], r1 = 0xFF, i, crc = 0x01;

    if (cmd == ACMD41)
    {
        r1 = sd_cmd(CMD55, 0);
        if (r1 > 1U)
            return r1;
    }

    sd_deselect();
    sd_select();
    if (cmd != CMD0 && !sd_waitReady(WRITE_TIMEOUT))
        return 0xFF;

    if (cmd == CMD0) crc = 0x95;
    if (cmd == CMD8) crc = 0x87;
    frame[0] = (uint8_t)(0x40U | cmd);
    frame[1] = (uint8_t)(arg >> 24);
    frame[2] = (uint8_t)(arg >> 16);
    frame[3] = (uint8_t)(arg >> 8);
    frame[4] = (uint8_t)arg;
    frame[5] = crc;
    HAL_SPI_Transmit(&SD_SPI_HANDLE, frame, 6, SPI_TIMEOUT);

    for (i = 0; i < 10U; i++)
    {
        r1 = sd_xfer(0xFF);
        if (!(r1 & 0x80U))
            break;
    }
    return r1;
}

static bool sd_readData(uint8_t *buf, uint32_t len)
{
    uint32_t t0 = HAL_GetTick();
    uint8_t token;

    do
    {
        token = sd_xfer(0xFF);
    } while (token == 0xFF && HAL_GetTick() - t0 < READ_TIMEOUT);
    if (token != TOKEN_START)
        return false;

    memset(buf, 0xFF, len);        /* MOSI doit rester à 1 pendant la lecture */
    if (HAL_SPI_TransmitReceive(&SD_SPI_HANDLE, buf, buf, (uint16_t)len, SPI_TIMEOUT) != HAL_OK)
        return false;
    sd_xfer(0xFF);                 /* CRC ignoré */
    sd_xfer(0xFF);
    return true;
}

static bool sd_writeBlock(const uint8_t *buf)
{
    uint8_t resp;

    if (!sd_waitReady(WRITE_TIMEOUT))
        return false;
    sd_xfer(TOKEN_START);
    if (HAL_SPI_Transmit(&SD_SPI_HANDLE, (uint8_t *)buf, 512, SPI_TIMEOUT) != HAL_OK)
        return false;
    sd_xfer(0xFF);                 /* CRC factice */
    sd_xfer(0xFF);
    resp = sd_xfer(0xFF);
    if ((resp & 0x1FU) != 0x05U)   /* données acceptées */
        return false;
    return sd_waitReady(WRITE_TIMEOUT);   /* fin de la programmation interne */
}

/* ---- Interface FatFs --------------------------------------------------- */

void sd_diskio_release(void)
{
    s_stat = STA_NOINIT;
    s_cardType = 0;
    HAL_GPIO_WritePin(SD_CS_PORT, SD_CS_PIN, GPIO_PIN_SET);
}

DSTATUS disk_status(BYTE pdrv)
{
    return (pdrv == 0U) ? s_stat : STA_NOINIT;
}

DSTATUS disk_initialize(BYTE pdrv)
{
    uint8_t i, ocr[4], type = 0;
    uint32_t t0;

    if (pdrv != 0U)
        return STA_NOINIT;

    board_sdSpiFast(false);
    sd_deselect();
    for (i = 0; i < 10U; i++)      /* au moins 74 coups d'horloge, CS à 1 */
        sd_xfer(0xFF);

    if (sd_cmd(CMD0, 0) == 1U)
    {
        t0 = HAL_GetTick();
        if (sd_cmd(CMD8, 0x1AA) == 1U)                      /* SD v2 */
        {
            for (i = 0; i < 4U; i++)
                ocr[i] = sd_xfer(0xFF);
            if (ocr[2] == 0x01U && ocr[3] == 0xAAU)
            {
                while (HAL_GetTick() - t0 < INIT_TIMEOUT && sd_cmd(ACMD41, 1UL << 30) != 0U) { }
                if (HAL_GetTick() - t0 < INIT_TIMEOUT && sd_cmd(CMD58, 0) == 0U)
                {
                    for (i = 0; i < 4U; i++)
                        ocr[i] = sd_xfer(0xFF);
                    type = (ocr[0] & 0x40U) ? (CT_SD2 | CT_BLOCK) : CT_SD2;
                }
            }
        }
        else                                                 /* SD v1 ou MMC */
        {
            uint8_t initCmd = (sd_cmd(ACMD41, 0) <= 1U) ? ACMD41 : CMD1;
            while (HAL_GetTick() - t0 < INIT_TIMEOUT && sd_cmd(initCmd, 0) != 0U) { }
            if (HAL_GetTick() - t0 < INIT_TIMEOUT && sd_cmd(CMD16, 512) == 0U)
                type = CT_SD1;
        }
    }
    sd_deselect();

    s_cardType = type;
    if (type != 0U)
    {
        s_stat &= (DSTATUS)~STA_NOINIT;
        board_sdSpiFast(true);
    }
    else
        s_stat = STA_NOINIT;
    return s_stat;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    DRESULT res = RES_OK;

    if (pdrv != 0U || count == 0U)
        return RES_PARERR;
    if (s_stat & STA_NOINIT)
        return RES_NOTRDY;
    if (!(s_cardType & CT_BLOCK))
        sector *= 512U;            /* SDSC : adresse en octets */

    while (count-- > 0U && res == RES_OK)
    {
        if (sd_cmd(CMD17, (uint32_t)sector) != 0U || !sd_readData(buff, 512))
            res = RES_ERROR;
        buff += 512;
        sector += (s_cardType & CT_BLOCK) ? 1U : 512U;
    }
    sd_deselect();
    return res;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    DRESULT res = RES_OK;

    if (pdrv != 0U || count == 0U)
        return RES_PARERR;
    if (s_stat & STA_NOINIT)
        return RES_NOTRDY;
    if (!(s_cardType & CT_BLOCK))
        sector *= 512U;

    while (count-- > 0U && res == RES_OK)
    {
        if (sd_cmd(CMD24, (uint32_t)sector) != 0U || !sd_writeBlock(buff))
            res = RES_ERROR;
        buff += 512;
        sector += (s_cardType & CT_BLOCK) ? 1U : 512U;
    }
    sd_deselect();
    return res;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    DRESULT res = RES_ERROR;
    uint8_t csd[16];

    if (pdrv != 0U)
        return RES_PARERR;
    if (s_stat & STA_NOINIT)
        return RES_NOTRDY;

    switch (cmd)
    {
    case CTRL_SYNC:                       /* chaque écriture attend déjà la fin */
        sd_select();
        res = sd_waitReady(WRITE_TIMEOUT) ? RES_OK : RES_ERROR;
        break;

    case GET_SECTOR_COUNT:                /* capacité, lue dans le CSD */
        if (sd_cmd(CMD9, 0) == 0U && sd_readData(csd, 16))
        {
            if ((csd[0] >> 6) == 1U)      /* CSD v2 (SDHC/SDXC) */
            {
                uint32_t csize = ((uint32_t)(csd[7] & 0x3FU) << 16) | ((uint32_t)csd[8] << 8) | csd[9];
                *(LBA_t *)buff = (LBA_t)((csize + 1U) << 10);
            }
            else                          /* CSD v1 */
            {
                uint32_t n = (uint32_t)(csd[5] & 15U) + ((csd[10] & 128U) >> 7) + ((csd[9] & 3U) << 1) + 2U;
                uint32_t csize = (uint32_t)(csd[8] >> 6) + ((uint32_t)csd[7] << 2) + ((uint32_t)(csd[6] & 3U) << 10) + 1U;
                *(LBA_t *)buff = (LBA_t)(csize << (n - 9U));
            }
            res = RES_OK;
        }
        break;

    case GET_BLOCK_SIZE:
        *(DWORD *)buff = 1;
        res = RES_OK;
        break;

    default:
        res = RES_PARERR;
        break;
    }
    sd_deselect();
    return res;
}

/**
 * @file sd_diskio.h
 * @brief Carte SD en mode SPI, branchée sur FatFs (diskio.h).
 */
#ifndef SD_DISKIO_H
#define SD_DISKIO_H

#include <stdbool.h>

/** @brief Oublie la carte : la prochaine opération la réinitialisera. */
void sd_diskio_release(void);

#endif

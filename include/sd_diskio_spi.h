#ifndef SD_DISKIO_SPI_H
#define SD_DISKIO_SPI_H

/**
 * @brief Réinitialise l'état logique du lecteur SPI FatFs (STA_NOINIT)
 *        lors d'une fermeture propre (mode MAINTENANCE) ou d'une perte de carte SD,
 *        afin de forcer une réinitialisation matérielle complète au prochain montage.
 */
void SD_DiskIO_Invalidate(void);

#endif /* SD_DISKIO_SPI_H */

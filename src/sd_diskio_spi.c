#include "sd_diskio_spi.h"
#include "diskio.h"
#include "board_pins.h"
#include "sd_logger.h"
#include "capteur_manager.h"
#include "stm32l4xx_hal.h"
#include <string.h>

#define SD_CMD17_READ_SINGLE_BLOCK   17U
#define SD_CMD24_WRITE_SINGLE_BLOCK  24U

#define SD_TOKEN_START_BLOCK         0xFEU
#define SD_DATA_RESP_MASK            0x1FU
#define SD_DATA_RESP_ACCEPTED        0x05U

#define SD_READ_TIMEOUT_MS           200U
#define SD_WRITE_TIMEOUT_MS          500U
#define SD_READY_TIMEOUT_MS          500U

static volatile DSTATUS s_disk_stat = STA_NOINIT;

static const uint8_t s_ff_chunk[64] = {
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF, 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF, 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF, 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF, 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF
};

static uint8_t spi_xfer_byte(uint8_t tx)
{
    uint8_t rx = 0xFFU;
    (void)HAL_SPI_TransmitReceive(&SD_SPI_HANDLE, &tx, &rx, 1U, 100U);
    return rx;
}

static void sd_cs_select(void)
{
    HAL_GPIO_WritePin(SD_CS_PORT, SD_CS_PIN, GPIO_PIN_RESET);
}

static void sd_cs_deselect(void)
{
    HAL_GPIO_WritePin(SD_CS_PORT, SD_CS_PIN, GPIO_PIN_SET);
    (void)spi_xfer_byte(0xFFU);
}

static bool sd_wait_ready(uint32_t timeout_ms)
{
    uint32_t t0 = HAL_GetTick();
    while ((HAL_GetTick() - t0) < timeout_ms)
    {
        if (spi_xfer_byte(0xFFU) == 0xFFU)
        {
            return true;
        }
    }
    return false;
}

static uint8_t sd_crc7(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0U;
    for (uint8_t i = 0U; i < len; ++i)
    {
        uint8_t d = data[i];
        for (uint8_t b = 0U; b < 8U; ++b)
        {
            crc <<= 1;
            if (((d ^ crc) & 0x80U) != 0U)
            {
                crc ^= 0x09U;
            }
            d <<= 1;
        }
    }
    return (uint8_t)((crc << 1) | 0x01U);
}

static uint16_t sd_crc16_ccitt(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0U;
    for (uint16_t i = 0U; i < len; ++i)
    {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);
        for (uint8_t b = 0U; b < 8U; ++b)
        {
            if ((crc & 0x8000U) != 0U)
            {
                crc = (uint16_t)((crc << 1) ^ 0x1021U);
            }
            else
            {
                crc <<= 1;
            }
        }
    }
    return crc;
}

static uint8_t sd_send_cmd_selected(uint8_t cmd, uint32_t arg)
{
    uint8_t frame[6];
    uint8_t r1 = 0xFFU;

    sd_cs_deselect();
    sd_cs_select();

    if (!sd_wait_ready(SD_READ_TIMEOUT_MS))
    {
        sd_cs_deselect();
        return 0xFFU;
    }

    frame[0] = (uint8_t)(0x40U | cmd);
    frame[1] = (uint8_t)(arg >> 24);
    frame[2] = (uint8_t)(arg >> 16);
    frame[3] = (uint8_t)(arg >> 8);
    frame[4] = (uint8_t)(arg);
    frame[5] = sd_crc7(frame, 5U);

    (void)HAL_SPI_Transmit(&SD_SPI_HANDLE, frame, 6U, 100U);

    for (uint8_t i = 0U; i < 10U; ++i)
    {
        r1 = spi_xfer_byte(0xFFU);
        if ((r1 & 0x80U) == 0U)
        {
            return r1;
        }
    }
    return r1;
}

static uint32_t sector_to_addr(DWORD sector)
{
    if (SDLogger_GetCardType() == SDLOGGER_CARD_SDHC)
    {
        return (uint32_t)sector;
    }
    return (uint32_t)(sector * 512UL);
}

static bool sd_read_block(DWORD sector, BYTE *buff)
{
    uint32_t addr = sector_to_addr(sector);
    uint8_t r1 = sd_send_cmd_selected(SD_CMD17_READ_SINGLE_BLOCK, addr);
    if (r1 != 0x00U)
    {
        sd_cs_deselect();
        return false;
    }

    uint32_t t0 = HAL_GetTick();
    uint8_t token = 0xFFU;
    while ((HAL_GetTick() - t0) < SD_READ_TIMEOUT_MS)
    {
        token = spi_xfer_byte(0xFFU);
        if (token != 0xFFU)
        {
            break;
        }
    }

    if (token != SD_TOKEN_START_BLOCK)
    {
        sd_cs_deselect();
        return false;
    }

    for (uint16_t offset = 0U; offset < 512U; offset += 64U)
    {
        if (HAL_SPI_TransmitReceive(&SD_SPI_HANDLE,
                                    (uint8_t *)s_ff_chunk,
                                    &buff[offset],
                                    64U,
                                    100U) != HAL_OK)
        {
            sd_cs_deselect();
            return false;
        }
    }

    /* Discard 2-byte CRC */
    (void)spi_xfer_byte(0xFFU);
    (void)spi_xfer_byte(0xFFU);
    sd_cs_deselect();
    return true;
}

static bool sd_write_block(DWORD sector, const BYTE *buff)
{
    uint32_t addr = sector_to_addr(sector);
    uint8_t r1 = sd_send_cmd_selected(SD_CMD24_WRITE_SINGLE_BLOCK, addr);
    if (r1 != 0x00U)
    {
        sd_cs_deselect();
        return false;
    }

    (void)spi_xfer_byte(0xFFU);
    (void)spi_xfer_byte(SD_TOKEN_START_BLOCK);

    if (HAL_SPI_Transmit(&SD_SPI_HANDLE, (uint8_t *)buff, 512U, 200U) != HAL_OK)
    {
        sd_cs_deselect();
        return false;
    }

    uint16_t crc = sd_crc16_ccitt(buff, 512U);
    (void)spi_xfer_byte((uint8_t)(crc >> 8));
    (void)spi_xfer_byte((uint8_t)(crc & 0xFFU));

    uint8_t resp = spi_xfer_byte(0xFFU);
    if ((resp & SD_DATA_RESP_MASK) != SD_DATA_RESP_ACCEPTED)
    {
        sd_cs_deselect();
        return false;
    }

    bool ok = sd_wait_ready(SD_WRITE_TIMEOUT_MS);
    sd_cs_deselect();
    return ok;
}

void SD_DiskIO_Invalidate(void)
{
    s_disk_stat = STA_NOINIT;
    sd_cs_deselect();
}

/* =========================================================================
 * Interface standard ChaN FatFs (diskio.h)
 * ========================================================================= */

DSTATUS disk_initialize(BYTE pdrv)
{
    if (pdrv != 0U)
    {
        return STA_NOINIT;
    }

    if (SDLogger_Init())
    {
        s_disk_stat = 0U;
    }
    else
    {
        s_disk_stat = STA_NOINIT;
    }
    return s_disk_stat;
}

DSTATUS disk_status(BYTE pdrv)
{
    if (pdrv != 0U)
    {
        return STA_NOINIT;
    }
    return s_disk_stat;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count)
{
    if (pdrv != 0U || buff == NULL || count == 0U)
    {
        return RES_PARERR;
    }
    if ((s_disk_stat & STA_NOINIT) != 0U)
    {
        return RES_NOTRDY;
    }

    for (UINT i = 0U; i < count; ++i)
    {
        if (!sd_read_block(sector + i, buff + (i * 512U)))
        {
            s_disk_stat = STA_NOINIT;
            return RES_ERROR;
        }
    }
    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count)
{
    if (pdrv != 0U || buff == NULL || count == 0U)
    {
        return RES_PARERR;
    }
    if ((s_disk_stat & STA_NOINIT) != 0U)
    {
        return RES_NOTRDY;
    }

    for (UINT i = 0U; i < count; ++i)
    {
        if (!sd_write_block(sector + i, buff + (i * 512U)))
        {
            s_disk_stat = STA_NOINIT;
            return RES_ERROR;
        }
    }
    return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    if (pdrv != 0U)
    {
        return RES_PARERR;
    }
    if ((s_disk_stat & STA_NOINIT) != 0U)
    {
        return RES_NOTRDY;
    }

    switch (cmd)
    {
    case CTRL_SYNC:
        sd_cs_select();
        if (sd_wait_ready(SD_READY_TIMEOUT_MS))
        {
            sd_cs_deselect();
            return RES_OK;
        }
        sd_cs_deselect();
        return RES_ERROR;

    case GET_SECTOR_SIZE:
        if (buff == NULL) return RES_PARERR;
        *(WORD *)buff = 512U;
        return RES_OK;

    case GET_BLOCK_SIZE:
        if (buff == NULL) return RES_PARERR;
        *(DWORD *)buff = 1UL;
        return RES_OK;

    default:
        return RES_PARERR;
    }
}

DWORD get_fattime(void)
{
    const Mesure *last = CapteurManager_GetLast();
    if (last != NULL &&
        last->timestamp.year >= 2000U &&
        last->timestamp.month >= 1U && last->timestamp.month <= 12U &&
        last->timestamp.day >= 1U && last->timestamp.day <= 31U)
    {
        return ((DWORD)(last->timestamp.year - 1980U) << 25)
             | ((DWORD)last->timestamp.month << 21)
             | ((DWORD)last->timestamp.day << 16)
             | ((DWORD)last->timestamp.hours << 11)
             | ((DWORD)last->timestamp.minutes << 5)
             | ((DWORD)(last->timestamp.seconds / 2U));
    }

    /* Horodatage par défaut : 2026-01-01 00:00:00 */
    return ((DWORD)(2026U - 1980U) << 25)
         | ((DWORD)1U << 21)
         | ((DWORD)1U << 16);
}

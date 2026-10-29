#include "sd_logger.h"
#include "board_pins.h"
#include "setup.h"
#include "stm32l4xx_hal.h"
#include <stdio.h>

#define SD_CMD0   0
#define SD_CMD1   1
#define SD_CMD8   8
#define SD_CMD16  16
#define SD_CMD55  55
#define SD_CMD58  58
#define SD_CMD59  59
#define SD_ACMD41 41

#define SD_R1_IDLE_STATE      0x01U
#define SD_R1_ILLEGAL_COMMAND 0x04U

/* P2-2 : Timeouts d'initialisation adaptés au standard SD SPI */
#define SD_INIT_TIMEOUT_MS    1500U
#define SD_WAIT_READY_MS      500U
#define SD_SPI_XFER_TIMEOUT   100U
#define SD_POWERUP_DELAY_MS   50U

typedef struct
{
    GPIO_TypeDef *port;
    uint16_t pin;
    const char *label;
} SDChipSelect;

static SDLoggerCardType g_card_type = SDLOGGER_CARD_UNKNOWN;
static SDLoggerInitStatus g_init_status = SDLOGGER_INIT_ERR_CMD0;
static uint8_t g_active_cs_index = 0;

static const SDChipSelect g_cs_candidates[] = {
    {SD_CS_PORT, SD_CS_PIN,  "D4/PB5"},
    {GPIOB,      GPIO_PIN_6, "D10/PB6"}
};

#define SD_CS_CANDIDATE_COUNT ((uint8_t)(sizeof(g_cs_candidates) / sizeof(g_cs_candidates[0])))

static uint8_t SD_SPI_Transfer(uint8_t tx)
{
    uint8_t rx = 0xFF;
    (void)HAL_SPI_TransmitReceive(&SD_SPI_HANDLE, &tx, &rx, 1, SD_SPI_XFER_TIMEOUT);
    return rx;
}

static void SD_Deselect(void)
{
    HAL_GPIO_WritePin(g_cs_candidates[g_active_cs_index].port,
                      g_cs_candidates[g_active_cs_index].pin,
                      GPIO_PIN_SET);
    (void)SD_SPI_Transfer(0xFF);
}

static void SD_Select(void)
{
    HAL_GPIO_WritePin(g_cs_candidates[g_active_cs_index].port,
                      g_cs_candidates[g_active_cs_index].pin,
                      GPIO_PIN_RESET);
}

static void SD_SetActiveChipSelect(uint8_t index)
{
    uint8_t i;

    g_active_cs_index = index;
    for (i = 0; i < SD_CS_CANDIDATE_COUNT; ++i)
    {
        HAL_GPIO_WritePin(g_cs_candidates[i].port, g_cs_candidates[i].pin, GPIO_PIN_SET);
    }
}

static bool SD_WaitReady(uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();

    while ((HAL_GetTick() - start) < timeout_ms)
    {
        if (SD_SPI_Transfer(0xFF) == 0xFF)
        {
            return true;
        }
    }

    return false;
}

/**
 * @brief Calcul CRC-7 (polynôme G(x) = x^7 + x^3 + 1 -> 0x09) + bit de fin à 1
 *        Utilisé pour sécuriser les commandes SPI SD lorsque CMD59 (CRC_ON_OFF) est utilisé (P2-2).
 */
static uint8_t SD_ComputeCRC7(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0;
    uint8_t i, bit;

    for (i = 0; i < len; ++i)
    {
        uint8_t d = data[i];
        for (bit = 0; bit < 8U; ++bit)
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

static uint8_t SD_SendCommand(uint8_t cmd, uint32_t arg, uint8_t crc)
{
    uint8_t response = 0xFF;
    uint8_t frame[6];
    uint8_t i;

    SD_Deselect();
    SD_Select();

    if (cmd != SD_CMD0)
    {
        if (!SD_WaitReady(SD_WAIT_READY_MS))
        {
            SD_Deselect();
            return 0xFF;
        }
    }

    frame[0] = (uint8_t)(0x40U | cmd);
    frame[1] = (uint8_t)(arg >> 24);
    frame[2] = (uint8_t)(arg >> 16);
    frame[3] = (uint8_t)(arg >> 8);
    frame[4] = (uint8_t)(arg);
    frame[5] = (crc != 0x01U) ? crc : SD_ComputeCRC7(frame, 5U);

    for (i = 0; i < 6; ++i)
    {
        (void)SD_SPI_Transfer(frame[i]);
    }

    for (i = 0; i < 10; ++i)
    {
        response = SD_SPI_Transfer(0xFF);
        if ((response & 0x80U) == 0U)
        {
            return response;
        }
    }

    return response;
}

/**
 * @brief Vérifie si la broche MISO (PA6 / D12) est physiquement reliée au lecteur SD
 *        ou si elle est flottante (en l'air).
 */
static void SD_DiagnosePhysicalWiring(void)
{
    GPIO_InitTypeDef gpio = {0};

    /* Test avec pull-down interne (40 kOhm) sur PA6 (D12 / MISO) */
    gpio.Pin       = GPIO_PIN_6;
    gpio.Mode      = GPIO_MODE_INPUT;
    gpio.Pull      = GPIO_PULLDOWN;
    gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(GPIOA, &gpio);
    HAL_Delay(2U);
    int miso_pd = (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_6) == GPIO_PIN_SET) ? 1 : 0;

    /* Restauration en AF5 SPI1_MISO avec pull-up interne */
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_PULLUP;
    gpio.Alternate = GPIO_AF5_SPI1;
    HAL_GPIO_Init(GPIOA, &gpio);
    HAL_Delay(1U);
    int miso_pu = (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_6) == GPIO_PIN_SET) ? 1 : 0;

    printf("[SD diag] Test ligne MISO (D12/PA6): PullUp=%d, PullDown=%d\r\n", miso_pu, miso_pd);
    if (miso_pu == 1 && miso_pd == 0)
    {
        printf("[SD ALERTE MATERIELLE] La broche D12 (MISO/PA6) est FLOTTANTE (non reliee au SD Shield) !\r\n");
        printf("[SD ALERTE MATERIELLE] -> Sur Nucleo-L476RG, il n'y a pas de connecteur ICSP 6 broches.\r\n");
        printf("[SD ALERTE MATERIELLE] -> Pontez avec 3 fils Dupont : D11->ICSP_MOSI(pin4), D12->ICSP_MISO(pin1), D13->ICSP_SCK(pin3).\r\n");
    }
}

bool SDLogger_Init(void)
{
    uint8_t cs_index;
    uint8_t i;
    uint8_t r1;
    uint8_t r7[4];
    uint8_t ocr[4];
    uint32_t start;
    uint8_t cmd0_try;
    uint8_t idle_samples[8];
    uint8_t cmd0_samples[8];
    uint8_t sample_count;

    g_card_type = SDLOGGER_CARD_UNKNOWN;
    g_init_status = SDLOGGER_INIT_ERR_CMD0;

    SD_DiagnosePhysicalWiring();

    /* P2-2 : Basse fréquence SPI pendant l'initialisation (<= 400 kHz : 80 MHz / 256 = 312.5 kHz) */
    (void)BSP_SPI1_SetPrescaler(SPI_BAUDRATEPRESCALER_256);

    for (cs_index = 0; cs_index < SD_CS_CANDIDATE_COUNT; ++cs_index)
    {
        SD_SetActiveChipSelect(cs_index);
        SD_Deselect();

        printf("[SD diag] try CS %s (SPI init <= 400 kHz)\r\n", g_cs_candidates[cs_index].label);

        HAL_Delay(SD_POWERUP_DELAY_MS);

        /* Send at least 74 clocks with CS high as mandated by SD SPI mode. */
        for (i = 0; i < 10; ++i)
        {
            (void)SD_SPI_Transfer(0xFF);
        }

        for (i = 0; i < 8; ++i)
        {
            idle_samples[i] = SD_SPI_Transfer(0xFF);
        }
        printf("[SD diag] MISO idle bytes: %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
               idle_samples[0], idle_samples[1], idle_samples[2], idle_samples[3],
               idle_samples[4], idle_samples[5], idle_samples[6], idle_samples[7]);

        r1 = 0xFFU;
        sample_count = 0U;
        for (cmd0_try = 0; cmd0_try < 32U; ++cmd0_try)
        {
            r1 = SD_SendCommand(SD_CMD0, 0x00000000UL, 0x95U);
            SD_Deselect();

            if (sample_count < 8U)
            {
                cmd0_samples[sample_count] = r1;
                ++sample_count;
            }

            if (r1 == SD_R1_IDLE_STATE)
            {
                break;
            }

            HAL_Delay(2U);
        }

        if (sample_count > 0U)
        {
            printf("[SD diag] CMD0 R1 first bytes:");
            for (i = 0; i < sample_count; ++i)
            {
                printf(" %02X", cmd0_samples[i]);
            }
            printf("\r\n");
        }

        if (r1 != SD_R1_IDLE_STATE)
        {
            g_init_status = SDLOGGER_INIT_ERR_CMD0;
            printf("[SD diag] CMD0 failed on %s\r\n", g_cs_candidates[cs_index].label);
            continue;
        }

        /* P2-2 : CMD59 (CRC_ON_OFF) - Activation de la vérification CRC en mode SPI */
        r1 = SD_SendCommand(SD_CMD59, 0x00000001UL, 0x01U);
        SD_Deselect();
        if ((r1 != SD_R1_IDLE_STATE) && ((r1 & SD_R1_ILLEGAL_COMMAND) == 0U))
        {
            g_init_status = SDLOGGER_INIT_ERR_CMD59;
            continue;
        }

        r1 = SD_SendCommand(SD_CMD8, 0x000001AAUL, 0x87U);
        if (r1 == SD_R1_IDLE_STATE)
        {
            for (i = 0; i < 4; ++i)
            {
                r7[i] = SD_SPI_Transfer(0xFF);
            }
            SD_Deselect();

            if ((r7[2] != 0x01U) || (r7[3] != 0xAAU))
            {
                g_init_status = SDLOGGER_INIT_ERR_CMD8_PATTERN;
                continue;
            }

            start = HAL_GetTick();
            do
            {
                r1 = SD_SendCommand(SD_CMD55, 0x00000000UL, 0x01U);
                SD_Deselect();
                if (r1 > 0x01U)
                {
                    g_init_status = SDLOGGER_INIT_ERR_ACMD41_TIMEOUT;
                    break;
                }

                r1 = SD_SendCommand(SD_ACMD41, 0x40000000UL, 0x01U);
                SD_Deselect();
                if (r1 == 0x00U)
                {
                    break;
                }
            }
            while ((HAL_GetTick() - start) < SD_INIT_TIMEOUT_MS);

            if (r1 != 0x00U)
            {
                g_init_status = SDLOGGER_INIT_ERR_ACMD41_TIMEOUT;
                continue;
            }

            r1 = SD_SendCommand(SD_CMD58, 0x00000000UL, 0x01U);
            if (r1 != 0x00U)
            {
                SD_Deselect();
                g_init_status = SDLOGGER_INIT_ERR_CMD58;
                continue;
            }

            for (i = 0; i < 4; ++i)
            {
                ocr[i] = SD_SPI_Transfer(0xFF);
            }
            SD_Deselect();

            g_card_type = ((ocr[0] & 0x40U) != 0U) ? SDLOGGER_CARD_SDHC : SDLOGGER_CARD_SDSC;

            /* P2-2 : CMD16 (SET_BLOCKLEN = 512 octets) requis pour les cartes SDSC */
            if (g_card_type == SDLOGGER_CARD_SDSC)
            {
                r1 = SD_SendCommand(SD_CMD16, 512UL, 0x01U);
                SD_Deselect();
                if (r1 != 0x00U)
                {
                    g_init_status = SDLOGGER_INIT_ERR_CMD16;
                    continue;
                }
            }

            /* P2-2 : Désactiver CMD59 pour les transferts de données bruts si souhaité,
             * puis basculer le bus SPI à haute vitesse (5 MHz @ 80 MHz = PRESCALER_16) */
            (void)SD_SendCommand(SD_CMD59, 0x00000000UL, 0x01U);
            SD_Deselect();
            (void)BSP_SPI1_SetPrescaler(SPI_BAUDRATEPRESCALER_16);

            g_init_status = SDLOGGER_INIT_OK;
            return true;
        }

        if ((r1 & SD_R1_ILLEGAL_COMMAND) == 0U)
        {
            SD_Deselect();
            g_init_status = SDLOGGER_INIT_ERR_CMD8_PATTERN;
            continue;
        }

        SD_Deselect();

        start = HAL_GetTick();
        do
        {
            r1 = SD_SendCommand(SD_CMD1, 0x00000000UL, 0x01U);
            SD_Deselect();
            if (r1 == 0x00U)
            {
                /* P2-2 : CMD16 (SET_BLOCKLEN = 512 octets) obligatoire sur carte SDSC v1 */
                r1 = SD_SendCommand(SD_CMD16, 512UL, 0x01U);
                SD_Deselect();
                if (r1 != 0x00U)
                {
                    g_init_status = SDLOGGER_INIT_ERR_CMD16;
                    break;
                }

                (void)SD_SendCommand(SD_CMD59, 0x00000000UL, 0x01U);
                SD_Deselect();
                (void)BSP_SPI1_SetPrescaler(SPI_BAUDRATEPRESCALER_16);

                g_card_type = SDLOGGER_CARD_SDSC;
                g_init_status = SDLOGGER_INIT_OK;
                return true;
            }
        }
        while ((HAL_GetTick() - start) < SD_INIT_TIMEOUT_MS);

        if (g_init_status != SDLOGGER_INIT_ERR_CMD16)
        {
            g_init_status = SDLOGGER_INIT_ERR_CMD1_TIMEOUT;
        }
    }

    return false;
}

SDLoggerCardType SDLogger_GetCardType(void)
{
    return g_card_type;
}

SDLoggerInitStatus SDLogger_GetLastInitStatus(void)
{
    return g_init_status;
}

const char *SDLogger_GetActiveCsLabel(void)
{
    return g_cs_candidates[g_active_cs_index].label;
}

bool SDLogger_WriteLine(const char *filename, const char *line)
{
    (void)filename;
    (void)line;

    /* File write support needs FatFs + diskio SPI (Phase 1 : P1-14). */
    return false;
}

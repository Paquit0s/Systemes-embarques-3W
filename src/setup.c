#include "setup.h"
#include "stm32l4xx_hal.h"
#include "board_pins.h"

/* =========================================================================
 * DÉFINITION UNIQUE DES HANDLES MATÉRIELS (Couche BSP - P0-7 & Phase 2)
 * ========================================================================= */
I2C_HandleTypeDef  hi2c1;
SPI_HandleTypeDef  hspi1;
UART_HandleTypeDef huart2;
UART_HandleTypeDef huart4;
ADC_HandleTypeDef  hadc1;
IWDG_HandleTypeDef hiwdg;

static void MX_I2C1_Init(void);
static void MX_SPI1_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_UART4_Init(void);
static void MX_ADC1_Init(void);

/**
 * @brief Redirection du flux standard stdout (printf) vers l'UART2 (ST-Link)
 */
int _write(int file, char *ptr, int len)
{
    (void)file; /* Évite le warning compilateur -Wunused-parameter */
    HAL_UART_Transmit(&huart2, (uint8_t *)ptr, (uint16_t)len, HAL_MAX_DELAY);
    return len;
}

/**
 * @brief Interruption du timer système (base de temps 1 ms pour HAL_GetTick)
 */
void SysTick_Handler(void)
{
    HAL_IncTick();
}

/**
 * @brief Interruption matérielle de l'USART2 (Console série non bloquante P2-4)
 */
void USART2_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart2);
}

/**
 * @brief Initialisation du bus I2C1 (PB8=SCL, PB9=SDA)
 * @note  P2-1 : TIMINGR recalculé pour PCLK1 = 80 MHz (Standard Mode 100 kHz : 0x10909CEC)
 */
static void MX_I2C1_Init(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_I2C1_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};

    gpio.Pin       = GPIO_PIN_8 | GPIO_PIN_9; /* PB8 SCL, PB9 SDA */
    gpio.Mode      = GPIO_MODE_AF_OD;
    gpio.Pull      = GPIO_PULLUP;
    gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(GPIOB, &gpio);

    hi2c1.Instance              = I2C1;
    hi2c1.Init.Timing           = 0x10909CEC; /* 100 kHz @ PCLK1 = 80 MHz (P2-1) */
    hi2c1.Init.OwnAddress1      = 0;
    hi2c1.Init.AddressingMode   = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode  = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2      = 0;
    hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
    hi2c1.Init.GeneralCallMode  = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode    = I2C_NOSTRETCH_DISABLE;

    HAL_I2C_Init(&hi2c1);
}

/**
 * @brief Initialisation du bus SPI1 (PA5=SCK, PA6=MISO, PA7=MOSI, PB5=CS)
 * @note  PA5 est strictement réservée à SPI1_SCK (ne jamais utiliser LD2).
 *        P2-1 / P2-2 : À 80 MHz, PRESCALER_256 donne 312,5 kHz (<= 400 kHz pour init SD).
 */
static void MX_SPI1_Init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_SPI1_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};

    gpio.Pin       = GPIO_PIN_5 | GPIO_PIN_7; /* PA5 SCK, PA7 MOSI */
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_NOPULL;
    gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF5_SPI1;
    HAL_GPIO_Init(GPIOA, &gpio);

    gpio.Pin       = GPIO_PIN_6; /* PA6 MISO (pull-up requis quand CS est haut) */
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_PULLUP;
    gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF5_SPI1;
    HAL_GPIO_Init(GPIOA, &gpio);

    __HAL_RCC_GPIOB_CLK_ENABLE();
    gpio.Pin   = SD_CS_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(SD_CS_PORT, &gpio);
    HAL_GPIO_WritePin(SD_CS_PORT, SD_CS_PIN, GPIO_PIN_SET);

    hspi1.Instance               = SPI1;
    hspi1.Init.Mode              = SPI_MODE_MASTER;
    hspi1.Init.Direction         = SPI_DIRECTION_2LINES;
    hspi1.Init.DataSize          = SPI_DATASIZE_8BIT;
    hspi1.Init.CLKPolarity       = SPI_POLARITY_LOW;
    hspi1.Init.CLKPhase          = SPI_PHASE_1EDGE;
    hspi1.Init.NSS               = SPI_NSS_SOFT;
    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_256; /* 312.5 kHz @ 80 MHz */
    hspi1.Init.FirstBit          = SPI_FIRSTBIT_MSB;
    hspi1.Init.TIMode            = SPI_TIMODE_DISABLE;
    hspi1.Init.CRCCalculation    = SPI_CRCCALCULATION_DISABLE;
    hspi1.Init.CRCPolynomial     = 7;

    HAL_SPI_Init(&hspi1);
}

/**
 * @brief Change dynamiquement le prescaler du bus SPI1 (P2-1 / P2-2)
 *        Utile pour passer de <= 400 kHz (init SD : PRESCALER_256 = 312.5 kHz)
 *        à 5 MHz (transfert SD : PRESCALER_16 = 5 MHz @ 80 MHz).
 */
bool BSP_SPI1_SetPrescaler(uint32_t BaudRatePrescaler)
{
    __HAL_SPI_DISABLE(&hspi1);
    hspi1.Init.BaudRatePrescaler = BaudRatePrescaler;
    if (HAL_SPI_Init(&hspi1) != HAL_OK)
    {
        return false;
    }
    return true;
}

/**
 * @brief Configuration de l'horloge système à 80 MHz via PLL (P2-1)
 *        Source : MSI 4 MHz -> PLLM = 1, PLLN = 40, PLLR = 2 => SYSCLK = 80 MHz.
 *        LSI (32 kHz) activé pour le Watchdog indépendant (IWDG - P2-5).
 */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1);

    osc.OscillatorType      = RCC_OSCILLATORTYPE_MSI | RCC_OSCILLATORTYPE_LSI;
    osc.MSIState            = RCC_MSI_ON;
    osc.MSICalibrationValue = RCC_MSICALIBRATION_DEFAULT;
    osc.MSIClockRange       = RCC_MSIRANGE_6; /* 4 MHz */
    osc.LSIState            = RCC_LSI_ON;     /* 32 kHz pour IWDG (P2-5) */
    osc.PLL.PLLState        = RCC_PLL_ON;
    osc.PLL.PLLSource       = RCC_PLLSOURCE_MSI;
    osc.PLL.PLLM            = 1;              /* 4 MHz / 1 = 4 MHz */
    osc.PLL.PLLN            = 40;             /* 4 MHz * 40 = 160 MHz VCO */
    osc.PLL.PLLP            = RCC_PLLP_DIV7;
    osc.PLL.PLLQ            = RCC_PLLQ_DIV2;
    osc.PLL.PLLR            = RCC_PLLR_DIV2;  /* 160 MHz / 2 = 80 MHz SYSCLK */

    if (HAL_RCC_OscConfig(&osc) != HAL_OK)
    {
        while (1) {}
    }

    clk.ClockType = RCC_CLOCKTYPE_HCLK   |
                    RCC_CLOCKTYPE_SYSCLK |
                    RCC_CLOCKTYPE_PCLK1  |
                    RCC_CLOCKTYPE_PCLK2;

    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider  = RCC_SYSCLK_DIV1; /* HCLK  = 80 MHz */
    clk.APB1CLKDivider = RCC_HCLK_DIV1;   /* PCLK1 = 80 MHz */
    clk.APB2CLKDivider = RCC_HCLK_DIV1;   /* PCLK2 = 80 MHz */

    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_4) != HAL_OK)
    {
        while (1) {}
    }
}

/**
 * @brief Callback MSP d'initialisation bas niveau des GPIO UART (USART2 & UART4)
 */
void HAL_UART_MspInit(UART_HandleTypeDef *uartHandle)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    if (uartHandle->Instance == USART2)
    {
        /* Console série ST-Link : PA2=TX, PA3=RX */
        __HAL_RCC_USART2_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();

        GPIO_InitStruct.Pin       = GPIO_PIN_2 | GPIO_PIN_3;
        GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull      = GPIO_NOPULL;
        GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
        GPIO_InitStruct.Alternate = GPIO_AF7_USART2;
        HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

        /* P2-4 : Activation de l'interruption USART2 pour réception IT + ring buffer */
        HAL_NVIC_SetPriority(USART2_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(USART2_IRQn);
    }
    else if (uartHandle->Instance == UART4)
    {
        /* GPS Air530Z sur Port Grove A0 : PA0=UART4_TX, PA1=UART4_RX (AF8) */
        __HAL_RCC_UART4_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();

        GPIO_InitStruct.Pin       = GPIO_PIN_0 | GPIO_PIN_1;
        GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull      = GPIO_PULLUP;
        GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
        GPIO_InitStruct.Alternate = GPIO_AF8_UART4;
        HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    }
}

/**
 * @brief Initialisation de l'USART2 (Console série @ 115200 bauds)
 */
static void MX_USART2_UART_Init(void)
{
    huart2.Instance                    = USART2;
    huart2.Init.BaudRate               = 115200;
    huart2.Init.WordLength             = UART_WORDLENGTH_8B;
    huart2.Init.StopBits               = UART_STOPBITS_1;
    huart2.Init.Parity                 = UART_PARITY_NONE;
    huart2.Init.Mode                   = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl              = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling           = UART_OVERSAMPLING_16;
    huart2.Init.OneBitSampling         = UART_ONE_BIT_SAMPLE_DISABLE;
    huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

    if (HAL_UART_Init(&huart2) != HAL_OK)
    {
        while (1) {}
    }
}

/**
 * @brief Initialisation de l'UART4 (GPS Air530Z @ 9600 bauds avec Pin Swap PA0/PA1)
 */
static void MX_UART4_Init(void)
{
    huart4.Instance                    = UART4;
    huart4.Init.BaudRate               = 9600;
    huart4.Init.WordLength             = UART_WORDLENGTH_8B;
    huart4.Init.StopBits               = UART_STOPBITS_1;
    huart4.Init.Parity                 = UART_PARITY_NONE;
    huart4.Init.Mode                   = UART_MODE_TX_RX;
    huart4.Init.HwFlowCtl              = UART_HWCONTROL_NONE;
    huart4.Init.OverSampling           = UART_OVERSAMPLING_16;
    huart4.Init.OneBitSampling         = UART_ONE_BIT_SAMPLE_DISABLE;
    huart4.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_SWAP_INIT;
    huart4.AdvancedInit.Swap           = UART_ADVFEATURE_SWAP_ENABLE;

    if (HAL_UART_Init(&huart4) != HAL_OK)
    {
        while (1) {}
    }
}

/**
 * @brief Initialisation de l'ADC1 (Capteur de lumière sur PA4 / Canal 9, 12 bits)
 * @note  À HCLK = 80 MHz, ADC_CLOCK_SYNC_PCLK_DIV4 donne 20 MHz (conforme spec STM32L4 <= 80 MHz)
 */
static void MX_ADC1_Init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_ADC_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin  = LIGHT_GPIO_PIN;
    gpio.Mode = GPIO_MODE_ANALOG_ADC_CONTROL;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(LIGHT_GPIO_PORT, &gpio);

    hadc1.Instance                   = ADC1;
    hadc1.Init.ClockPrescaler        = ADC_CLOCK_SYNC_PCLK_DIV4;
    hadc1.Init.Resolution            = ADC_RESOLUTION_12B;
    hadc1.Init.DataAlign             = ADC_DATAALIGN_RIGHT;
    hadc1.Init.ScanConvMode          = ADC_SCAN_DISABLE;
    hadc1.Init.EOCSelection          = ADC_EOC_SINGLE_CONV;
    hadc1.Init.LowPowerAutoWait      = DISABLE;
    hadc1.Init.ContinuousConvMode    = DISABLE;
    hadc1.Init.NbrOfConversion       = 1;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConv      = ADC_SOFTWARE_START;
    hadc1.Init.ExternalTrigConvEdge  = ADC_EXTERNALTRIGCONVEDGE_NONE;
    hadc1.Init.DMAContinuousRequests = DISABLE;
    hadc1.Init.Overrun               = ADC_OVR_DATA_PRESERVED;
    hadc1.Init.OversamplingMode      = DISABLE;

    if (HAL_ADC_Init(&hadc1) != HAL_OK)
    {
        while (1) {}
    }
}

/**
 * @brief Initialisation du Watchdog indépendant IWDG (P2-5)
 * @param timeout_ms Délai avant reset matériel (ex. 12000 ms).
 *        Basé sur LSI = 32 kHz et Prescaler = 256 (125 ticks/s, max ~32.7 s).
 */
bool BSP_Watchdog_Init(uint32_t timeout_ms)
{
    uint32_t reload = (timeout_ms * 125U) / 1000U;
    if (reload == 0U)
    {
        reload = 1U;
    }
    if (reload > 0x0FFFU)
    {
        reload = 0x0FFFU;
    }

    hiwdg.Instance       = IWDG;
    hiwdg.Init.Prescaler = IWDG_PRESCALER_256;
    hiwdg.Init.Window    = IWDG_WINDOW_DISABLE;
    hiwdg.Init.Reload    = reload;

    return (HAL_IWDG_Init(&hiwdg) == HAL_OK);
}

/**
 * @brief Rafraîchit le compteur du Watchdog IWDG (P2-5)
 */
void BSP_Watchdog_Refresh(void)
{
    if (hiwdg.Instance == IWDG)
    {
        (void)HAL_IWDG_Refresh(&hiwdg);
    }
}

/**
 * @brief Initialisation globale du BSP
 */
void Global_Init(void)
{
    HAL_Init();
    SystemClock_Config();

    MX_I2C1_Init();
    MX_SPI1_Init();
    MX_USART2_UART_Init();
    MX_UART4_Init();
    MX_ADC1_Init();
}

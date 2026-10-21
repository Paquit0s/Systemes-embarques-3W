#include <stdio.h>
#include <string.h>

#include "board_pins.h"
#include "cesi_types.h"
#include "setup.h"
#include "grove_rtc_ds1307.h"
#include "grove_gps_air530z.h"
#include "grove_light.h"
#include "sd_logger.h"

#define BUTTON_GPIO_PORT GPIOC
#define BUTTON_PIN GPIO_PIN_13
#define BUTTON_DEBOUNCE_MS 200U

static volatile uint8_t acquisition_requested;

static void GPIO_EXTI_Init(void);
static void acquisition(void);

void interruption(void)
{
    Global_Init();
    GPIO_EXTI_Init();

    if (!GroveRTC_Init())
    {
        printf("Erreur d'initialisation de l'horloge RTC.\r\n");
    }

    GroveGPS_Init();
    GroveLight_Init();

    printf("Initialisation de la carte SD...\r\n");
    if (!SDLogger_Init())
    {
        printf("Carte SD absente ou impossible à initialiser.\r\n");
        while (1)
        {
        }
    }

    printf("Carte SD initialisee. Appuyez sur le bouton pour lancer une acquisition.\r\n");

    while (1)
    {
        if (acquisition_requested != 0U)
        {
            __disable_irq();
            acquisition_requested = 0U;
            __enable_irq();

            acquisition();
        }
    }
}

void EXTI15_10_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(BUTTON_PIN);
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    static uint32_t last_press_tick;
    static uint8_t has_last_press;
    uint32_t now;

    if (GPIO_Pin != BUTTON_PIN)
    {
        return;
    }

    now = HAL_GetTick();
    if (has_last_press == 0U ||
        (uint32_t)(now - last_press_tick) >= BUTTON_DEBOUNCE_MS)
    {
        acquisition_requested = 1U;
        last_press_tick = now;
        has_last_press = 1U;
    }
}

static void acquisition(void)
{
    RTC_DateTime date_time = {0};
    GPS_Data gps_info = {0};
    uint16_t light_value;
    char light_text[16];
    char gps_text[sizeof(gps_info.raw_sentence)];
    char data_line[256];
    char log_filename[16];
    uint8_t gps_attempt;

    if (!GroveRTC_GetDateTime(&date_time))
    {
        printf("Erreur de lecture de l'horloge RTC.\r\n");
    }

    light_value = GroveLight_ReadRaw();
    if (light_value == 0xFFFFU)
    {
        printf("Erreur de lecture du capteur de luminosite.\r\n");
        strcpy(light_text, "NA");
    }
    else
    {
        snprintf(light_text, sizeof(light_text), "%u", (unsigned int)light_value);
    }

    strcpy(gps_text, "NA");
    for (gps_attempt = 0U; gps_attempt < 10U; gps_attempt++)
    {
        if (GroveGPS_ReadGGA(&gps_info, 1000U))
        {
            snprintf(gps_text, sizeof(gps_text), "%s", gps_info.raw_sentence);
            break;
        }
    }

    snprintf(data_line, sizeof(data_line),
             "%02u:%02u:%02u %02u/%02u/%04u ; %s ; %s",
             (unsigned int)date_time.hours,
             (unsigned int)date_time.minutes,
             (unsigned int)date_time.seconds,
             (unsigned int)date_time.day,
             (unsigned int)date_time.month,
             (unsigned int)date_time.year,
             light_text,
             gps_text);

    snprintf(log_filename, sizeof(log_filename), "%02u%02u%02u_0.LOG",
             (unsigned int)(date_time.year % 100U),
             (unsigned int)date_time.month,
             (unsigned int)date_time.day);

    if (!SDLogger_WriteLine(log_filename, data_line))
    {
        printf("Erreur lors de l'ecriture sur la carte SD.\r\n");
        return;
    }

    printf("%s\r\n", data_line);
}

static void GPIO_EXTI_Init(void)
{
    GPIO_InitTypeDef gpio_init = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();

    gpio_init.Pin = BUTTON_PIN;
    gpio_init.Mode = GPIO_MODE_IT_FALLING;
    gpio_init.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(BUTTON_GPIO_PORT, &gpio_init);

    HAL_NVIC_SetPriority(EXTI15_10_IRQn, 5U, 0U);
    HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
}

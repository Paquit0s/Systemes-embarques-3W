#include <stdio.h>
#include <string.h>

#include "board_pins.h"
#include "cesi_types.h"
#include "setup.h"
#include "interruption.h"
#include "grove_rtc_ds1307.h"
#include "grove_bme680.h"
#include "grove_gps_air530z.h"
#include "grove_light.h"
#include "grove_rgb.h"
#include "sd_logger.h"

static void acquisition(void);

int main(void)
{
    /* 1. Initialisation du BSP (Horloge, I2C1, SPI1, USART2, UART4, ADC1) */
    Global_Init();
    Interruption_Init();

    /* 2. Signalisation lumineuse via Grove RGB (PA8/PB10) - Jamais LD2/PA5 ! */
    GroveRGB_Init();

    if (!GroveRTC_Init())
    {
        printf("Erreur d'initialisation de l'horloge RTC.\r\n");
    }

    if (!GroveBME680_Init())
    {
        printf("Erreur d'initialisation du capteur BME680.\r\n");
    }

    GroveGPS_Init();
    GroveLight_Init();

    printf("Initialisation de la carte SD...\r\n");
    if (!SDLogger_Init())
    {
        printf("Carte SD absente ou impossible a initialiser.\r\n");
        GroveRGB_SetColor(255, 0, 0); /* Signalisation erreur SD sur LED RGB */
        while (1)
        {
        }
    }

    /* Mode STANDARD prêt : LED RGB Verte (0, 255, 0) */
    GroveRGB_SetColor(0, 255, 0);
    printf("Carte SD initialisee. Appuyez sur le bouton pour lancer une acquisition.\r\n");

    /* 3. Boucle principale coopérative (aucun HAL_Delay bloquant) */
    while (1)
    {
        if (Interruption_ConsumeButtonEvent())
        {
            acquisition();
        }
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
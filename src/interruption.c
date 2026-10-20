#include <stdio.h>
#include "board_pins.h" /*Permet de récupérer les périphériques de com*/
#include "string.h"

/**
 * Include des bibliothèques permettant de controler les modules :
 *  - Horloge temps réel (RTC)
 *  - Module GPS
 *  - Module lumière
 *  - Enregistreur Carte SD
 */



#include "grove_rtc_ds1307.h"
#include "grove_gps_air530z.h"
#include "grove_light.h"
#include "sd_logger.h"




int main(void)
{
    GroveRTC_GetDateTime date_time;

    uint16_t light_value = 0;

    char gps_data[100];
    char data_line[256];

    HAL_Init();
    SystemClock_Config();

    MX_GPIO_Init();
    MX_ADC1_Init();
    MX_I2C1_Init();
    MX_SPI1_Init();
    MX_USART1_UART_Init();
    MX_USART2_UART_Init();

    /** 
     * Initilisation des modules du projet
     */

    GroveRTC_Init(&hi2c1);
    GroveGPS_Init(&huart1);
    GroveLight_Init(&hadc1);

    printf("Initialisation de la carte SD...\r\n");

    if (SDLogger_Init(&hspi1, SD_CS_PORT, SD_CS_PIN) != HAL_OK)
    {
    printf("Carte  SD absente ou impossible à initialiser.\r\n");

    /**
     * Le programme reste bloqué si la carte SD ne peut pas être utilisée.
     */

    while(1)
    {}
    }

    printf("Carte SD initialisée.\r\n");

    while(1)
    {
        /**
         * Lecture de la date et de l'heure depuis l'horloge RTC.
         */
        if (GroveRTC_GetDateTime(&date_time) != HAL_OK)
        {
            printf("Erreur de lecture de l'horloge RTC.\r\n");
        }

        /**
         * Lecture de la valeur brute du capteur de luminosité.
         */
        if (GroveLight_ReadRaw(&light_value) != HAL_OK)
        {
            printf("Erreur de lecture du capteur de luminosité.\r\n");
            light_value = 0;
        }

        /**
         * Lecture des données envoyées par le GPS
         * 
         * Le programme lit les trames reçues jusqu'à trouver une trame GGA,
         * qui contient notamment la position géographique.
         */
        gps_data[0] = '\0';

        do {
        
            if (GroveGPS_ReadGGA(gps_data, sizeof(gps_data), 1000U) != HAL_OK) {
            
                strcpy(gps_data, "GPS indisponible");
                break;
            }
        } while (strncmp(gps_data, "$GPGGA", 6) != 0);

    
    
    
    /**
     * Création d'une ligne regroupant toutes les données
     * 
     * Exemple :
     * 14:35:12 01/01/2026 ; 1850 ; $GPGGA,....
     */

    snprintf(data_line, sizeof(data_line), "%02u:%02u:%02u %02u/%02u/%04u ; %u ; %s", date_time.hour,
    date_time.minute, date_time.second, date_time.day, date_time.month, date_time.year, light_value, gps_data);

    /**
     * Enregistrement de la ligne dans le fichier de mesures
     */

    if (SDLogger_WriteLine("datalog.txt", data_line) != HAL_OK)
    {
        printf("Erreur lors de l'écriture");
    } 
    else
    {
        /**
         * Affichage de la même ligne dans le terminal série
         */
        printf("%s\r\n", data_line);
    }

}
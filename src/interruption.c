#include <stdio.h>
#include "board_pins.h" /*Permet de récupérer les périphériques de com*/
#include "main.h"
#include <string.h>

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

// [CORRECTION] On doit définir ces variables ici car dans le fichier "board_pins.h" elles ont été définie en extern

I2C_HandleTypeDef hi2c1;
UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;
ADC_HandleTypeDef hadc1;
SPI_HandleTypeDef hspi1;



int main(void)
{

    // [CORRECTION] Remplacement du nom de la fonction par le vrai type de la type de la structure (dans grove_rtc_ds1307.h)
    // Initialisation à 0 pour éviter d'afficher des valeurs RAM aléatoires si la RTC ne répond pas
    RTC_DateTime date_time;

    uint16_t light_value = 0;
    char light_str[16]; // [CORRECTION] Chaîne intermédiaire pour pouvoir écrire "NA" en cas d'erreur capteur
    char log_filename[16]; // [CORRECTION] Buffer pour génerer le nom du fichier AAMMJJ_0.LOG

    char gps_data[100];
    char data_line[256];

    HAL_Init();
    SystemClock_Config();

    /**
     * A définir cette partie en dessous pour la suite
     */

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
     * (Note) il faudra faire clignoter la LED en rouge et en blanc selon le cahier des charges
     */

    while(1)
    {
    }
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
            // [CORRECTION] Selon le cahier des charges ont écrit "NA" si le capteur échoue et non 0
            strcpy(light_str, "NA");
        }

        /**
         * Lecture des données envoyées par le GPS
         * 
         * Le programme lit les trames reçues jusqu'à trouver une trame GGA,
         * qui contient notamment la position géographique.
         */
        gps_data[0] = '\0';
        uint8_t gps_retries = 0; // [CORRECTION] Sécurité pour éviter une boucle infinie

        do {
        
            if (GroveGPS_ReadGGA(gps_data, sizeof(gps_data), 1000U) != HAL_OK) {
            
                strcpy(gps_data, "GPS indisponible");
                break;
            }
            gps_retries++; // On incrémente pour ne pas avoir une boucle infinie

        // [CORRECTION] On vérifie "GPGGA" et "GNGGA" (très fréquent sur le Air530Z)
        // On fait bien attention de sortir de la boucle au bout de 10 essaie pour ne pas faire planter le programme
        } while (strncmp(gps_data, "$GPGGA", 6) != 0 &&
                 strncmp(gps_data, "$GNGGA", 6) != 0 &&
                 gps_retries < 10);
        
        if (gps_retries >=10)
        {
            strcpy(gps_data, "NA");
        }
    
    
    
    /**
     * Création d'une ligne regroupant toutes les données
     * 
     * Exemple :
     * 14:35:12 01/01/2026 ; 1850 ; $GPGGA,....
     */
    
     // [CORRECTION] Les champs de structure (second, minute, hour) s'appellent aux pluriels
    snprintf(data_line, sizeof(data_line), "%02u:%02u:%02u %02u/%02u/%04u ; %u ; %s", date_time.hours,
    date_time.minutes, date_time.seconds, date_time.day, date_time.month, date_time.year, light_value, gps_data);

    /**
     * Enregistrement de la ligne dans le fichier de mesures
     */
    
    // [CHANGEMENT] Génération du nom du fichier au format AAMMJJ_0.LOG
    snprintf(log_filename, sizeof(log_filename), "%02u%02u%02u_0.LOG",
             (uint8_t)(date_time.year % 100), date_time.month, date_time.day);

    if (SDLogger_WriteLine(log_filename, data_line) != HAL_OK)
    {
        printf("Erreur lors de l'écriture\r\n");
    } 
    else
    {
        /**
         * Affichage de la même ligne dans le terminal série
         */
        printf("%s\r\n", data_line);
    }

    // [CORRECTION] Pause entre deux mesures pour ne pas saturer la carte SD
    // Ici 5000 ms, à remplacer par (sys_cfg.log_interval * 60000) dans la version finale
    HAL_Delay(5000);

    }

} // [CORRECTION] Fermer le main()
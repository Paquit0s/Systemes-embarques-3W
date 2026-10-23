#ifndef BOARD_PINS_H
#define BOARD_PINS_H

#include "stm32l4xx_hal.h"

/**
 * Périphériques de communications :
 *     - Ces 5 structures permettent de piloté diréctemenet les phériphériques
 *       matériels nécessaire aux mode de fonctionnement
 * 
 *     - I2C :  Permet l'utilisation du bus I2C n°1, il permet de communiquer avec
 *              l'horloge RTC, les capteurs numériques (température de l'air, hygométrie, 
 *              pression atmoshpérique)
 * 
 *     - UART1/2 : Permettent d'accéder aux deux ports séries (UART)
 *                 "huart2" gère la console série pour saisir les commandes du mode configuration
 *                 ainsi qu'afficher les mesures en direct en mode maintenance.
 * 
 *                 "huart1" sert à recevoir les trames du module GPS
 * 
 *     - ADC : Donne accès au convertisseur analogique numérique (ADC), il sert à lire la
 *             tension analogique du capteur de luminosité (LUMIN) seuil : 0 à 1023
 * 
 *     - SPI : Donne accès au bus SPI n°1, permet de piloter le module carte SD pour écrire
 *             et archiver les fichiers de mesures
 */

extern I2C_HandleTypeDef hi2c1;
extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart2;
extern ADC_HandleTypeDef hadc1;
extern SPI_HandleTypeDef hspi1;


/*
 * Grove Chainable RGB LED :
 * - Port Grove D6 du shield
 * - Signal1=D6=PB10 → CIN (horloge P9813)
 * - Signal2=D7=PA8  → DIN (données P9813)
 */
#define RGB_DATA_PORT   GPIOA
#define RGB_DATA_PIN    GPIO_PIN_8

#define RGB_CLK_PORT    GPIOB
#define RGB_CLK_PIN     GPIO_PIN_10

/*
 * Grove Light Sensor v1.3
 * Port Grove A2 du shield (PA4 = ADC1_IN9).
 */
#define LIGHT_ADC_CHANNEL  ADC_CHANNEL_9
#define LIGHT_GPIO_PORT    GPIOA
#define LIGHT_GPIO_PIN     GPIO_PIN_4

/*
 * Grove Dual Button v1.0
 * À brancher sur un port digital Grove, par exemple D2.
 * À adapter selon le port choisi et le mapping réel.
 */
#define BUTTON1_PORT GPIOA
#define BUTTON1_PIN  GPIO_PIN_10

#define BUTTON2_PORT GPIOB
#define BUTTON2_PIN  GPIO_PIN_3

/*
 * Grove GPS Air530Z
 * Port Grove A0 du shield (PA0=UART4_TX, PA1=UART4_RX, AF8).
 * Seul UART libre sur un connecteur Grove unique (USART2=debug, USART1 split sur D2+D8).
 */

/*
 * Grove RTC v1.2 DS1307 + BME680
 * Tous deux sur I2C.
 */
#define GROVE_I2C_HANDLE hi2c1

/*
 * SD Card Shield Seeed v4.3
 * SPI Arduino : D11/D12/D13 et CS sur D4.
 */
#define SD_CS_PORT GPIOB
#define SD_CS_PIN  GPIO_PIN_5

#define SD_SPI_HANDLE hspi1


 

#endif
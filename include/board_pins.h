#ifndef BOARD_PINS_H
#define BOARD_PINS_H

#include "stm32l4xx_hal.h"

/**
 * Périphériques de communication (BSP) :
 *   Tous les handles matériels sont définis UNE SEULE FOIS dans src/setup.c
 *   et exposés ici en extern pour le reste de l'application et des pilotes.
 *
 *   - hi2c1  : Bus I2C1 (PB8=SCL, PB9=SDA) -> Horloge RTC DS1307 (0x68) + BME680 (0x76/0x77)
 *   - huart2 : Console série ST-Link (PA2=TX, PA3=RX @ 115200 bauds) -> Modes Config & Maintenance
 *   - huart4 : Réception GPS Air530Z (PA0=TX, PA1=RX @ 9600 bauds, pin swap actif)
 *   - hadc1  : Convertisseur ADC1 12 bits (PA4 = ADC1_IN9) -> Capteur de luminosité (0 à 4095)
 *   - hspi1  : Bus SPI1 (PA5=SCK, PA6=MISO, PA7=MOSI, PB5=CS) -> Carte SD
 *
 * ATTENTION MATÉRIELLE (P0-8) :
 *   La broche PA5 (D13) est utilisée par SPI1_SCK pour la carte SD.
 *   Il est STRICTEMENT INTERDIT d'utiliser la LED interne LD2 (câblée sur PA5).
 *   Toute signalisation lumineuse doit passer par la LED Grove RGB (PA8 / PB10).
 */

extern I2C_HandleTypeDef  hi2c1;
extern UART_HandleTypeDef huart2;
extern UART_HandleTypeDef huart4;
extern ADC_HandleTypeDef  hadc1;
extern SPI_HandleTypeDef  hspi1;

/* Alias de handles pour les pilotes de la couche lib/cesi_grove */
#define GROVE_I2C_HANDLE  hi2c1
#define GPS_UART_HANDLE   huart4
#define SD_SPI_HANDLE     hspi1
#define LIGHT_ADC_HANDLE  hadc1

/*
 * Grove Chainable RGB LED (P9813) :
 * - Port Grove D6 du shield
 * - Signal1 = D6 = PB10 -> CIN (horloge P9813)
 * - Signal2 = D7 = PA8  -> DIN (données P9813)
 */
#define RGB_DATA_PORT     GPIOA
#define RGB_DATA_PIN      GPIO_PIN_8

#define RGB_CLK_PORT      GPIOB
#define RGB_CLK_PIN       GPIO_PIN_10

/*
 * Grove Light Sensor v1.3 :
 * - Port Grove A2 du shield (PA4 = ADC1_IN9, résolution 12 bits : 0 à 4095)
 */
#define LIGHT_ADC_CHANNEL ADC_CHANNEL_9
#define LIGHT_GPIO_PORT   GPIOA
#define LIGHT_GPIO_PIN    GPIO_PIN_4

/*
 * Boutons :
 * - Bouton utilisateur Nucleo B1 sur PC13 (EXTI15_10)
 * - Grove Dual Button sur PA10 / PB3
 */
#define BUTTON_USER_PORT  GPIOC
#define BUTTON_USER_PIN   GPIO_PIN_13

#define BUTTON1_PORT      GPIOA
#define BUTTON1_PIN       GPIO_PIN_10

#define BUTTON2_PORT      GPIOB
#define BUTTON2_PIN       GPIO_PIN_3

/*
 * SD Card Shield Seeed v4.3 :
 * - SPI Arduino : D11 (PA7=MOSI), D12 (PA6=MISO), D13 (PA5=SCK)
 * - Chip Select (CS) sur D4 (PB5)
 */
#define SD_CS_PORT        GPIOB
#define SD_CS_PIN         GPIO_PIN_5

#endif /* BOARD_PINS_H */
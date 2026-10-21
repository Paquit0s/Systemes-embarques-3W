#include <stdio.h>
#include "setup.h"
#include "cesi_types.h"
#include "grove_rgb.h"
#include "grove_button.h"
#include "grove_light.h"
#include "grove_rtc_ds1307.h"
#include "grove_gps_air530z.h"
#include "grove_bme680.h"
#include "sd_logger.h"
#include "board_pins.h"

/**
 * LED utilisateur LD2 de la carte
 * PA5, active à l'état haut
 */

#define LED_GPIO_PORT GPIOA
#define LED_PIN GPIO_PIN_5

/**
 * Bouton utilisateur B1 de la carte
 * PC13, actif à l'état bas
 */

#define BUTTON_GPIO_PORT GPIOC
#define BUTTON_PIN GPIO_PIN_13
#define BUTTON_PRESSED GPIO_PIN_RESET

static void GPIO_Init(void);
static void acquisition(void);




int main(void)
{
    /* GPIO_PinState est le type HAL qui représente l'état logique d'une broche :
     * GPIO_PIN_RESET (bas) ou GPIO_PIN_SET (haut).
     */
    GPIO_PinState button_state;

    /**
     * Initialise les fonctions : 
     * - HAL_Init()
     * - SystemClock_Config()
     * - MX_I2C1_Init()
     * - MX_SPI1_Init()
     * - MX_USART2_UART_Init()
     */

    Global_Init(); // Voir dans le "setup.c"

    GPIO_Init();

    while (1)
    {
        /**
         * Lecture de l'état du bouton par scrutation
         * 
         * Cette lecture n'est réalisée qu'une seule fois avant le lancement
         * da la fonction acquisition()
         */

        button_state = HAL_GPIO_ReadPin(BUTTON_GPIO_PORT, BUTTON_PIN);

        if (button_state == BUTTON_PRESSED)
        {
            HAL_GPIO_WritePin(LED_GPIO_PORT, LED_PIN, GPIO_PIN_SET);
        }
        else
        {
            HAL_GPIO_WritePin(LED_GPIO_PORT, LED_PIN, GPIO_PIN_RESET);
        }

        /**
         * Simulation d'une acquisition longue
         * 
         * Pendant cette fonction l'état du bouton n'est plus consulté
         */
        acquisition();
    }
}

static void acquisition(void)
{
    uint32_t acquisition_duration;

    /**
     * Simulation d'une durée d'acquisition entre 
     * 3 et 4 secondes
     * Vérifier que ces timings correspondent bien au cahier des charges.
     */
    acquisition_duration = 3000U + (HAL_GetTick() % 4000U);

    /**
     * HAL_Delay() bloque la poursuite du programme principal
     */
    HAL_Delay(acquisition_duration);

}

static void GPIO_Init(void)
{
    GPIO_InitTypeDef gpio_init = {0};

    /**
     * Activation des horloges des ports GPIOA et GPIOC
     */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    /**
     * Configuration de la LED LD2 en sortie
     */
    gpio_init.Pin = LED_PIN;
    gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_LOW;

    HAL_GPIO_Init(LED_GPIO_PORT, &gpio_init);

    /**
     * Configuration duu bouton B1 en entrée simple
     * 
     * Aucune interruption n'est encore configurée
     */
    gpio_init.Pin = BUTTON_PIN;
    gpio_init.Mode = GPIO_MODE_INPUT;
    gpio_init.Pull = GPIO_NOPULL;

    HAL_GPIO_Init(BUTTON_GPIO_PORT, &gpio_init);

    /**
     * La LED est éteinte au démarrage
     */
    HAL_GPIO_WritePin(LED_GPIO_PORT, LED_PIN, GPIO_PIN_RESET);
}
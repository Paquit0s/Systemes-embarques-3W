#include "interruption.h"
#include "board_pins.h"

#define BUTTON_DEBOUNCE_MS 200U

static volatile uint8_t s_acquisition_requested = 0U;

void Interruption_Init(void)
{
    GPIO_InitTypeDef gpio_init = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();

    gpio_init.Pin  = BUTTON_USER_PIN;
    gpio_init.Mode = GPIO_MODE_IT_FALLING;
    gpio_init.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(BUTTON_USER_PORT, &gpio_init);

    HAL_NVIC_SetPriority(EXTI15_10_IRQn, 5U, 0U);
    HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
}

void EXTI15_10_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(BUTTON_USER_PIN);
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    static uint32_t last_press_tick = 0U;
    static uint8_t  has_last_press  = 0U;
    uint32_t now;

    if (GPIO_Pin != BUTTON_USER_PIN)
    {
        return;
    }

    now = HAL_GetTick();
    if (has_last_press == 0U ||
        (uint32_t)(now - last_press_tick) >= BUTTON_DEBOUNCE_MS)
    {
        s_acquisition_requested = 1U;
        last_press_tick         = now;
        has_last_press          = 1U;
    }
}

bool Interruption_ConsumeButtonEvent(void)
{
    bool event_detected = false;

    __disable_irq();
    if (s_acquisition_requested != 0U)
    {
        s_acquisition_requested = 0U;
        event_detected = true;
    }
    __enable_irq();

    return event_detected;
}
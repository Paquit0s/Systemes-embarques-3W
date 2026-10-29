#include "interruption.h"
#include "board_pins.h"
#include "stm32l4xx_hal.h"
#include <stdio.h>

/* --- Mapping matériel des boutons (Base Shield Grove + Nucleo-L476RG) ---
 *  - BTN_GREEN : PA10 (Port Grove D2), actif à l'état BAS (pull-up).
 *  - BTN_RED   :
 *      1. PB3 (Port Grove D3), actif à l'état BAS (pull-up).
 *      2. PB4 (Port Grove D5), actif à l'état BAS (pull-up).
 *      3. PA9 (Port Grove D8), actif à l'état BAS (pull-up).
 *      4. PC13 (B1 utilisateur Nucleo), actif à l'état BAS (pull-up).
 */
#define BTN_ENABLE_GROVE_RED    1

#define DEBOUNCE_MS             30U
#define LONG_PRESS_MS           5000U
#define EVENT_QUEUE_LEN         16U

typedef enum {
    BS_IDLE = 0,
    BS_DEBOUNCE_PRESS,
    BS_PRESSED,
    BS_DEBOUNCE_RELEASE,
} BtnState;

typedef struct {
    BtnState          state;
    uint32_t          edge_tick;
    uint32_t          press_tick;
    bool              long_fired;
    bool              boot_held;
    volatile bool     edge_flag;
    volatile uint32_t isr_tick;
} BtnCtx;

static BtnCtx s_btn[BTN_COUNT];

static Event             s_queue[EVENT_QUEUE_LEN];
static volatile uint32_t s_qhead = 0U;
static volatile uint32_t s_qtail = 0U;

static bool button_is_pressed_hw(ButtonId id)
{
    if (id == BTN_RED)
    {
        /* 1. Bouton bleu Nucleo B1 (PC13) : actif a l'etat BAS */
        bool red_b1 = (HAL_GPIO_ReadPin(BUTTON_USER_PORT, BUTTON_USER_PIN) == GPIO_PIN_RESET);

#if BTN_ENABLE_GROVE_RED
        /* 2. Bouton Grove Rouge sur D3 (PB3) : actif a l'etat BAS (pull-up) */
        bool red_d3 = (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_3) == GPIO_PIN_RESET);
        /* 3. Bouton Grove Rouge alternatif sur D5 (PB4) : actif a l'etat BAS (pull-up) */
        bool red_d5 = (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_4) == GPIO_PIN_RESET);
        /* 4. Bouton Grove Rouge alternatif sur D8 (PA9) : actif a l'etat BAS (pull-up) */
        bool red_d8 = (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_9) == GPIO_PIN_RESET);
        /* 5. Bouton Grove Rouge alternatif sur D9 (PC7) : actif a l'etat BAS (pull-up) */
        bool red_d9 = (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_7) == GPIO_PIN_RESET);

        return (red_b1 || red_d3 || red_d5 || red_d8 || red_d9);
#else
        return red_b1;
#endif
    }
    if (id == BTN_GREEN)
    {
        /* Bouton Grove vert sur D2 (PA10) : actif a l'etat BAS (pull-up) */
        return (HAL_GPIO_ReadPin(BUTTON1_PORT, BUTTON1_PIN) == GPIO_PIN_RESET);
    }
    return false;
}

static void push_event(EventType t, ButtonId id, uint32_t tick, uint32_t dur)
{
    uint32_t next = (s_qhead + 1U) % EVENT_QUEUE_LEN;
    if (next == s_qtail)
    {
        return; /* File pleine : on ignore sans bloquer */
    }
    s_queue[s_qhead] = (Event){
        .type        = t,
        .button      = id,
        .tick        = tick,
        .duration_ms = dur
    };
    s_qhead = next;

    if (t == EVENT_BTN_PRESS)
    {
        if (id == BTN_RED)
        {
            printf("[BTN] Appui detecte sur bouton ROUGE (maintenez 5s pour changer de mode)...\r\n");
        }
        else
        {
            printf("[BTN] Appui detecte sur bouton VERT (bascule Standard <-> Eco)\r\n");
        }
    }
    else if (t == EVENT_BTN_LONG)
    {
        printf("[BTN] *** Appui de 5 SECONDES confirme sur bouton %s ! ***\r\n", (id == BTN_RED) ? "ROUGE" : "VERT");
    }
    else if (t == EVENT_BTN_RELEASE)
    {
        printf("[BTN] Relachement bouton %s (duree: %lu ms)\r\n",
               (id == BTN_RED) ? "ROUGE" : "VERT", (unsigned long)dur);
    }
}

void Interruption_Init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Mode  = GPIO_MODE_IT_RISING_FALLING;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;

    /* Bouton Rouge Nucleo B1 (PC13 -> EXTI15_10, actif BAS avec pull-up) */
    gpio.Pin  = BUTTON_USER_PIN;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(BUTTON_USER_PORT, &gpio);

    /* Bouton Vert Grove D2 (PA10 -> EXTI15_10, actif BAS avec pull-up) */
    gpio.Pin  = BUTTON1_PIN;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(BUTTON1_PORT, &gpio);

#if BTN_ENABLE_GROVE_RED
    /* Boutons Rouges Grove : D3 (PB3 -> EXTI3), D5 (PB4 -> EXTI4) avec pull-up */
    gpio.Pin  = GPIO_PIN_3 | GPIO_PIN_4;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOB, &gpio);

    /* Bouton Rouge Grove alternatif : D8 (PA9 -> EXTI9_5) avec pull-up */
    gpio.Pin  = GPIO_PIN_9;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &gpio);

    /* Bouton Rouge Grove alternatif : D9 (PC7 -> EXTI9_5) avec pull-up */
    gpio.Pin  = GPIO_PIN_7;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOC, &gpio);
#endif

    /* Nettoyage des drapeaux EXTI avant activation NVIC */
    __HAL_GPIO_EXTI_CLEAR_IT(BUTTON_USER_PIN);
    __HAL_GPIO_EXTI_CLEAR_IT(BUTTON1_PIN);
#if BTN_ENABLE_GROVE_RED
    __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_3);
    __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_4);
    __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_7);
    __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_9);
#endif

    HAL_NVIC_SetPriority(EXTI15_10_IRQn, 3U, 0U);
    HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

#if BTN_ENABLE_GROVE_RED
    HAL_NVIC_SetPriority(EXTI3_IRQn, 3U, 0U);
    HAL_NVIC_EnableIRQ(EXTI3_IRQn);

    HAL_NVIC_SetPriority(EXTI4_IRQn, 3U, 0U);
    HAL_NVIC_EnableIRQ(EXTI4_IRQn);

    HAL_NVIC_SetPriority(EXTI9_5_IRQn, 3U, 0U);
    HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);
#endif

    s_qhead = 0U;
    s_qtail = 0U;

    /* Stabilisation des résistances de pull-up (filtre RC 100nF sur PC13) avant lecture au boot */
    uint32_t t0 = HAL_GetTick();
    while ((HAL_GetTick() - t0) < 25U) {}

    uint32_t now = HAL_GetTick();
    for (int i = 0; i < (int)BTN_COUNT; ++i)
    {
        s_btn[i].edge_flag = false;
        s_btn[i].edge_tick = now;
        if (button_is_pressed_hw((ButtonId)i))
        {
            s_btn[i].state      = BS_PRESSED;
            s_btn[i].press_tick = now;
            s_btn[i].long_fired = true;
            s_btn[i].boot_held  = true; /* Maintenu dès le reset : ignorer le relâchement initial */
        }
        else
        {
            s_btn[i].state      = BS_IDLE;
            s_btn[i].press_tick = 0U;
            s_btn[i].long_fired = false;
            s_btn[i].boot_held  = false;
        }
    }
}

/* =========================================================================
 * Routines d'interruption EXTI (très courtes : horodatage + flag uniquement)
 * ========================================================================= */
void EXTI15_10_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(BUTTON_USER_PIN);
    HAL_GPIO_EXTI_IRQHandler(BUTTON1_PIN);
}

#if BTN_ENABLE_GROVE_RED
void EXTI3_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_3);
}

void EXTI4_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_4);
}

void EXTI9_5_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_7);
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_9);
}
#endif

void HAL_GPIO_EXTI_Callback(uint16_t pin)
{
    uint32_t t = HAL_GetTick();
    if (pin == BUTTON_USER_PIN
#if BTN_ENABLE_GROVE_RED
        || pin == GPIO_PIN_3 || pin == GPIO_PIN_4 || pin == GPIO_PIN_7 || pin == GPIO_PIN_9
#endif
    )
    {
        s_btn[BTN_RED].isr_tick  = t;
        s_btn[BTN_RED].edge_flag = true;
    }
    else if (pin == BUTTON1_PIN)
    {
        s_btn[BTN_GREEN].isr_tick  = t;
        s_btn[BTN_GREEN].edge_flag = true;
    }
}

void Interruption_Update(uint32_t now)
{
    for (int i = 0; i < (int)BTN_COUNT; ++i)
    {
        BtnCtx *b = &s_btn[i];
        bool pressed = button_is_pressed_hw((ButtonId)i);

        if (b->edge_flag)
        {
            b->edge_flag = false;
            b->edge_tick = b->isr_tick;
        }

        switch (b->state)
        {
        case BS_IDLE:
            if (pressed)
            {
                b->state     = BS_DEBOUNCE_PRESS;
                b->edge_tick = now;
            }
            break;

        case BS_DEBOUNCE_PRESS:
            if (!pressed)
            {
                b->state = BS_IDLE;
            }
            else if ((now - b->edge_tick) >= DEBOUNCE_MS)
            {
                b->state      = BS_PRESSED;
                b->press_tick = now;
                b->long_fired = false;
                b->boot_held  = false;
                push_event(EVENT_BTN_PRESS, (ButtonId)i, now, 0U);
            }
            break;

        case BS_PRESSED:
            if (!pressed)
            {
                b->state     = BS_DEBOUNCE_RELEASE;
                b->edge_tick = now;
            }
            else if (!b->long_fired && (now - b->press_tick) >= LONG_PRESS_MS)
            {
                b->long_fired = true;
                push_event(EVENT_BTN_LONG, (ButtonId)i, now, LONG_PRESS_MS);
            }
            break;

        case BS_DEBOUNCE_RELEASE:
            if (pressed)
            {
                b->state = BS_PRESSED;
            }
            else if ((now - b->edge_tick) >= DEBOUNCE_MS)
            {
                uint32_t dur = now - b->press_tick;
                if (b->boot_held)
                {
                    /* Relâchement de l'appui initial au boot (entrée en CONFIGURATION) :
                     * on n'émet pas d'événement RELEASE qui ferait quitter le mode ! */
                    b->boot_held = false;
                }
                else
                {
                    push_event(EVENT_BTN_RELEASE, (ButtonId)i, now, dur);
                }
                b->state = BS_IDLE;
            }
            break;
        }
    }
}

bool Interruption_PopEvent(Event *out)
{
    if (out == NULL || s_qtail == s_qhead)
    {
        return false;
    }
    *out = s_queue[s_qtail];
    s_qtail = (s_qtail + 1U) % EVENT_QUEUE_LEN;
    return true;
}

bool Interruption_IsHeld(ButtonId id)
{
    if (id >= BTN_COUNT)
    {
        return false;
    }
    return (s_btn[id].state == BS_PRESSED || s_btn[id].state == BS_DEBOUNCE_PRESS);
}

uint32_t Interruption_HeldMs(ButtonId id)
{
    if (id >= BTN_COUNT)
    {
        return 0U;
    }
    BtnCtx *b = &s_btn[id];
    if (b->state != BS_PRESSED && b->state != BS_DEBOUNCE_RELEASE)
    {
        return 0U;
    }
    return HAL_GetTick() - b->press_tick;
}

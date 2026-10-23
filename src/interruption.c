#include "interruption.h"
#include "board_pins.h"
#include "stm32l4xx_hal.h"

/* --- Pins boutons (à confirmer sur câblage réel) ---
 *  RED   = PC13 (B1 utilisateur de la Nucleo, actif bas → pull-up)
 *  GREEN = PA10 (à câbler, actif bas → pull-up)
 *  ⚠ Vérifier polarité (M1-4) — si actif haut, inverser la macro BTN_ACTIVE_LOW.
 */
#define BTN_RED_PORT    GPIOC
#define BTN_RED_PIN     GPIO_PIN_13
#define BTN_GREEN_PORT  GPIOA
#define BTN_GREEN_PIN   GPIO_PIN_10
#define BTN_ACTIVE_LOW  1

#define DEBOUNCE_MS     30
#define LONG_PRESS_MS   5000
#define EVENT_QUEUE_LEN 16

typedef enum {
    BS_IDLE = 0,
    BS_DEBOUNCE_PRESS,
    BS_PRESSED,
    BS_DEBOUNCE_RELEASE,
} BtnState;

typedef struct {
    GPIO_TypeDef *port;
    uint16_t      pin;
    BtnState      state;
    uint32_t      edge_tick;
    uint32_t      press_tick;
    bool          long_fired;
    volatile bool edge_flag;   /* armé par l'EXTI */
} BtnCtx;

static BtnCtx s_btn[BTN_COUNT] = {
    [BTN_RED]   = { .port = BTN_RED_PORT,   .pin = BTN_RED_PIN   },
    [BTN_GREEN] = { .port = BTN_GREEN_PORT, .pin = BTN_GREEN_PIN },
};

static Event s_queue[EVENT_QUEUE_LEN];
static volatile uint32_t s_qhead = 0;
static volatile uint32_t s_qtail = 0;

static inline bool pin_pressed(const BtnCtx *b) {
    GPIO_PinState s = HAL_GPIO_ReadPin(b->port, b->pin);
#if BTN_ACTIVE_LOW
    return s == GPIO_PIN_RESET;
#else
    return s == GPIO_PIN_SET;
#endif
}

static void push_event(EventType t, ButtonId id, uint32_t tick, uint32_t dur) {
    uint32_t next = (s_qhead + 1) % EVENT_QUEUE_LEN;
    if (next == s_qtail) return;            /* file pleine : on jette */
    s_queue[s_qhead] = (Event){ .type = t, .button = id,
                                .tick = tick, .duration_ms = dur };
    s_qhead = next;
}

void Interruption_Init(void) {
    /* EXTI déjà configuré dans setup.c pour PC13 ; ajouter PA10 :
     *   GPIO_InitTypeDef i = {0};
     *   i.Pin  = BTN_GREEN_PIN;
     *   i.Mode = GPIO_MODE_IT_RISING_FALLING;
     *   i.Pull = GPIO_PULLUP;
     *   HAL_GPIO_Init(BTN_GREEN_PORT, &i);
     *   HAL_NVIC_SetPriority(EXTI15_10_IRQn, 3, 0);
     *   HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
     */
    s_qhead = s_qtail = 0;
}

/* Appelé par HAL depuis EXTI15_10_IRQHandler (ou EXTI4_IRQn selon pin) */
void HAL_GPIO_EXTI_Callback(uint16_t pin) {
    if (pin == BTN_RED_PIN)   s_btn[BTN_RED].edge_flag   = true;
    if (pin == BTN_GREEN_PIN) s_btn[BTN_GREEN].edge_flag = true;
}

/* Machine à états par bouton — appelée ~10 ms depuis main */
void Interruption_Update(uint32_t now) {
    for (int i = 0; i < BTN_COUNT; i++) {
        BtnCtx *b = &s_btn[i];
        bool pressed = pin_pressed(b);
        bool edge = b->edge_flag;
        if (edge) { b->edge_flag = false; b->edge_tick = now; }

        switch (b->state) {
        case BS_IDLE:
            if (pressed && edge) {
                b->state = BS_DEBOUNCE_PRESS;
                b->edge_tick = now;
            }
            break;
        case BS_DEBOUNCE_PRESS:
            if (!pressed) { b->state = BS_IDLE; }
            else if ((now - b->edge_tick) >= DEBOUNCE_MS) {
                b->state = BS_PRESSED;
                b->press_tick = now;
                b->long_fired = false;
                push_event(EVENT_BTN_PRESS, (ButtonId)i, now, 0);
            }
            break;
        case BS_PRESSED:
            if (!pressed && edge) {
                b->state = BS_DEBOUNCE_RELEASE;
                b->edge_tick = now;
            } else if (!b->long_fired && (now - b->press_tick) >= LONG_PRESS_MS) {
                b->long_fired = true;
                push_event(EVENT_BTN_LONG, (ButtonId)i, now, LONG_PRESS_MS);
            }
            break;
        case BS_DEBOUNCE_RELEASE:
            if (pressed) { b->state = BS_PRESSED; }
            else if ((now - b->edge_tick) >= DEBOUNCE_MS) {
                uint32_t dur = now - b->press_tick;
                push_event(EVENT_BTN_RELEASE, (ButtonId)i, now, dur);
                b->state = BS_IDLE;
            }
            break;
        }
    }
}

bool Interruption_PopEvent(Event *out) {
    if (s_qtail == s_qhead) return false;
    *out = s_queue[s_qtail];
    s_qtail = (s_qtail + 1) % EVENT_QUEUE_LEN;
    return true;
}

bool Interruption_IsHeld(ButtonId id) {
    return s_btn[id].state == BS_PRESSED || s_btn[id].state == BS_DEBOUNCE_PRESS;
}

uint32_t Interruption_HeldMs(ButtonId id) {
    BtnCtx *b = &s_btn[id];
    if (b->state != BS_PRESSED && b->state != BS_DEBOUNCE_RELEASE) return 0;
    return HAL_GetTick() - b->press_tick;
}
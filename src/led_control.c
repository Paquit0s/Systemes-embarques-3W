#include "led_control.h"
#include "mode_manager.h"
#include "grove_rgb.h"   // ⚠ API : GroveRGB_SetAll(r,g,b)
#include "stm32l4xx_hal.h"

#define BLINK_MS   400
#define HOLD_MS    1500

static struct {
    CodeErreur last_err;
    CapteurId  last_cap;
    uint32_t   err_tick;
} s_led = { .last_err = ERR_NONE };

void LedControl_Init(void) {
    GroveRGB_Init();
    GroveRGB_SetAll(0, 0, 0);
}

void LedControl_ReportError(CodeErreur err, CapteurId cap) {
    s_led.last_err = err;
    s_led.last_cap = cap;
    s_led.err_tick = HAL_GetTick();
}

void LedControl_ClearError(CodeErreur err) {
    if (s_led.last_err == err) s_led.last_err = ERR_NONE;
}

static void mode_color(uint8_t *r, uint8_t *g, uint8_t *b) {
    switch (ModeManager_GetMode()) {
    case MODE_STANDARD:      *r=0;   *g=180; *b=0;   break;
    case MODE_CONFIGURATION: *r=200; *g=180; *b=0;   break;
    case MODE_ECO:           *r=0;   *g=0;   *b=180; break;
    case MODE_MAINTENANCE:   *r=255; *g=100; *b=0;   break;
    default:                 *r=0;   *g=0;   *b=0;   break;
    }
}

void LedControl_Update(uint32_t now) {
    uint8_t r, g, b;

    /* Erreur active récente : clignotement rouge/blanc (spec) */
    if (s_led.last_err != ERR_NONE && (now - s_led.err_tick) < HOLD_MS * 20) {
        bool on = ((now / BLINK_MS) & 1);
        GroveRGB_SetAll(on ? 200 : 255, on ? 0 : 255, on ? 0 : 255);
        return;
    }
    /* Erreur RTC = rouge/bleu selon spec — prioritaire si non effacée */
    if (s_led.last_err == ERR_RTC_INIT || s_led.last_err == ERR_RTC_LECTURE) {
        bool on = ((now / BLINK_MS) & 1);
        GroveRGB_SetAll(on ? 200 : 0, 0, on ? 0 : 200);
        return;
    }

    mode_color(&r, &g, &b);
    GroveRGB_SetAll(r, g, b);
}
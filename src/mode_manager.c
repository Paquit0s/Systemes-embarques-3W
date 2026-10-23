#include "mode_manager.h"
#include "led_control.h"
#include "config_uart.h"
#include <string.h>

static struct {
    ModeSysteme current;
    ModeSysteme previous;      /* pour retour MAINTENANCE */
    uint32_t    config_last_activity_ms;
} s_mm;

void ModeManager_Init(void) {
    /* Au boot : si bouton rouge maintenu → CONFIGURATION, sinon STANDARD.
     * ⚠ appeler Interruption_Init() + ~50 ms de marge avant. */
    extern bool Interruption_IsHeld(ButtonId);
    s_mm.current  = Interruption_IsHeld(BTN_RED) ? MODE_CONFIGURATION : MODE_STANDARD;
    s_mm.previous = s_mm.current;
    s_mm.config_last_activity_ms = HAL_GetTick();
}

ModeSysteme ModeManager_GetMode(void)     { return s_mm.current; }
ModeSysteme ModeManager_GetPrevious(void) { return s_mm.previous; }

void ModeManager_NotifyActivity(void) {
    s_mm.config_last_activity_ms = HAL_GetTick();
}

bool ModeManager_RequestMode(ModeSysteme target) {
    if (target == s_mm.current || target >= MODE_COUNT) return false;
    s_mm.previous = s_mm.current;
    s_mm.current  = target;
    if (target == MODE_CONFIGURATION) {
        s_mm.config_last_activity_ms = HAL_GetTick();
    }
    return true;
}

/* Transitions sur événements boutons */
static void on_event(const Event *e) {
    if (e->type != EVENT_BTN_LONG) return;

    if (e->button == BTN_GREEN && s_mm.current == MODE_STANDARD) {
        ModeManager_RequestMode(MODE_ECO);
    } else if (e->button == BTN_RED) {
        if (s_mm.current == MODE_STANDARD || s_mm.current == MODE_ECO) {
            ModeManager_RequestMode(MODE_MAINTENANCE);
        } else if (s_mm.current == MODE_MAINTENANCE) {
            ModeManager_RequestMode(s_mm.previous);
        }
    }
}

void ModeManager_Update(uint32_t now) {
    extern bool Interruption_PopEvent(Event*);
    Event e;
    while (Interruption_PopEvent(&e)) on_event(&e);

    /* Timeout CONFIG (30 min par défaut) */
    if (s_mm.current == MODE_CONFIGURATION) {
        const Config *c = ConfigUART_Get();
        if (c && (now - s_mm.config_last_activity_ms) >= c->config_timeout_s * 1000U) {
            ModeManager_RequestMode(MODE_STANDARD);
        }
    }
}
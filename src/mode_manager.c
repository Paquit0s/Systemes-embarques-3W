#include "mode_manager.h"
#include "interruption.h"
#include "led_control.h"
#include "config_uart.h"
#include "stm32l4xx_hal.h"

static struct {
    ModeSysteme current;
    ModeSysteme previous;      /* Pour retour depuis MAINTENANCE (STANDARD ou ECO) */
    uint32_t    config_last_activity_ms;
} s_mm;

void ModeManager_Init(void)
{
    /* Au démarrage : bouton rouge maintenu -> CONFIGURATION, sinon STANDARD */
    s_mm.current  = Interruption_IsHeld(BTN_RED) ? MODE_CONFIGURATION : MODE_STANDARD;
    s_mm.previous = MODE_STANDARD;
    s_mm.config_last_activity_ms = HAL_GetTick();
}

ModeSysteme ModeManager_GetMode(void)
{
    return s_mm.current;
}

ModeSysteme ModeManager_GetPrevious(void)
{
    return s_mm.previous;
}

void ModeManager_NotifyActivity(void)
{
    s_mm.config_last_activity_ms = HAL_GetTick();
}

bool ModeManager_RequestMode(ModeSysteme target)
{
    if (target == s_mm.current || target >= MODE_COUNT)
    {
        return false;
    }
    if (s_mm.current == MODE_STANDARD || s_mm.current == MODE_ECO)
    {
        s_mm.previous = s_mm.current;
    }
    s_mm.current = target;
    if (target == MODE_CONFIGURATION)
    {
        s_mm.config_last_activity_ms = HAL_GetTick();
    }
    return true;
}

/* Transitions sur appui long 5 s des boutons (P1-8) */
static void on_event(const Event *e)
{
    if (e->type != EVENT_BTN_LONG)
    {
        return;
    }

    if (e->button == BTN_GREEN)
    {
        if (s_mm.current == MODE_STANDARD)
        {
            (void)ModeManager_RequestMode(MODE_ECO);
        }
        else if (s_mm.current == MODE_ECO)
        {
            (void)ModeManager_RequestMode(MODE_STANDARD);
        }
    }
    else if (e->button == BTN_RED)
    {
        if (s_mm.current == MODE_STANDARD || s_mm.current == MODE_ECO)
        {
            (void)ModeManager_RequestMode(MODE_MAINTENANCE);
        }
        else if (s_mm.current == MODE_MAINTENANCE)
        {
            (void)ModeManager_RequestMode(s_mm.previous);
        }
    }
}

void ModeManager_Update(uint32_t now)
{
    Event e;
    while (Interruption_PopEvent(&e))
    {
        on_event(&e);
    }

    /* Sortie automatique de CONFIGURATION après 30 min d'inactivité (1800 s par défaut) */
    if (s_mm.current == MODE_CONFIGURATION)
    {
        const Config *c = ConfigUART_Get();
        uint32_t timeout_ms = (c != NULL ? c->config_timeout_s : 1800U) * 1000U;
        if ((now - s_mm.config_last_activity_ms) >= timeout_ms)
        {
            (void)ModeManager_RequestMode(MODE_STANDARD);
        }
    }
}

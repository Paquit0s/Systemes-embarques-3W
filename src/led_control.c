#include "led_control.h"
#include "mode_manager.h"
#include "grove_rgb.h"
#include "stm32l4xx_hal.h"

/* Période de clignotement 1 Hz (500 ms phase 1 / 500 ms phase 2) conforme au Cahier des Charges */
#define HALF_PERIOD_1HZ_MS   500U
#define ERR_DISPLAY_HOLD_MS  10000U

static struct {
    CodeErreur last_err;
    CapteurId  last_cap;
    uint32_t   err_tick;
    uint8_t    cur_r;
    uint8_t    cur_g;
    uint8_t    cur_b;
    bool       initialized;
} s_led = { .last_err = ERR_NONE, .initialized = false };

static void apply_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_led.initialized || r != s_led.cur_r || g != s_led.cur_g || b != s_led.cur_b)
    {
        GroveRGB_SetColor(r, g, b);
        s_led.cur_r       = r;
        s_led.cur_g       = g;
        s_led.cur_b       = b;
        s_led.initialized = true;
    }
}

void LedControl_Init(void)
{
    GroveRGB_Init();
    s_led.initialized = false;
    apply_rgb(0U, 0U, 0U);
}

void LedControl_ReportError(CodeErreur err, CapteurId cap)
{
    s_led.last_err = err;
    s_led.last_cap = cap;
    s_led.err_tick = HAL_GetTick();
}

void LedControl_ClearError(CodeErreur err)
{
    if (s_led.last_err == err)
    {
        s_led.last_err = ERR_NONE;
    }
}

static void mode_color(uint8_t *r, uint8_t *g, uint8_t *b)
{
    switch (ModeManager_GetMode())
    {
    case MODE_STANDARD:      *r = 0U;   *g = 200U; *b = 0U;   break; /* Vert   */
    case MODE_CONFIGURATION: *r = 220U; *g = 180U; *b = 0U;   break; /* Jaune  */
    case MODE_ECO:           *r = 0U;   *g = 0U;   *b = 200U; break; /* Bleu   */
    case MODE_MAINTENANCE:   *r = 255U; *g = 80U;  *b = 0U;   break; /* Orange */
    default:                 *r = 0U;   *g = 0U;   *b = 0U;   break;
    }
}

void LedControl_Update(uint32_t now)
{
    uint8_t r = 0U, g = 0U, b = 0U;

    /* 1. Erreur RTC (critique / persistante) : Clignotement Rouge / Bleu @ 1 Hz (durée identique) */
    if (s_led.last_err == ERR_RTC_INIT || s_led.last_err == ERR_RTC_LECTURE)
    {
        bool phase_red = (((now / HALF_PERIOD_1HZ_MS) & 1U) == 0U);
        apply_rgb(phase_red ? 255U : 0U, 0U, phase_red ? 0U : 255U);
        return;
    }

    /* 2. Autres erreurs actives (affichées pendant ERR_DISPLAY_HOLD_MS ou tant que réarmées) */
    if (s_led.last_err != ERR_NONE)
    {
        if ((now - s_led.err_tick) < ERR_DISPLAY_HOLD_MS)
        {
            switch (s_led.last_err)
            {
            case ERR_SD_PLEINE:
            {
                /* Carte SD pleine : Rouge / Blanc @ 1 Hz, durée identique (500 ms / 500 ms) */
                bool phase_red = (((now / HALF_PERIOD_1HZ_MS) & 1U) == 0U);
                apply_rgb(255U, phase_red ? 0U : 255U, phase_red ? 0U : 255U);
                return;
            }

            case ERR_SD_ABSENTE:
            case ERR_SD_ECRITURE:
            {
                /* Erreur d'accès ou d'écriture SD : Rouge / Blanc @ 1 Hz, blanc 2x plus long (333 ms / 667 ms) */
                uint32_t slot = (now % 1000U);
                if (slot < 333U)
                {
                    apply_rgb(255U, 0U, 0U);
                }
                else
                {
                    apply_rgb(255U, 255U, 255U);
                }
                return;
            }

            case ERR_GPS_TIMEOUT:
            case ERR_GPS_PAS_FIX:
            {
                /* Erreur d'accès aux données du GPS : Rouge / Jaune @ 1 Hz, durée identique (500 ms / 500 ms) */
                bool phase_red = (((now / HALF_PERIOD_1HZ_MS) & 1U) == 0U);
                apply_rgb(phase_red ? 255U : 220U, phase_red ? 0U : 180U, 0U);
                return;
            }

            case ERR_CAPTEUR_INCOHERENT:
            {
                /* Données capteur incohérentes : Rouge / Vert @ 1 Hz, vert 2x plus long (333 ms / 667 ms) */
                uint32_t slot = (now % 1000U);
                if (slot < 333U)
                {
                    apply_rgb(255U, 0U, 0U);
                }
                else
                {
                    apply_rgb(0U, 200U, 0U);
                }
                return;
            }

            case ERR_CAPTEUR_ACCES:
            case ERR_CAPTEUR_TIMEOUT:
            default:
            {
                /* Erreur d'accès aux données d'un capteur : Rouge / Vert @ 1 Hz, durée identique (500 ms / 500 ms) */
                bool phase_red = (((now / HALF_PERIOD_1HZ_MS) & 1U) == 0U);
                apply_rgb(phase_red ? 255U : 0U, phase_red ? 0U : 200U, 0U);
                return;
            }
            }
        }
        else
        {
            s_led.last_err = ERR_NONE;
        }
    }

    /* 3. Signalisation nominale du mode actif */
    mode_color(&r, &g, &b);
    apply_rgb(r, g, b);
}

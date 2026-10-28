#include "capteur_manager.h"
#include "config_uart.h"
#include "led_control.h"
#include "mode_manager.h"
#include "setup.h"
#include "stm32l4xx_hal.h"
#include "grove_rtc_ds1307.h"
#include "grove_bme680.h"
#include "grove_light.h"
#include "grove_gps_air530z.h"
#include <math.h>
#include <stdbool.h>
#include <stdio.h>

static Mesure  s_last;
static uint8_t s_to_bme   = 0U;
static uint8_t s_to_light = 0U;
static uint8_t s_to_gps   = 0U;
static uint8_t s_to_rtc   = 0U;
static bool    s_eco_skip_next_gps = false;

static StatutMesure statut_from_float(float v, float vmin, float vmax)
{
    if (isnan(v))
    {
        return STATUT_NA;
    }
    if (v < vmin || v > vmax)
    {
        return STATUT_INCOHERENT;
    }
    return STATUT_OK;
}

bool CapteurManager_Init(void)
{
    s_to_bme   = 0U;
    s_to_light = 0U;
    s_to_gps   = 0U;
    s_to_rtc   = 0U;
    s_eco_skip_next_gps = false;

    bool rtc_ok = GroveRTC_Init();
    if (!rtc_ok)
    {
        printf("[ERR] Echec d'initialisation de la RTC DS1307 (0x68).\r\n");
    }

    if (!GroveBME680_Init())
    {
        printf("[WARN] BME680 non detecte au demarrage (nouvel essai a l'acquisition).\r\n");
    }

    /* P2-3 : Calibration matérielle ADC1 12 bits + configuration canal PA4 */
    GroveLight_Init();
    if (!GroveGPS_Init())
        printf("[WARN] Impossible d'armer la reception IT du GPS (UART4).\r\n");
    }

    return rtc_ok;
}

bool CapteurManager_Acquire(Mesure *out)
{
    const Config *c = ConfigUART_Get();
    Mesure m = {0};
    bool any_ok = false;

    BSP_Watchdog_Refresh();

    /* --- 1. Horloge RTC (DS1307) --- */
    uint32_t t0 = HAL_GetTick();
    bool rtc_ok = GroveRTC_GetDateTime(&m.timestamp);
    uint32_t rtc_dt = HAL_GetTick() - t0;
    if (!rtc_ok || rtc_dt > c->timeout_rtc_ms)
    {
        m.timestamp = (RTC_DateTime){0};
        if (++s_to_rtc >= 2U)
        {
            LedControl_ReportError(ERR_RTC_LECTURE, CAPTEUR_RTC);
        }
    }
    else
    {
        s_to_rtc = 0U;
        LedControl_ClearError(ERR_RTC_LECTURE);
    }

    BSP_Watchdog_Refresh();

    /* --- 2. Capteur environnemental BME680 (T, HR, P) --- */
    t0 = HAL_GetTick();
    bool bme_ok = GroveBME680_ReadEnvironment(&m.env);
    uint32_t bme_dt = HAL_GetTick() - t0;
    if (!bme_ok || bme_dt > c->timeout_bme680_ms)
    {
        m.statut_temp  = STATUT_NA;
        m.statut_hum   = STATUT_NA;
        m.statut_press = STATUT_NA;
        if (++s_to_bme >= 2U)
        {
            LedControl_ReportError(ERR_CAPTEUR_TIMEOUT, CAPTEUR_BME680);
            printf("[ERR] BME680 : 2 timeouts consecutifs -> NA\r\n");
        }
    }
    else
    {
        s_to_bme = 0U;
        m.statut_temp  = statut_from_float(m.env.temp,  c->temp_min,  c->temp_max);
        m.statut_hum   = statut_from_float(m.env.hum,   c->hum_min,   c->hum_max);
        m.statut_press = statut_from_float(m.env.press, c->press_min, c->press_max);

        if (m.statut_temp == STATUT_OK || m.statut_hum == STATUT_OK || m.statut_press == STATUT_OK)
        {
            any_ok = true;
        }
        if (m.statut_temp  == STATUT_INCOHERENT ||
            m.statut_hum   == STATUT_INCOHERENT ||
            m.statut_press == STATUT_INCOHERENT)
        {
            LedControl_ReportError(ERR_CAPTEUR_INCOHERENT, CAPTEUR_BME680);
        }
    }

    BSP_Watchdog_Refresh();

    /* --- 3. Capteur de luminosité Grove Light (ADC 12 bits calibré : 0..4095, P2-3) --- */
    t0 = HAL_GetTick();
    bool light_ok = GroveLight_Read(&m.light);
    uint32_t light_dt = HAL_GetTick() - t0;
    if (!light_ok || light_dt > c->timeout_light_ms)
    {
        m.statut_light = STATUT_NA;
        if (++s_to_light >= 2U)
        {
            LedControl_ReportError(ERR_CAPTEUR_TIMEOUT, CAPTEUR_LIGHT);
            printf("[ERR] Capteur lumiere : 2 timeouts consecutifs -> NA\r\n");
        }
    }
    else
    {
        s_to_light = 0U;
        m.statut_light = (m.light < c->light_min || m.light > c->light_max)
                       ? STATUT_INCOHERENT : STATUT_OK;
        if (m.statut_light == STATUT_OK)
        {
            any_ok = true;
        }
        else
        {
            LedControl_ReportError(ERR_CAPTEUR_INCOHERENT, CAPTEUR_LIGHT);
        }
    }

    BSP_Watchdog_Refresh();

    /* --- 4. GPS Air530Z (P2-6 : lecture 1 cycle sur 2 en MODE_ECO) --- */
    bool read_gps_this_cycle = true;
    if (ModeManager_GetMode() == MODE_ECO)
    {
        if (s_eco_skip_next_gps)
        {
            read_gps_this_cycle = false;
            s_eco_skip_next_gps = false;
        }
        else
        {
            s_eco_skip_next_gps = true;
        }
    }
    else
    {
        s_eco_skip_next_gps = false;
    }

    if (!read_gps_this_cycle)
    {
        m.statut_gps = STATUT_NA;
        m.gps.valid  = false;
    }
    else
    {
        bool gps_frame_ok = GroveGPS_ReadGGA(&m.gps, c->timeout_gps_ms);
        if (!gps_frame_ok)
        {
            m.statut_gps = STATUT_NA;
            if (++s_to_gps >= 2U)
            {
                LedControl_ReportError(ERR_GPS_TIMEOUT, CAPTEUR_GPS);
            }
        }
        else
        {
            s_to_gps = 0U;
            if (m.gps.valid)
            {
                m.statut_gps = STATUT_OK;
                any_ok = true;
            }
            else
            {
                /* Trame reçue mais pas encore de fix satellite -> position marquée NA */
                m.statut_gps = STATUT_NA;
            }
        }
    }

    BSP_Watchdog_Refresh();

    s_last = m;
    if (out != NULL)
    {
        *out = m;
    }
    return any_ok;
}

const Mesure *CapteurManager_GetLast(void)
{
    return &s_last;
}

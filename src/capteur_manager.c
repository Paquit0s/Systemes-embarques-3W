#include "capteur_manager.h"
#include "config_uart.h"
#include "led_control.h"
#include "stm32l4xx_hal.h"
#include "grove_rtc_ds1307.h"
#include "grove_bme680.h"
#include "grove_light.h"
#include "grove_gps_air530z.h"
#include <math.h>

// Ajout de stdbool pour être sûr
#include <stdbool.h>

static Mesure s_last;
static uint8_t s_to_bme = 0, s_to_light = 0, s_to_gps = 0, s_to_rtc = 0;

static StatutMesure statut_from(const Config *c, float v, float vmin, float vmax) {
    if (isnan(v)) return STATUT_NA;
    if (v < vmin || v > vmax) return STATUT_INCOHERENT;
    return STATUT_OK;
}

bool CapteurManager_Init(void) {
    const Config *c = ConfigUART_Get();
    bool ok = true;
    
    // CORRECTION 1 : On retire le "if (!...)" car ces fonctions ne renvoient rien (void)
    GroveRTC_Init();      
    GroveBME680_Init();        
    GroveLight_Init();    
    GroveGPS_Init();           
    
    (void)c;
    return ok;
}

bool CapteurManager_Acquire(Mesure *out) {
    const Config *c = ConfigUART_Get();
    Mesure m = {0};
    bool any_ok = false;

    /* --- RTC --- */
    uint32_t t0 = HAL_GetTick();
    bool rtc_ok = GroveRTC_GetDateTime(&m.timestamp);
    uint32_t rtc_dt = HAL_GetTick() - t0;
    if (!rtc_ok || rtc_dt > c->timeout_rtc_ms) {
        if (++s_to_rtc >= 2) LedControl_ReportError(ERR_RTC_LECTURE, CAPTEUR_RTC);
    } else s_to_rtc = 0;

    /* --- BME680 --- */
    t0 = HAL_GetTick();
    bool bme_ok = GroveBME680_Read(&m.env);   
    uint32_t bme_dt = HAL_GetTick() - t0;
    if (!bme_ok || bme_dt > c->timeout_bme680_ms) {
        m.statut_temp = m.statut_hum = m.statut_press = STATUT_NA;
        if (++s_to_bme >= 2) LedControl_ReportError(ERR_CAPTEUR_TIMEOUT, CAPTEUR_BME680);
    } else {
        s_to_bme = 0;
        m.statut_temp  = statut_from(c, m.env.temp,  c->temp_min,  c->temp_max);
        m.statut_hum   = statut_from(c, m.env.hum,   c->hum_min,   c->hum_max);
        m.statut_press = statut_from(c, m.env.press, c->press_min, c->press_max);
        
        if (m.statut_temp  == STATUT_OK) any_ok = true;
        if (m.statut_hum   == STATUT_INCOHERENT || m.statut_press == STATUT_INCOHERENT) {
            LedControl_ReportError(ERR_CAPTEUR_INCOHERENT, CAPTEUR_BME680);
        }
    }

    /* --- Lumière --- */
    t0 = HAL_GetTick();
    bool light_ok = GroveLight_Read(&m.light);    
    uint32_t light_dt = HAL_GetTick() - t0;
    if (!light_ok || light_dt > c->timeout_light_ms) {
        m.statut_light = STATUT_NA;
        if (++s_to_light >= 2) LedControl_ReportError(ERR_CAPTEUR_TIMEOUT, CAPTEUR_LIGHT);
    } else {
        s_to_light = 0;
        m.statut_light = (m.light < c->light_min || m.light > c->light_max)
                       ? STATUT_INCOHERENT : STATUT_OK;
        if (m.statut_light == STATUT_OK) any_ok = true;
    }

    /* --- GPS --- */
        /* --- GPS --- */
    // On passe le timeout de la config directement à la fonction.
    // Elle renvoie "true" si elle a lu une trame valide (fix OK), "false" sinon.
    bool gps_ok = GroveGPS_ReadGGA(&m.gps, c->timeout_gps_ms);       
    
    if (!gps_ok) {
        m.statut_gps = STATUT_NA;
        if (++s_to_gps >= 2) {
            LedControl_ReportError(ERR_GPS_PAS_FIX, CAPTEUR_GPS);
        }
    } else {
        s_to_gps = 0;
        m.statut_gps = STATUT_OK;
        any_ok = true;
    }

    s_last = m;
    if (out) *out = m;
    return any_ok;
}

const Mesure* CapteurManager_GetLast(void) { return &s_last; }
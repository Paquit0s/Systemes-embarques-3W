#include "capteur_manager.h"
#include "board_pins.h"
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

    /* Scan I2C au démarrage pour diagnostic matériel (M1-7) */
    printf("[I2C SCAN] Recherche peripheriques I2C1 (PB8=SCL, PB9=SDA)...\r\n");

    /* Vérification de l'état de repos des lignes (doivent être à 1 avec pull-up) */
    bool scl_idle = (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_8) == GPIO_PIN_SET);
    bool sda_idle = (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_9) == GPIO_PIN_SET);
    if (!scl_idle || !sda_idle)
    {
        printf("[I2C ALERTE] Ligne(s) I2C bloquee(s) a la masse ! SCL=%d, SDA=%d\r\n", (int)scl_idle, (int)sda_idle);
        printf("[I2C ALERTE] -> Verifiez qu'aucun fil SCL/SDA n'est en court-circuit avec GND ou mal insere.\r\n");
    }

    uint8_t count = 0;
    for (uint8_t addr = 0x08; addr < 0x78; ++addr)
    {
        if (HAL_I2C_IsDeviceReady(&GROVE_I2C_HANDLE, (uint16_t)(addr << 1), 3U, 20U) == HAL_OK)
        {
            const char *label = "";
            if (addr == 0x68) label = " -> RTC DS1307";
            else if (addr == 0x76 || addr == 0x77) label = " -> BME (ou BMP)";
            printf("  - Dispositif detecte a l'adresse 0x%02X%s\r\n", addr, label);
            count++;
        }
    }
    if (count == 0)
    {
        printf("[I2C SCAN] Aucun peripherique detecte ! Verifiez que le switch du Base Shield est sur 5V.\r\n");
    }

    /* Test spécifique de lecture directe registre 0xD0 (Chip ID) sur 0x76 et 0x77 */
    uint8_t chip_id_76 = 0U, chip_id_77 = 0U;
    HAL_StatusTypeDef st_76 = HAL_I2C_Mem_Read(&GROVE_I2C_HANDLE, (0x76U << 1), 0xD0U, I2C_MEMADD_SIZE_8BIT, &chip_id_76, 1U, 50U);
    HAL_StatusTypeDef st_77 = HAL_I2C_Mem_Read(&GROVE_I2C_HANDLE, (0x77U << 1), 0xD0U, I2C_MEMADD_SIZE_8BIT, &chip_id_77, 1U, 50U);

    if (st_76 == HAL_OK)
    {
        printf("[I2C DIAG] Reponse sur 0x76 ! Chip ID lu: 0x%02X\r\n", chip_id_76);
    }
    if (st_77 == HAL_OK)
    {
        printf("[I2C DIAG] Reponse sur 0x77 ! Chip ID lu: 0x%02X\r\n", chip_id_77);
    }
    if (st_76 != HAL_OK && st_77 != HAL_OK)
    {
        printf("[I2C DIAG] Aucune reponse BME sur 0x76 (st=%d, err=0x%lX) ni 0x77 (st=%d, err=0x%lX)\r\n",
               (int)st_76, (unsigned long)GROVE_I2C_HANDLE.ErrorCode,
               (int)st_77, (unsigned long)GROVE_I2C_HANDLE.ErrorCode);
    }

    bool rtc_ok = GroveRTC_Init();
    if (!rtc_ok)
    {
        printf("[ERR] Echec d'initialisation de la RTC DS1307 (0x68).\r\n");
    }

    if (!GroveBME680_Init())
    {
        printf("[WARN] BME680/BME280 non detecte au demarrage (nouvel essai a l'acquisition).\r\n");
    }

    /* P2-3 : Calibration matérielle ADC1 12 bits + configuration canal PA4 */
    GroveLight_Init();
    if (!GroveGPS_Init())
    {
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

    /* --- 2. Capteur environnemental BME680/BME280 (T, HR, P) --- */
    bool want_bme = (c->temp_air_enabled != 0U) || (c->hygr_enabled != 0U) || (c->pressure_enabled != 0U);
    if (!want_bme)
    {
        m.statut_temp  = STATUT_NA;
        m.statut_hum   = STATUT_NA;
        m.statut_press = STATUT_NA;
        s_to_bme       = 0U;
    }
    else
    {
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
                printf("[ERR] BME : 2 timeouts consecutifs -> NA\r\n");
            }
        }
        else
        {
            s_to_bme = 0U;
            m.statut_temp  = (c->temp_air_enabled != 0U)
                           ? statut_from_float(m.env.temp, c->temp_min, c->temp_max)
                           : STATUT_NA;

            /* Cahier des charges p.4 : HYGR_MINT / HYGR_MAXT = plage de température où l'hygrométrie est prise en compte */
            if (c->hygr_enabled == 0U || m.env.temp < c->hygr_mint || m.env.temp > c->hygr_maxt)
            {
                m.statut_hum = STATUT_NA;
            }
            else
            {
                m.statut_hum = statut_from_float(m.env.hum, c->hum_min, c->hum_max);
            }

            m.statut_press = (c->pressure_enabled != 0U)
                           ? statut_from_float(m.env.press, c->press_min, c->press_max)
                           : STATUT_NA;

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
    }

    BSP_Watchdog_Refresh();

    /* --- 3. Capteur de luminosité Grove Light (ADC 12 bits calibré : 0..4095, P2-3) --- */
    if (c->lumin_enabled == 0U)
    {
        m.statut_light = STATUT_NA;
        s_to_light     = 0U;
    }
    else
    {
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
        uint32_t gps_wait = (c->timeout_gps_ms > 300U) ? 300U : c->timeout_gps_ms;
        bool gps_frame_ok = GroveGPS_ReadGGA(&m.gps, gps_wait);
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

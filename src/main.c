#include "setup.h"
#include "board_pins.h"
#include "cesi_types.h"
#include "interruption.h"
#include "mode_manager.h"
#include "led_control.h"
#include "capteur_manager.h"
#include "stockage.h"
#include "config_uart.h"
#include "grove_light.h"
#include "stm32l4xx_hal.h"

#include <stdio.h>
#include <stdbool.h>
#include <string.h>

/* Conforme au Livrable 2 (section 6.1 & 7) : si la RTC échoue au démarrage,
 * signaler l'erreur (LED rouge/bleue 1 Hz) puis arrêter le programme.
 * Passer à 0 uniquement pour déboguer sur carte nue sans module RTC câblé. */
#define RTC_HALT_ON_INIT_FAIL   1

/* P2-5 : Délai du Watchdog matériel IWDG (15 secondes) */
#define WATCHDOG_TIMEOUT_MS     15000U

/* P2-6 : Cadence d'affichage en direct sur UART en mode MAINTENANCE */
#define MAINTENANCE_PERIOD_MS   2000U

static const char *mode_to_str(ModeSysteme m)
{
    switch (m)
    {
    case MODE_STANDARD:      return "STANDARD";
    case MODE_CONFIGURATION: return "CONFIGURATION";
    case MODE_ECO:           return "ECONOMIQUE";
    case MODE_MAINTENANCE:   return "MAINTENANCE";
    default:                 return "INCONNU";
    }
}

static void print_mesure_uart(const Mesure *m)
{
    char ts_str[32];
    char t_str[20], h_str[20], p_str[20], l_str[40], gps_str[48];

    if (m->timestamp.year >= 2000U && m->timestamp.month >= 1U && m->timestamp.day >= 1U)
    {
        snprintf(ts_str, sizeof(ts_str), "%04u-%02u-%02u %02u:%02u:%02u",
                 (unsigned int)m->timestamp.year,
                 (unsigned int)m->timestamp.month,
                 (unsigned int)m->timestamp.day,
                 (unsigned int)m->timestamp.hours,
                 (unsigned int)m->timestamp.minutes,
                 (unsigned int)m->timestamp.seconds);
    }
    else
    {
        strcpy(ts_str, "NA");
    }

    if (m->statut_temp == STATUT_NA)
        strcpy(t_str, "NA");
    else
        snprintf(t_str, sizeof(t_str), "%.2fC%s", (double)m->env.temp,
                 (m->statut_temp == STATUT_INCOHERENT) ? "(!)" : "");

    if (m->statut_hum == STATUT_NA)
        strcpy(h_str, "NA");
    else
        snprintf(h_str, sizeof(h_str), "%.1f%%%s", (double)m->env.hum,
                 (m->statut_hum == STATUT_INCOHERENT) ? "(!)" : "");

    if (m->statut_press == STATUT_NA)
        strcpy(p_str, "NA");
    else
        snprintf(p_str, sizeof(p_str), "%.2fhPa%s", (double)m->env.press,
                 (m->statut_press == STATUT_INCOHERENT) ? "(!)" : "");

    if (m->statut_light == STATUT_NA)
    {
        strcpy(l_str, "NA");
    }
    else
    {
        /* P2-3 : Affichage valeur brute 12 bits + % + estimation lux */
        float pct = GroveLight_RawToPercent(m->light);
        float lux = GroveLight_RawToLux(m->light);
        snprintf(l_str, sizeof(l_str), "%u (%.0f%%/%.0flux)%s",
                 (unsigned int)m->light, (double)pct, (double)lux,
                 (m->statut_light == STATUT_INCOHERENT) ? "(!)" : "");
    }

    if (m->statut_gps == STATUT_NA || !m->gps.valid)
    {
        strcpy(gps_str, "lat=NA lon=NA");
    }
    else
    {
        snprintf(gps_str, sizeof(gps_str), "lat=%.6f lon=%.6f (sat=%u)",
                 m->gps.latitude, m->gps.longitude, (unsigned int)m->gps.satellites);
    }

    printf("[%s] T=%s | HR=%s | P=%s | Lum=%s | %s\r\n",
           ts_str, t_str, h_str, p_str, l_str, gps_str);
}

int main(void)
{
    /* 1. Initialisation matérielle BSP (PLL 80 MHz, I2C1, SPI1, USART2 IT, UART4, ADC1) */
    Global_Init();

    bool iwdg_reset = (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST) != RESET);
    __HAL_RCC_CLEAR_RESET_FLAGS();

    /* 2. Initialisation des services applicatifs */
    ConfigUART_Init();
    Interruption_Init();
    LedControl_Init();
    ModeManager_Init();

    printf("\r\n=== Station Meteo 3W (STM32L476RG @ %lu MHz) ===\r\n",
           (unsigned long)(HAL_RCC_GetSysClockFreq() / 1000000UL));
    if (iwdg_reset)
    {
        printf("[WDT] Reprise automatique apres declenchement du Watchdog IWDG.\r\n");
    }

    /* 3. Initialisation RTC et capteurs (Livrable 2 : arrêt si échec RTC au démarrage) */
    bool rtc_ok = CapteurManager_Init();
    if (!rtc_ok)
    {
        LedControl_ReportError(ERR_RTC_INIT, CAPTEUR_RTC);
#if RTC_HALT_ON_INIT_FAIL
        printf("[FATAL] Arret du systeme suite a l'echec RTC (LED Rouge/Bleu 1 Hz).\r\n");
        while (1)
        {
            LedControl_Update(HAL_GetTick());
        }
#endif
    }

    /* 4. Activation du Watchdog indépendant IWDG (P2-5) */
    if (!BSP_Watchdog_Init(WATCHDOG_TIMEOUT_MS))
    {
        printf("[WARN] Impossible d'initialiser le Watchdog IWDG.\r\n");
    }

    ModeSysteme last_mode = ModeManager_GetMode();
    printf("[t=%lu ms] [MODE] Demarrage en mode %s\r\n",
           (unsigned long)HAL_GetTick(), mode_to_str(last_mode));

    if (last_mode == MODE_CONFIGURATION)
    {
        ConfigUART_FlushInput();
        printf("[CFG] Mode CONFIGURATION (inactivite max: %lu s). Tapez HELP.\r\n",
               (unsigned long)ConfigUART_Get()->config_timeout_s);
    }
    else if (last_mode == MODE_STANDARD || last_mode == MODE_ECO)
    {
        (void)Stockage_Init();
    }

    LedControl_Update(HAL_GetTick());

    uint32_t last_acq_ms   = HAL_GetTick();
    uint32_t mode_enter_ms = last_acq_ms;
    bool     first_acq_pending = (last_mode != MODE_CONFIGURATION);

    /* 5. Boucle principale coopérative (aucun HAL_Delay bloquant) */
    for (;;)
    {
        uint32_t now = HAL_GetTick();
        BSP_Watchdog_Refresh();

        Interruption_Update(now);
        ModeManager_Update(now);
        ModeSysteme mode = ModeManager_GetMode();

        /* P2-6 & P2-7 : Gestion et chronométrage des transitions de mode */
        if (mode != last_mode)
        {
            uint32_t elapsed_ms = now - mode_enter_ms;
            printf("[t=%lu ms] [MODE] Transition %s -> %s (duree mode precedent: %lu ms)\r\n",
                   (unsigned long)now,
                   mode_to_str(last_mode),
                   mode_to_str(mode),
                   (unsigned long)elapsed_ms);

            if (mode == MODE_MAINTENANCE || mode == MODE_CONFIGURATION)
            {
                /* P2-5 / P2-6 : Fermeture propre du fichier SD avant retrait carte ou config */
                Stockage_Close();
                if (mode == MODE_CONFIGURATION)
                {
                    ConfigUART_FlushInput();
                    printf("[CFG] Mode CONFIGURATION actif (inactivite max: %lu s). Tapez HELP.\r\n",
                           (unsigned long)ConfigUART_Get()->config_timeout_s);
                }
            }
            else if (last_mode == MODE_MAINTENANCE || last_mode == MODE_CONFIGURATION)
            {
                /* P2-5 : Réouverture et réinitialisation de la carte SD en sortie de maintenance/config */
                (void)Stockage_Init();
            }

            first_acq_pending = (mode != MODE_CONFIGURATION);
            last_acq_ms       = now;
            mode_enter_ms     = now;
            last_mode         = mode;
        }

        const Config *c = ConfigUART_Get();
        uint32_t period_ms = c->log_interval_s * 1000U;
        if (mode == MODE_ECO)
        {
            period_ms *= 2U; /* P2-6 : Intervalle doublé en mode ECONOMIQUE */
        }

        bool acq_due = (mode == MODE_STANDARD || mode == MODE_ECO) &&
                       (first_acq_pending || ((now - last_acq_ms) >= period_ms));

        bool maint_due = (mode == MODE_MAINTENANCE) &&
                         (first_acq_pending || ((now - last_acq_ms) >= MAINTENANCE_PERIOD_MS));

        if (acq_due || maint_due)
        {
            Mesure m;
            (void)CapteurManager_Acquire(&m);

            if (mode == MODE_STANDARD || mode == MODE_ECO)
            {
                if (Stockage_LogMesure(&m))
                {
                    printf("[%s | LOG SD] ", (mode == MODE_ECO) ? "ECO" : "STD");
                }
                else
                {
                    printf("[%s | ERR SD] ", (mode == MODE_ECO) ? "ECO" : "STD");
                }
                print_mesure_uart(&m);
            }
            else
            {
                /* P2-6 : Mode MAINTENANCE : affichage live UART uniquement, pas d'écriture SD */
                printf("[MAINT] ");
                print_mesure_uart(&m);
            }

            first_acq_pending = false;
            last_acq_ms = HAL_GetTick();
        }

        /* P2-4 & P2-6 : Console non bloquante active uniquement en MODE_CONFIGURATION */
        if (mode == MODE_CONFIGURATION)
        {
            ConfigUART_Process();
        }

        LedControl_Update(HAL_GetTick());
    }
}

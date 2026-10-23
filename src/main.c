#include "setup.h"
#include "board_pins.h"
#include "cesi_types.h"
#include "interruption.h"
#include "mode_manager.h"
#include "led_control.h"
#include "capteur_manager.h"
#include "stockage.h"
#include "config_uart.h"
#include "stm32l4xx_hal.h"

#include <stdio.h> 
#include <math.h>  
#include <stdbool.h> // CORRECTION 1 : Ajout de stdbool.h

static void print_mesure_uart(const Mesure *m) {
    extern UART_HandleTypeDef huart2;
    char buf[128];
    
    // CORRECTION 2 : Remplacement de NAN par 0.0f (évite les bugs IntelliSense)
    // CORRECTION 3 : Ajout de (unsigned int) pour le %u de la lumière
    int n = snprintf(buf, sizeof(buf),
        "%04u-%02u-%02u %02u:%02u:%02u  T=%.1f  H=%.1f  P=%.1f  L=%u  "
        "lat=%.6f lon=%.6f\r\n",
        m->timestamp.year, m->timestamp.month, m->timestamp.day,
        m->timestamp.hour, m->timestamp.minute, m->timestamp.second,
        (m->statut_temp ==STATUT_OK)?m->env.temp:0.0f,
        (m->statut_hum  ==STATUT_OK)?m->env.hum:0.0f,
        (m->statut_press==STATUT_OK)?m->env.press:0.0f,
        (m->statut_light==STATUT_OK)?(unsigned int)m->light:0,
        (m->statut_gps  ==STATUT_OK)?m->gps.latitude:0.0f,
        (m->statut_gps  ==STATUT_OK)?m->gps.longitude:0.0f);
        
    if (n > 0) HAL_UART_Transmit(&huart2, (uint8_t*)buf, n, 100);
}

int main(void) {
    Global_Init();            

    ConfigUART_Init();
    Interruption_Init();
    LedControl_Init();
    ModeManager_Init();       
    CapteurManager_Init();

    ModeSysteme m0 = ModeManager_GetMode();
    if (m0 == MODE_STANDARD || m0 == MODE_ECO) Stockage_Init();

    uint32_t last_acq_ms = 0;
    ModeSysteme last_mode = m0;
    uint32_t now = HAL_GetTick();

    for (;;) {
        now = HAL_GetTick();

        Interruption_Update(now);
        ModeManager_Update(now);
        ModeSysteme mode = ModeManager_GetMode();

        if (mode != last_mode) {
            if (mode == MODE_MAINTENANCE || mode == MODE_CONFIGURATION) {
                Stockage_Close();     
            } else {
                Stockage_Init();
                last_acq_ms = 0;      
            }
            last_mode = mode;
        }

        const Config *c = ConfigUART_Get();
        uint32_t period_ms = c->log_interval_s * 1000U;
        if (mode == MODE_ECO) period_ms *= 2U;

        bool acq_due = (mode == MODE_STANDARD || mode == MODE_ECO) &&
                       (now - last_acq_ms >= period_ms);

        bool maint_due = (mode == MODE_MAINTENANCE) &&
                         (now - last_acq_ms >= 1000U);  

        if (acq_due || maint_due) {
            Mesure m;
            CapteurManager_Acquire(&m);
            if (mode == MODE_STANDARD || mode == MODE_ECO) {
                Stockage_LogMesure(&m);
            } else {
                print_mesure_uart(&m);
            }
            last_acq_ms = now;
        }

        if (mode == MODE_CONFIGURATION || mode == MODE_MAINTENANCE) {
            ConfigUART_Process();
        }

        LedControl_Update(now);
    }
}
#ifndef SETUP_H
#define SETUP_H

#include <stdint.h>
#include <stdbool.h>

void Global_Init(void);
void SystemClock_Config(void);

/* Phase 2 (P2-1 / P2-2) : Changement dynamique du prescaler SPI1 pour la carte SD */
bool BSP_SPI1_SetPrescaler(uint32_t BaudRatePrescaler);

/* Phase 2 (P2-5) : Watchdog indépendant matériel (IWDG) */
bool BSP_Watchdog_Init(uint32_t timeout_ms);
void BSP_Watchdog_Refresh(void);

#endif /* SETUP_H */

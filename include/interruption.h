#ifndef INTERRUPTION_H
#define INTERRUPTION_H

#include <stdbool.h>
#include "cesi_types.h"

void    Interruption_Init(void);
void    Interruption_Update(uint32_t now_ms);
bool    Interruption_PopEvent(Event *out);
bool    Interruption_IsHeld(ButtonId id);   /* pour boot CONFIG */
uint32_t Interruption_HeldMs(ButtonId id);  /* durée appui en cours */

#endif
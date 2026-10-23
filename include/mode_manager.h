#ifndef MODE_MANAGER_H
#define MODE_MANAGER_H

#include <stdbool.h>
#include "cesi_types.h"

void         ModeManager_Init(void);
void         ModeManager_Update(uint32_t now_ms);
ModeSysteme  ModeManager_GetMode(void);
ModeSysteme  ModeManager_GetPrevious(void);
bool         ModeManager_RequestMode(ModeSysteme target);
void         ModeManager_NotifyActivity(void);  /* reset du timer CONFIG */

#endif
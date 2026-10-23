#ifndef LED_CONTROL_H
#define LED_CONTROL_H

#include <stdbool.h>
#include "cesi_types.h"

void LedControl_Init(void);
void LedControl_Update(uint32_t now_ms);
void LedControl_ReportError(CodeErreur err, CapteurId cap);
void LedControl_ClearError(CodeErreur err);

#endif
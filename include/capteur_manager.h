#ifndef CAPTEUR_MANAGER_H
#define CAPTEUR_MANAGER_H

#include <stdbool.h>
#include "cesi_types.h"

bool CapteurManager_Init(void);
bool CapteurManager_Acquire(Mesure *out);   /* true si au moins une grandeur OK */
const Mesure* CapteurManager_GetLast(void);

#endif
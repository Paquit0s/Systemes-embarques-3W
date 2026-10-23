#ifndef STOCKAGE_H
#define STOCKAGE_H

#include <stdbool.h>
#include "cesi_types.h"

bool Stockage_Init(void);
bool Stockage_LogMesure(const Mesure *m);
void Stockage_Flush(void);
void Stockage_Close(void);      /* MAINTENANCE : fermer proprement */

#endif
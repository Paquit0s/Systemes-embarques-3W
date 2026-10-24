#ifndef GROVE_GPS_AIR530Z_H
#define GROVE_GPS_AIR530Z_H

#include <stdbool.h>
#include "cesi_types.h"

bool GroveGPS_Init(void);
bool GroveGPS_ReadLine(char *buffer, unsigned int max_len, unsigned int timeout_ms);
bool GroveGPS_ReadGGA(GPS_Data *gps, unsigned int timeout_ms);

/* Hooks d'interruption UART4 pour le ring buffer GPS (P1-12) */
void GroveGPS_RxCpltHook(void);
void GroveGPS_ErrorHook(void);

#endif

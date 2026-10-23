#ifndef CONFIG_UART_H
#define CONFIG_UART_H

#include <stdbool.h>
#include "cesi_types.h"

void ConfigUART_Init(void);
void ConfigUART_Process(void);      /* à appeler depuis la boucle principale */
const Config* ConfigUART_Get(void);
bool ConfigUART_Save(void);         /* écriture flash */
bool ConfigUART_Load(void);         /* lecture flash au boot */

#endif
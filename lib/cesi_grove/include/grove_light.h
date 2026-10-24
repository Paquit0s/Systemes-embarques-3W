#ifndef GROVE_LIGHT_H
#define GROVE_LIGHT_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Plage de sortie de l'ADC1 12 bits (P2-3)
 *        Le capteur Grove Light Sensor v1.3 (LS06-S phototransistor) connecté
 *        sur PA4 (ADC1_IN9) délivre une tension analogique de 0 à 3,3 V
 *        numérisée sur 12 bits : 0 à 4095.
 *
 * Seuils de référence documentés (P2-3) :
 *   - 0    .. 100   : Obscurité (< ~3 lux)
 *   - 101  .. 800   : Faible luminosité / intérieur sombre (~3 .. 50 lux)
 *   - 801  .. 2500  : Éclairage intérieur normal (~50 .. 400 lux)
 *   - 2501 .. 4095  : Forte luminosité / plein jour (> 400 lux)
 */
#define GROVE_LIGHT_ADC_MAX     4095U
#define GROVE_LIGHT_RAW_ERROR   0xFFFFU

void     GroveLight_Init(void);
uint16_t GroveLight_ReadRaw(void);
bool     GroveLight_Read(uint16_t *raw_out);
float    GroveLight_RawToPercent(uint16_t raw);
float    GroveLight_RawToLux(uint16_t raw);

#endif /* GROVE_LIGHT_H */

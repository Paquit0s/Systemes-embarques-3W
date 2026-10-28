#ifndef GROVE_GPS_AIR530Z_H
#define GROVE_GPS_AIR530Z_H

#include <stdbool.h>
#include "cesi_types.h"

/**
 * @brief Initialise le pilote GPS Air530Z (ring buffer UART4, interruption RX).
 * @return true si l'UART4 est valide et l'interruption RX armee, false sinon.
 */
bool GroveGPS_Init(void);

/**
 * @brief Vide le ring buffer de réception UART du GPS.
 */
void GroveGPS_Flush(void);

/**
 * @brief Lit une ligne NMEA complète depuis le ring buffer.
 * @param buffer Tampon de sortie.
 * @param max_len Taille maximale du tampon.
 * @param timeout_ms Délai d'attente maximum en millisecondes.
 * @return true si une ligne NMEA complète a été lue.
 */
bool GroveGPS_ReadLine(char *buffer, unsigned int max_len, unsigned int timeout_ms);

/**
 * @brief Recherche et extrait une trame GGA ($GPGGA, $GNGGA...) depuis le ring buffer,
 *        puis réalise le parsing complet des données GPS (UTC, fix, satellites, lat/lon, alt, hdop).
 * @param gps Pointeur vers la structure GPS_Data à remplir.
 * @param timeout_ms Délai d'attente maximum en millisecondes.
 * @return true si une trame GGA a été reçue et parsée avec succès (même sans fix satellite).
 */
bool GroveGPS_ReadGGA(GPS_Data *gps, unsigned int timeout_ms);

/**
 * @brief Gestionnaire d'interruption UART4 appelé à chaque octet reçu du GPS.
 */
void GroveGPS_UART_IRQHandler(void);

#endif

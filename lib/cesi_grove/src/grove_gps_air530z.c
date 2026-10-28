#include "grove_gps_air530z.h"
#include "board_pins.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>

/* =========================================================================
 * TAMPON CIRCULAIRE (RING BUFFER) DE RÉCEPTION UART4
 * ========================================================================= */
#define GPS_RING_BUF_SIZE 512U

static volatile uint8_t  s_rx_buf[GPS_RING_BUF_SIZE];
static volatile uint16_t s_rx_head     = 0U;
static volatile uint16_t s_rx_tail     = 0U;
static volatile bool     s_rx_overflow = false;

/**
 * @brief Gestionnaire d'interruption UART4 appelé à chaque octet reçu du GPS
 */
void GroveGPS_UART_IRQHandler(void)
{
    /* Vérification de la réception d'un octet (RXNE) */
    if (__HAL_UART_GET_FLAG(&GPS_UART_HANDLE, UART_FLAG_RXNE) != RESET)
    {
        uint8_t c = (uint8_t)(GPS_UART_HANDLE.Instance->RDR & 0xFFU);
        uint16_t next = (s_rx_head + 1U) % GPS_RING_BUF_SIZE;
        if (next == s_rx_tail)
        {
            /* En cas de saturation du tampon, on écrase l'octet le plus ancien
             * pour toujours conserver le flux NMEA le plus récent */
            s_rx_tail = (s_rx_tail + 1U) % GPS_RING_BUF_SIZE;
            s_rx_overflow = true;
        }
        s_rx_buf[s_rx_head] = c;
        s_rx_head = next;
    }

    /* Acquittement préventif des drapeaux d'erreur de la liaison série */
    if (__HAL_UART_GET_FLAG(&GPS_UART_HANDLE, UART_FLAG_ORE) != RESET)
    {
        __HAL_UART_CLEAR_OREFLAG(&GPS_UART_HANDLE);
    }
    if (__HAL_UART_GET_FLAG(&GPS_UART_HANDLE, UART_FLAG_FE) != RESET)
    {
        __HAL_UART_CLEAR_FEFLAG(&GPS_UART_HANDLE);
    }
    if (__HAL_UART_GET_FLAG(&GPS_UART_HANDLE, UART_FLAG_NE) != RESET)
    {
        __HAL_UART_CLEAR_NEFLAG(&GPS_UART_HANDLE);
    }
}

/**
 * @brief Routine d'interruption UART4 pour le vecteur Cortex-M4 (surcharge du symbole weak CMSIS)
 */
void UART4_IRQHandler(void)
{
    GroveGPS_UART_IRQHandler();
}

/**
 * @brief Extraction atomique d'un octet depuis le ring buffer
 */
static bool gps_ring_pop(uint8_t *out)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    if (s_rx_head == s_rx_tail)
    {
        __set_PRIMASK(primask);
        return false;
    }

    *out = s_rx_buf[s_rx_tail];
    s_rx_tail = (s_rx_tail + 1U) % GPS_RING_BUF_SIZE;

    __set_PRIMASK(primask);
    return true;
}

void GroveGPS_Flush(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    s_rx_head     = 0U;
    s_rx_tail     = 0U;
    s_rx_overflow = false;

    __set_PRIMASK(primask);
}

bool GroveGPS_Init(void)
{
    /* Vérification que le handle UART4 est bien initialisé par setup.c */
    if (GPS_UART_HANDLE.Instance == NULL)
    {
        return false;
    }

    /* Réinitialisation du tampon circulaire */
    GroveGPS_Flush();

    /* Configuration NVIC de la priorité et activation de l'interruption UART4 */
    HAL_NVIC_SetPriority(UART4_IRQn, 2U, 0U);
    HAL_NVIC_EnableIRQ(UART4_IRQn);

    /* Acquittement des erreurs résiduelles avant armement */
    __HAL_UART_CLEAR_OREFLAG(&GPS_UART_HANDLE);
    __HAL_UART_CLEAR_FEFLAG(&GPS_UART_HANDLE);
    __HAL_UART_CLEAR_NEFLAG(&GPS_UART_HANDLE);

    /* Activation de l'interruption de réception (RXNE) */
    __HAL_UART_ENABLE_IT(&GPS_UART_HANDLE, UART_IT_RXNE);

    return true;
}

/* =========================================================================
 * FONCTIONS DE LECTURE ET PARSING NMEA
 * ========================================================================= */

/**
 * @brief Conversion NMEA (ddmm.mmmm ou dddmm.mmmm) vers coordonnées décimales
 */
static double nmea_to_decimal(const char *value, const char hemisphere)
{
    if (value == NULL || value[0] == '\0') return 0.0;

    double raw = atof(value);
    int degrees = (int)(raw / 100.0);
    double minutes = raw - ((double)degrees * 100.0);
    double decimal = (double)degrees + (minutes / 60.0);

    if (hemisphere == 'S' || hemisphere == 's' || hemisphere == 'W' || hemisphere == 'w')
    {
        decimal = -decimal;
    }

    return decimal;
}

/**
 * @brief Vérification de la somme de contrôle NMEA (*HH en fin de trame)
 */
static bool nmea_verify_checksum(const char *sentence)
{
    if (sentence == NULL || sentence[0] != '$') return false;

    const char *p = sentence + 1;
    uint8_t calculated = 0U;

    while (*p != '\0' && *p != '*' && *p != '\r' && *p != '\n')
    {
        calculated ^= (uint8_t)(*p);
        p++;
    }

    /* Si pas d'astérisque de checksum, on accepte par défaut */
    if (*p != '*')
    {
        return true;
    }

    p++; /* Sauter le '*' */
    if (p[0] == '\0' || p[1] == '\0')
    {
        return false;
    }

    char hex[3] = { p[0], p[1], '\0' };
    char *endptr = NULL;
    unsigned long expected = strtoul(hex, &endptr, 16);
    if (endptr == hex)
    {
        return false;
    }

    return (calculated == (uint8_t)expected);
}

/**
 * @brief Détection d'une trame de type GGA ($GPGGA, $GNGGA, $GAGGA, etc.)
 */
static bool is_gga_sentence(const char *line)
{
    if (line == NULL || line[0] != '$') return false;

    const char *comma = strchr(line, ',');
    if (comma == NULL) return false;

    ptrdiff_t tag_len = comma - line;
    if (tag_len < 5) return false;

    return (strncmp(comma - 3, "GGA", 3) == 0);
}

/**
 * @brief Découpage in-situ des champs NMEA séparés par des virgules
 *        (gère rigoureusement les champs vides sans les fusionner, contrairement à strtok)
 */
static int split_nmea_fields(char *str, char *fields[], int max_fields)
{
    if (str == NULL || max_fields <= 0) return 0;

    int count = 0;
    fields[count++] = str;

    while (*str != '\0' && count < max_fields)
    {
        if (*str == '*' || *str == '\r' || *str == '\n')
        {
            *str = '\0';
            break;
        }
        if (*str == ',')
        {
            *str = '\0';
            fields[count++] = str + 1;
        }
        str++;
    }
    return count;
}

bool GroveGPS_ReadLine(char *buffer, unsigned int max_len, unsigned int timeout_ms)
{
    if (buffer == NULL || max_len == 0) return false;

    unsigned int count = 0;
    uint32_t start = HAL_GetTick();

    while ((HAL_GetTick() - start) < timeout_ms)
    {
        uint8_t c = 0;
        if (gps_ring_pop(&c))
        {
            if (c == '\n')
            {
                if (count > 0 && buffer[count - 1] == '\r')
                {
                    count--;
                }
                buffer[count] = '\0';
                return (count > 0);
            }

            if (c == '$')
            {
                /* Début d'une nouvelle phrase NMEA : réalignement immédiat */
                count = 0;
                buffer[count++] = (char)c;
            }
            else if (count > 0 && count < (max_len - 1))
            {
                if (c != '\r')
                {
                    buffer[count++] = (char)c;
                }
            }
        }
    }

    buffer[count] = '\0';
    return false;
}

bool GroveGPS_ReadGGA(GPS_Data *gps, unsigned int timeout_ms)
{
    if (gps == NULL) return false;

    char line[96];
    uint32_t start = HAL_GetTick();

    while ((HAL_GetTick() - start) < timeout_ms)
    {
        uint32_t elapsed = HAL_GetTick() - start;
        uint32_t remaining = (elapsed < timeout_ms) ? (timeout_ms - elapsed) : 0U;
        if (remaining == 0U)
        {
            break;
        }

        uint32_t line_to = remaining > 250U ? 250U : remaining;

        if (!GroveGPS_ReadLine(line, sizeof(line), line_to))
        {
            continue;
        }

        if (!is_gga_sentence(line))
        {
            continue;
        }

        if (!nmea_verify_checksum(line))
        {
            continue;
        }

        /* Trame GGA valide identifiée : initialisation et parsing */
        memset(gps, 0, sizeof(GPS_Data));

        strncpy(gps->raw_sentence, line, sizeof(gps->raw_sentence) - 1);
        gps->raw_sentence[sizeof(gps->raw_sentence) - 1] = '\0';

        char copy[96];
        strncpy(copy, line, sizeof(copy) - 1);
        copy[sizeof(copy) - 1] = '\0';

        char *fields[16] = {0};
        int count = split_nmea_fields(copy, fields, 16);

        if (count < 8)
        {
            continue;
        }

        /* 1. Heure UTC (ex: "092750.000" ou "123519") */
        if (fields[1] != NULL && strlen(fields[1]) >= 6)
        {
            uint8_t h = (uint8_t)((fields[1][0] - '0') * 10 + (fields[1][1] - '0'));
            uint8_t m = (uint8_t)((fields[1][2] - '0') * 10 + (fields[1][3] - '0'));
            uint8_t s = (uint8_t)((fields[1][4] - '0') * 10 + (fields[1][5] - '0'));
            if (h < 24 && m < 60 && s < 60)
            {
                gps->utc_hour = h;
                gps->utc_min  = m;
                gps->utc_sec  = s;
                snprintf(gps->utc_time, sizeof(gps->utc_time), "%02u:%02u:%02u", h, m, s);
            }
        }

        /* 2. Qualité du fix & nombre de satellites */
        gps->fix_quality = (fields[6] != NULL && fields[6][0] != '\0') ? (uint8_t)atoi(fields[6]) : 0U;
        gps->satellites  = (fields[7] != NULL && fields[7][0] != '\0') ? (uint8_t)atoi(fields[7]) : 0U;
        gps->valid       = (gps->fix_quality > 0U);

        /* 3. Coordonnées Latitude & Longitude */
        if (gps->valid && fields[2] != NULL && fields[2][0] != '\0' &&
            fields[3] != NULL && fields[3][0] != '\0')
        {
            gps->latitude = nmea_to_decimal(fields[2], fields[3][0]);
        }
        else
        {
            gps->latitude = 0.0;
        }

        if (gps->valid && fields[4] != NULL && fields[4][0] != '\0' &&
            fields[5] != NULL && fields[5][0] != '\0')
        {
            gps->longitude = nmea_to_decimal(fields[4], fields[5][0]);
        }
        else
        {
            gps->longitude = 0.0;
        }

        /* 4. Précision HDOP et Altitude */
        if (count > 8 && fields[8] != NULL && fields[8][0] != '\0')
        {
            gps->hdop = (float)atof(fields[8]);
        }
        if (count > 9 && fields[9] != NULL && fields[9][0] != '\0')
        {
            gps->altitude_m = (float)atof(fields[9]);
        }

        return true;
    }

    return false;
}
#include "grove_gps_air530z.h"
#include "board_pins.h"
#include "setup.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>

/* =========================================================================
 * P1-12 : Ring buffer UART4 sur interruption pour réception NMEA continue
 * ========================================================================= */
#define GPS_RX_RING_SIZE  256U

static volatile uint8_t  s_gps_ring[GPS_RX_RING_SIZE];
static volatile uint32_t s_gps_head = 0U;
static volatile uint32_t s_gps_tail = 0U;
static uint8_t           s_gps_rx_byte = 0U;

void GroveGPS_RxCpltHook(void)
{
    uint32_t next = (s_gps_head + 1U) % GPS_RX_RING_SIZE;
    if (next != s_gps_tail)
    {
        s_gps_ring[s_gps_head] = s_gps_rx_byte;
        s_gps_head = next;
    }
    (void)HAL_UART_Receive_IT(&GPS_UART_HANDLE, &s_gps_rx_byte, 1U);
}

void GroveGPS_ErrorHook(void)
{
    __HAL_UART_CLEAR_OREFLAG(&GPS_UART_HANDLE);
    __HAL_UART_CLEAR_NEFLAG(&GPS_UART_HANDLE);
    __HAL_UART_CLEAR_FEFLAG(&GPS_UART_HANDLE);
    (void)HAL_UART_Receive_IT(&GPS_UART_HANDLE, &s_gps_rx_byte, 1U);
}

void GroveGPS_Flush(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    s_gps_head = 0U;
    s_gps_tail = 0U;

    __set_PRIMASK(primask);
}

static bool gps_ring_pop(uint8_t *out)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    if (s_gps_tail == s_gps_head)
    {
        __set_PRIMASK(primask);
        return false;
    }
    *out = s_gps_ring[s_gps_tail];
    s_gps_tail = (s_gps_tail + 1U) % GPS_RX_RING_SIZE;

    __set_PRIMASK(primask);
    return true;
}

static double nmea_to_decimal(const char *value, const char hemisphere)
{
    if (value == NULL || value[0] == '\0') return 0.0;

    double raw = atof(value);
    int degrees = (int)(raw / 100.0);
    double minutes = raw - (degrees * 100.0);
    double decimal = degrees + (minutes / 60.0);

    if (hemisphere == 'S' || hemisphere == 's' || hemisphere == 'W' || hemisphere == 'w')
        decimal = -decimal;

    return decimal;
}

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

    if (*p != '*')
    {
        return true;
    }

    p++;
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

static void parse_utc_time(const char *utc_field, GPS_Data *gps)
{
    gps->utc_hours   = 0U;
    gps->utc_minutes = 0U;
    gps->utc_seconds = 0U;
    gps->utc_hour    = 0U;
    gps->utc_min     = 0U;
    gps->utc_sec     = 0U;
    gps->utc_time[0] = '\0';

    if (utc_field != NULL && strlen(utc_field) >= 6U)
    {
        unsigned int raw_t = (unsigned int)atoi(utc_field);
        uint8_t h = (uint8_t)((raw_t / 10000U) % 24U);
        uint8_t m = (uint8_t)((raw_t / 100U) % 100U);
        uint8_t s = (uint8_t)(raw_t % 100U);

        gps->utc_hours   = h;
        gps->utc_minutes = m;
        gps->utc_seconds = s;
        gps->utc_hour    = h;
        gps->utc_min     = m;
        gps->utc_sec     = s;
        snprintf(gps->utc_time, sizeof(gps->utc_time), "%02u:%02u:%02u", h, m, s);
    }
}

bool GroveGPS_Init(void)
{
    if (GPS_UART_HANDLE.Instance == NULL)
    {
        return false;
    }

    GroveGPS_Flush();

    HAL_StatusTypeDef st = HAL_UART_Receive_IT(&GPS_UART_HANDLE, &s_gps_rx_byte, 1U);
    return (st == HAL_OK || st == HAL_BUSY);
}

bool GroveGPS_ReadLine(char *buffer, unsigned int max_len, unsigned int timeout_ms)
{
    if (buffer == NULL || max_len == 0U) return false;

    unsigned int count = 0U;
    uint8_t c = 0U;
    uint32_t start = HAL_GetTick();

    while ((HAL_GetTick() - start) < timeout_ms && count < (max_len - 1U))
    {
        BSP_Watchdog_Refresh();

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
                count = 0;
                buffer[count++] = (char)c;
            }
            else if (count > 0 && count < (max_len - 1U))
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
        BSP_Watchdog_Refresh();

        uint32_t elapsed = HAL_GetTick() - start;
        uint32_t rem = (elapsed < timeout_ms) ? (timeout_ms - elapsed) : 0U;
        if (rem == 0U) break;
        if (rem > 500U) rem = 500U;

        if (!GroveGPS_ReadLine(line, sizeof(line), rem))
            continue;

        if (strncmp(line, "$GPGGA", 6) != 0 && strncmp(line, "$GNGGA", 6) != 0)
            continue;

        if (!nmea_verify_checksum(line))
            continue;

        memset(gps, 0, sizeof(GPS_Data));

        strncpy(gps->raw_sentence, line, sizeof(gps->raw_sentence) - 1U);
        gps->raw_sentence[sizeof(gps->raw_sentence) - 1U] = '\0';

        char copy[96];
        strncpy(copy, line, sizeof(copy) - 1U);
        copy[sizeof(copy) - 1U] = '\0';

        /* Découpage champ par champ préservant les colonnes vides */
        char *fields[16] = {0};
        int field_count = 0;
        char *cursor = copy;
        fields[field_count++] = cursor;

        while (*cursor != '\0' && field_count < 16)
        {
            if (*cursor == ',' || *cursor == '*')
            {
                char sep = *cursor;
                *cursor = '\0';
                if (sep == '*')
                {
                    break;
                }
                fields[field_count++] = cursor + 1;
            }
            ++cursor;
        }

        if (field_count < 8)
            continue;

        /* P1-12 : Parsing GGA complet (UTC, fix, satellites, lat/lon, hdop, alt) */
        parse_utc_time(fields[1], gps);
        gps->fix_quality = (fields[6] && fields[6][0] != '\0') ? (uint8_t)atoi(fields[6]) : 0U;
        gps->satellites  = (fields[7] && fields[7][0] != '\0') ? (uint8_t)atoi(fields[7]) : 0U;
        gps->valid       = (gps->fix_quality > 0U &&
                            fields[2] && fields[2][0] != '\0' &&
                            fields[4] && fields[4][0] != '\0');

        if (gps->valid)
        {
            gps->latitude  = nmea_to_decimal(fields[2], (fields[3] && fields[3][0]) ? fields[3][0] : 'N');
            gps->longitude = nmea_to_decimal(fields[4], (fields[5] && fields[5][0]) ? fields[5][0] : 'E');
        }
        else
        {
            gps->latitude  = 0.0;
            gps->longitude = 0.0;
        }

        if (field_count > 8 && fields[8] != NULL && fields[8][0] != '\0')
        {
            gps->hdop = (float)atof(fields[8]);
        }
        if (field_count > 9 && fields[9] != NULL && fields[9][0] != '\0')
        {
            gps->altitude_m = (float)atof(fields[9]);
        }

        return true;
    }

    return false;
}

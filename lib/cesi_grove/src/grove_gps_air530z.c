#include "grove_gps_air530z.h"
#include "board_pins.h"
#include "setup.h"
#include <string.h>
#include <stdlib.h>

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

static bool gps_ring_pop(uint8_t *out)
{
    if (s_gps_tail == s_gps_head)
    {
        return false;
    }
    *out = s_gps_ring[s_gps_tail];
    s_gps_tail = (s_gps_tail + 1U) % GPS_RX_RING_SIZE;
    return true;
}

static double nmea_to_decimal(const char *value, const char hemisphere)
{
    if (value == NULL || value[0] == '\0') return 0.0;

    double raw = atof(value);
    int degrees = (int)(raw / 100.0);
    double minutes = raw - (degrees * 100.0);
    double decimal = degrees + (minutes / 60.0);

    if (hemisphere == 'S' || hemisphere == 'W')
        decimal = -decimal;

    return decimal;
}

static void parse_utc_time(const char *utc_field, GPS_Data *gps)
{
    gps->utc_hours   = 0U;
    gps->utc_minutes = 0U;
    gps->utc_seconds = 0U;

    if (utc_field != NULL && strlen(utc_field) >= 6U)
    {
        unsigned int raw_t = (unsigned int)atoi(utc_field);
        gps->utc_hours   = (uint8_t)((raw_t / 10000U) % 24U);
        gps->utc_minutes = (uint8_t)((raw_t / 100U) % 100U);
        gps->utc_seconds = (uint8_t)(raw_t % 100U);
    }
}

bool GroveGPS_Init(void)
{
    if (GPS_UART_HANDLE.Instance == NULL)
    {
        return false;
    }

    s_gps_head = 0U;
    s_gps_tail = 0U;

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
                buffer[count] = '\0';
                return true;
            }
            if (c != '\r')
            {
                buffer[count++] = (char)c;
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

        strncpy(gps->raw_sentence, line, sizeof(gps->raw_sentence) - 1U);
        gps->raw_sentence[sizeof(gps->raw_sentence) - 1U] = '\0';

        char copy[96];
        strncpy(copy, line, sizeof(copy));
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

        /* P1-12 : Parsing GGA complet (UTC, fix, satellites, lat/lon) */
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

        return true;
    }

    return false;
}

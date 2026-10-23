#include "grove_gps_air530z.h"
#include "board_pins.h"
#include <string.h>
#include <stdlib.h>

static double nmea_to_decimal(const char *value, const char hemisphere)
{
    if (value == 0 || value[0] == '\0') return 0.0;

    double raw = atof(value);
    int degrees = (int)(raw / 100.0);
    double minutes = raw - (degrees * 100.0);
    double decimal = degrees + (minutes / 60.0);

    if (hemisphere == 'S' || hemisphere == 'W')
        decimal = -decimal;

    return decimal;
}

void GroveGPS_Init(void)
{
    /*
     * L'initialisation matérielle de l'UART4 (GPS_UART_HANDLE = huart4,
     * 9600 bauds, pin swap PA0/PA1) est désormais centralisée dans setup.c.
     */
}

bool GroveGPS_ReadLine(char *buffer, unsigned int max_len, unsigned int timeout_ms)
{
    if (buffer == 0 || max_len == 0) return false;

    unsigned int count = 0;
    uint8_t c = 0;
    uint32_t start = HAL_GetTick();

    while ((HAL_GetTick() - start) < timeout_ms && count < (max_len - 1))
    {
        if (HAL_UART_Receive(&GPS_UART_HANDLE, &c, 1, 10) == HAL_OK)
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
    return count > 0;
}

bool GroveGPS_ReadGGA(GPS_Data *gps, unsigned int timeout_ms)
{
    if (gps == 0) return false;

    char line[96];
    uint32_t start = HAL_GetTick();

    while ((HAL_GetTick() - start) < timeout_ms)
    {
        if (!GroveGPS_ReadLine(line, sizeof(line), 500))
            continue;

        if (strncmp(line, "$GPGGA", 6) != 0 && strncmp(line, "$GNGGA", 6) != 0)
            continue;

        strncpy(gps->raw_sentence, line, sizeof(gps->raw_sentence) - 1);
        gps->raw_sentence[sizeof(gps->raw_sentence) - 1] = '\0';

        char copy[96];
        strncpy(copy, line, sizeof(copy));
        copy[sizeof(copy) - 1] = '\0';

        char *fields[16] = {0};
        int i = 0;
        char *token = strtok(copy, ",");

        while (token != 0 && i < 16)
        {
            fields[i++] = token;
            token = strtok(0, ",");
        }

        if (i < 8) return false;

        gps->latitude = nmea_to_decimal(fields[2], fields[3] ? fields[3][0] : 'N');
        gps->longitude = nmea_to_decimal(fields[4], fields[5] ? fields[5][0] : 'E');
        gps->fix_quality = fields[6] ? (uint8_t)atoi(fields[6]) : 0;
        gps->satellites = fields[7] ? (uint8_t)atoi(fields[7]) : 0;
        gps->valid = gps->fix_quality > 0;

        return true;
    }

    return false;
}
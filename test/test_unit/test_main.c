#include <unity.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "cesi_types.h"

#ifdef STM32L476xx
#include "stm32l4xx_hal.h"

static UART_HandleTypeDef huart2_test;

void SysTick_Handler(void)
{
    HAL_IncTick();
}

void unityOutputStart(unsigned long baudrate)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART2_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin       = GPIO_PIN_2 | GPIO_PIN_3;
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_NOPULL;
    gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF7_USART2;
    HAL_GPIO_Init(GPIOA, &gpio);

    huart2_test.Instance          = USART2;
    huart2_test.Init.BaudRate     = (uint32_t)baudrate;
    huart2_test.Init.WordLength   = UART_WORDLENGTH_8B;
    huart2_test.Init.StopBits     = UART_STOPBITS_1;
    huart2_test.Init.Parity       = UART_PARITY_NONE;
    huart2_test.Init.Mode         = UART_MODE_TX_RX;
    huart2_test.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart2_test.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&huart2_test);
}

void unityOutputChar(unsigned int c)
{
    uint32_t timeout = 100000U;
    while (((USART2->ISR & USART_ISR_TXE) == 0U) && (--timeout > 0U)) {}
    USART2->TDR = (uint8_t)c;
}

void unityOutputFlush(void)
{
    uint32_t timeout = 100000U;
    while (((USART2->ISR & USART_ISR_TC) == 0U) && (--timeout > 0U)) {}
}

void unityOutputComplete(void)
{
    unityOutputFlush();
}

#else
void unityOutputStart(unsigned long baudrate) { (void)baudrate; }
void unityOutputChar(unsigned int c) { putchar((int)c); }
void unityOutputFlush(void) { fflush(stdout); }
void unityOutputComplete(void) { fflush(stdout); }
#endif

/* =========================================================================
 * P3-1 : Fonctions unitaires testées (BCD, NMEA, Validation Config, CSV)
 * ========================================================================= */

static uint8_t bcd_to_dec(uint8_t bcd)
{
    return (uint8_t)(((bcd >> 4) * 10U) + (bcd & 0x0FU));
}

static uint8_t dec_to_bcd(uint8_t dec)
{
    return (uint8_t)(((dec / 10U) << 4) | (dec % 10U));
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

    if (*p != '*') return true;
    p++;
    if (p[0] == '\0' || p[1] == '\0') return false;

    char hex[3] = { p[0], p[1], '\0' };
    char *endptr = NULL;
    unsigned long expected = strtoul(hex, &endptr, 16);
    if (endptr == hex) return false;

    return (calculated == (uint8_t)expected);
}

static bool validate_config_param(Config *cfg, const char *param, const char *val)
{
    char *end = NULL;
    if (!strcmp(param, "MIN_TEMP_AIR") || !strcmp(param, "MAX_TEMP_AIR"))
    {
        float x = strtof(val, &end);
        if (end == val || *end != '\0' || x < -40.0f || x > 85.0f) return false;
        if (!strcmp(param, "MIN_TEMP_AIR")) cfg->temp_min = x;
        else cfg->temp_max = x;
        return true;
    }
    if (!strcmp(param, "PRESSURE_MIN") || !strcmp(param, "PRESSURE_MAX"))
    {
        float x = strtof(val, &end);
        if (end == val || *end != '\0' || x < 300.0f || x > 1100.0f) return false;
        if (!strcmp(param, "PRESSURE_MIN")) cfg->press_min = x;
        else cfg->press_max = x;
        return true;
    }
    if (!strcmp(param, "LUMIN_LOW") || !strcmp(param, "LUMIN_HIGH"))
    {
        unsigned long x = strtoul(val, &end, 10);
        if (end == val || *end != '\0' || x > 1023UL) return false;
        if (!strcmp(param, "LUMIN_LOW")) cfg->lumin_low = (uint16_t)x;
        else cfg->lumin_high = (uint16_t)x;
        return true;
    }
    if (!strcmp(param, "FILE_MAX_SIZE"))
    {
        unsigned long x = strtoul(val, &end, 10);
        if (end == val || *end != '\0' || x < 256UL) return false;
        cfg->sd_limit_bytes = (uint32_t)x;
        return true;
    }
    return false;
}

static size_t format_csv_line_unit(const Mesure *m, char *buf, size_t cap)
{
    char ts_str[32];
    char t_str[16], h_str[16], p_str[16], l_str[16], lat_str[20], lon_str[20];

    if (m->timestamp.year >= 2000U && m->timestamp.month >= 1U && m->timestamp.day >= 1U)
    {
        snprintf(ts_str, sizeof(ts_str), "%04u-%02u-%02u %02u:%02u:%02u",
                 (unsigned int)m->timestamp.year,
                 (unsigned int)m->timestamp.month,
                 (unsigned int)m->timestamp.day,
                 (unsigned int)m->timestamp.hours,
                 (unsigned int)m->timestamp.minutes,
                 (unsigned int)m->timestamp.seconds);
    }
    else
    {
        strcpy(ts_str, "NA");
    }

    if (m->statut_temp == STATUT_NA) strcpy(t_str, "NA");
    else snprintf(t_str, sizeof(t_str), "%.2f", (double)m->env.temp);

    if (m->statut_hum == STATUT_NA) strcpy(h_str, "NA");
    else snprintf(h_str, sizeof(h_str), "%.2f", (double)m->env.hum);

    if (m->statut_press == STATUT_NA) strcpy(p_str, "NA");
    else snprintf(p_str, sizeof(p_str), "%.2f", (double)m->env.press);

    if (m->statut_light == STATUT_NA) strcpy(l_str, "NA");
    else snprintf(l_str, sizeof(l_str), "%u", (unsigned int)m->light);

    if (m->statut_gps == STATUT_NA || !m->gps.valid)
    {
        strcpy(lat_str, "NA");
        strcpy(lon_str, "NA");
    }
    else
    {
        snprintf(lat_str, sizeof(lat_str), "%.6f", m->gps.latitude);
        snprintf(lon_str, sizeof(lon_str), "%.6f", m->gps.longitude);
    }

    const char *row_status = "OK";
    if (m->statut_temp == STATUT_INCOHERENT ||
        m->statut_hum == STATUT_INCOHERENT ||
        m->statut_press == STATUT_INCOHERENT ||
        m->statut_light == STATUT_INCOHERENT)
    {
        row_status = "INCOHERENT";
    }
    else if (m->statut_temp == STATUT_NA ||
             m->statut_hum == STATUT_NA ||
             m->statut_press == STATUT_NA ||
             m->statut_light == STATUT_NA ||
             m->statut_gps == STATUT_NA)
    {
        row_status = "PARTIAL";
    }

    int n = snprintf(buf, cap, "%s;%s;%s;%s;%s;%s;%s;%s\r\n",
                     ts_str, t_str, h_str, p_str, l_str, lat_str, lon_str, row_status);
    return (n > 0 && (size_t)n < cap) ? (size_t)n : 0U;
}

/* =========================================================================
 * Cas de tests unitaires Unity (P3-1)
 * ========================================================================= */

void setUp(void) {}
void tearDown(void) {}

void test_rtc_bcd_conversion(void)
{
    TEST_ASSERT_EQUAL_UINT8(0,  bcd_to_dec(0x00));
    TEST_ASSERT_EQUAL_UINT8(23, bcd_to_dec(0x23));
    TEST_ASSERT_EQUAL_UINT8(59, bcd_to_dec(0x59));
    TEST_ASSERT_EQUAL_UINT8(99, bcd_to_dec(0x99));

    TEST_ASSERT_EQUAL_HEX8(0x00, dec_to_bcd(0));
    TEST_ASSERT_EQUAL_HEX8(0x26, dec_to_bcd(26));
    TEST_ASSERT_EQUAL_HEX8(0x59, dec_to_bcd(59));

    for (uint8_t v = 0; v < 60; ++v)
    {
        TEST_ASSERT_EQUAL_UINT8(v, bcd_to_dec(dec_to_bcd(v)));
    }
}

void test_nmea_checksum_validation(void)
{
    const char *valid_gga = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47";
    const char *bad_gga   = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*48";

    TEST_ASSERT_TRUE(nmea_verify_checksum(valid_gga));
    TEST_ASSERT_FALSE(nmea_verify_checksum(bad_gga));
    TEST_ASSERT_FALSE(nmea_verify_checksum("GPGGA,sans_dollar*47"));
    TEST_ASSERT_FALSE(nmea_verify_checksum(NULL));
}

void test_nmea_coordinates_conversion(void)
{
    /* 48 deg 07.038 min N = 48 + 7.038/60 = 48.117300 */
    double lat = nmea_to_decimal("4807.038", 'N');
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 48.1173f, (float)lat);

    /* 11 deg 31.000 min W = -(11 + 31/60) = -11.516667 */
    double lon_w = nmea_to_decimal("01131.000", 'W');
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, -11.5167f, (float)lon_w);

    /* Hémisphère Sud négatif */
    double lat_s = nmea_to_decimal("3352.000", 'S');
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -33.8667f, (float)lat_s);
}

void test_config_domain_validation(void)
{
    Config cfg = CONFIG_DEFAULTS_INIT;

    /* Valeurs par défaut du Cahier des Charges */
    TEST_ASSERT_EQUAL_FLOAT(-10.0f, cfg.temp_min);
    TEST_ASSERT_EQUAL_FLOAT(60.0f,  cfg.temp_max);
    TEST_ASSERT_EQUAL_FLOAT(850.0f, cfg.press_min);
    TEST_ASSERT_EQUAL_FLOAT(1080.0f, cfg.press_max);
    TEST_ASSERT_EQUAL_UINT32(2048U, cfg.sd_limit_bytes);

    /* Bornes valides */
    TEST_ASSERT_TRUE(validate_config_param(&cfg, "MIN_TEMP_AIR", "-5"));
    TEST_ASSERT_EQUAL_FLOAT(-5.0f, cfg.temp_min);
    TEST_ASSERT_TRUE(validate_config_param(&cfg, "PRESSURE_MAX", "1030"));
    TEST_ASSERT_EQUAL_FLOAT(1030.0f, cfg.press_max);
    TEST_ASSERT_TRUE(validate_config_param(&cfg, "LUMIN_LOW", "200"));
    TEST_ASSERT_EQUAL_UINT16(200U, cfg.lumin_low);
    TEST_ASSERT_TRUE(validate_config_param(&cfg, "FILE_MAX_SIZE", "4096"));
    TEST_ASSERT_EQUAL_UINT32(4096U, cfg.sd_limit_bytes);

    /* Bornes invalides (hors domaine du Cahier des Charges) */
    TEST_ASSERT_FALSE(validate_config_param(&cfg, "MIN_TEMP_AIR", "-50"));
    TEST_ASSERT_FALSE(validate_config_param(&cfg, "MAX_TEMP_AIR", "100"));
    TEST_ASSERT_FALSE(validate_config_param(&cfg, "PRESSURE_MIN", "200"));
    TEST_ASSERT_FALSE(validate_config_param(&cfg, "PRESSURE_MAX", "1200"));
    TEST_ASSERT_FALSE(validate_config_param(&cfg, "LUMIN_HIGH", "2048"));
}

void test_csv_format_nominal_and_na(void)
{
    char line[192];
    Mesure m = {0};

    m.timestamp = (RTC_DateTime){ .year = 2026, .month = 10, .day = 29, .hours = 14, .minutes = 5, .seconds = 9 };
    m.env.temp  = 21.50f;
    m.env.hum   = 48.25f;
    m.env.press = 1013.25f;
    m.light     = 512U;
    m.gps.valid = true;
    m.gps.latitude  = 49.383300;
    m.gps.longitude = 1.068600;
    m.statut_temp  = STATUT_OK;
    m.statut_hum   = STATUT_OK;
    m.statut_press = STATUT_OK;
    m.statut_light = STATUT_OK;
    m.statut_gps   = STATUT_OK;

    size_t len = format_csv_line_unit(&m, line, sizeof(line));
    TEST_ASSERT_GREATER_THAN_UINT32(0U, (uint32_t)len);
    TEST_ASSERT_EQUAL_STRING("2026-10-29 14:05:09;21.50;48.25;1013.25;512;49.383300;1.068600;OK\r\n", line);

    /* Cas capteur BME et GPS absents (NA) -> statut PARTIAL */
    m.statut_temp  = STATUT_NA;
    m.statut_hum   = STATUT_NA;
    m.statut_press = STATUT_NA;
    m.statut_gps   = STATUT_NA;
    m.gps.valid    = false;

    len = format_csv_line_unit(&m, line, sizeof(line));
    TEST_ASSERT_GREATER_THAN_UINT32(0U, (uint32_t)len);
    TEST_ASSERT_EQUAL_STRING("2026-10-29 14:05:09;NA;NA;NA;512;NA;NA;PARTIAL\r\n", line);
}

int main(void)
{
#ifdef STM32L476xx
    HAL_Init();
    HAL_Delay(1500);
#endif
    UNITY_BEGIN();
    RUN_TEST(test_rtc_bcd_conversion);
    RUN_TEST(test_nmea_checksum_validation);
    RUN_TEST(test_nmea_coordinates_conversion);
    RUN_TEST(test_config_domain_validation);
    RUN_TEST(test_csv_format_nominal_and_na);
    int res = UNITY_END();
#ifdef STM32L476xx
    while (1)
    {
        HAL_Delay(1000);
    }
#endif
    return res;
}

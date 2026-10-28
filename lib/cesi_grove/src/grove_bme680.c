#include "grove_bme680.h"
#include "board_pins.h"
#include <string.h>
#include <stdio.h>

#define BME680_ADDR_PRIMARY    (0x76U << 1)
#define BME680_ADDR_SECONDARY  (0x77U << 1)

#define BME680_REG_STATUS      0x1DU
#define BME680_REG_PRESS_MSB   0x1FU
#define BME680_REG_CTRL_GAS_1  0x71U
#define BME680_REG_CTRL_HUM    0x72U
#define BME680_REG_CTRL_MEAS   0x74U
#define BME680_REG_CONFIG      0x75U
#define BME680_REG_CALIB_1     0x89U
#define BME680_REG_ID          0xD0U
#define BME680_REG_RESET       0xE0U
#define BME680_REG_CALIB_2     0xE1U

#define BME680_CHIP_ID         0x61U
#define BME280_CHIP_ID         0x60U
#define BME680_SOFT_RESET_CMD  0xB6U
#define BME680_NEW_DATA_MSK    0x80U

/* Registres spécifiques BME280 */
#define BME280_REG_CTRL_HUM    0xF2U
#define BME280_REG_STATUS      0xF3U
#define BME280_REG_CTRL_MEAS   0xF4U
#define BME280_REG_CONFIG      0xF5U
#define BME280_REG_PRESS_MSB   0xF7U
#define BME280_REG_CALIB_00    0x88U
#define BME280_REG_CALIB_26    0xE1U

typedef struct
{
    uint16_t par_t1;
    int16_t  par_t2;
    int8_t   par_t3;

    uint16_t par_p1;
    int16_t  par_p2;
    int8_t   par_p3;
    int16_t  par_p4;
    int16_t  par_p5;
    int8_t   par_p6;
    int8_t   par_p7;
    int16_t  par_p8;
    int16_t  par_p9;
    uint8_t  par_p10;

    uint16_t par_h1;
    uint16_t par_h2;
    int8_t   par_h3;
    int8_t   par_h4;
    int8_t   par_h5;
    uint8_t  par_h6;
    int8_t   par_h7;
} BME680_CalibData;

typedef struct
{
    uint16_t dig_t1;
    int16_t  dig_t2;
    int16_t  dig_t3;

    uint16_t dig_p1;
    int16_t  dig_p2;
    int16_t  dig_p3;
    int16_t  dig_p4;
    int16_t  dig_p5;
    int16_t  dig_p6;
    int16_t  dig_p7;
    int16_t  dig_p8;
    int16_t  dig_p9;

    uint8_t  dig_h1;
    int16_t  dig_h2;
    uint8_t  dig_h3;
    int16_t  dig_h4;
    int16_t  dig_h5;
    int8_t   dig_h6;
} BME280_CalibData;

static uint16_t         s_bme_addr     = BME680_ADDR_PRIMARY;
static uint8_t          s_chip_id      = 0U;
static bool             s_calib_loaded = false;
static BME680_CalibData s_calib_bme680;
static BME280_CalibData s_calib_bme280;

static bool read_reg(uint8_t reg, uint8_t *data, uint16_t len)
{
    return HAL_I2C_Mem_Read(&GROVE_I2C_HANDLE,
                            s_bme_addr,
                            reg,
                            I2C_MEMADD_SIZE_8BIT,
                            data,
                            len,
                            100U) == HAL_OK;
}

static bool write_reg(uint8_t reg, uint8_t value)
{
    return HAL_I2C_Mem_Write(&GROVE_I2C_HANDLE,
                             s_bme_addr,
                             reg,
                             I2C_MEMADD_SIZE_8BIT,
                             &value,
                             1U,
                             100U) == HAL_OK;
}

/* =========================================================================
 * Calibration et lecture BME680
 * ========================================================================= */
static bool read_calibration_bme680(void)
{
    uint8_t c1[25] = {0};
    uint8_t c2[10] = {0};

    if (!read_reg(BME680_REG_CALIB_1, c1, sizeof(c1))) return false;
    if (!read_reg(BME680_REG_CALIB_2, c2, sizeof(c2))) return false;

    s_calib_bme680.par_t1 = (uint16_t)(((uint16_t)c2[9] << 8) | c2[8]);
    s_calib_bme680.par_t2 = (int16_t)(((uint16_t)c1[2] << 8) | c1[1]);
    s_calib_bme680.par_t3 = (int8_t)c1[3];

    s_calib_bme680.par_p1  = (uint16_t)(((uint16_t)c1[6] << 8) | c1[5]);
    s_calib_bme680.par_p2  = (int16_t)(((uint16_t)c1[8] << 8) | c1[7]);
    s_calib_bme680.par_p3  = (int8_t)c1[9];
    s_calib_bme680.par_p4  = (int16_t)(((uint16_t)c1[12] << 8) | c1[11]);
    s_calib_bme680.par_p5  = (int16_t)(((uint16_t)c1[14] << 8) | c1[13]);
    s_calib_bme680.par_p7  = (int8_t)c1[15];
    s_calib_bme680.par_p6  = (int8_t)c1[16];
    s_calib_bme680.par_p8  = (int16_t)(((uint16_t)c1[20] << 8) | c1[19]);
    s_calib_bme680.par_p9  = (int16_t)(((uint16_t)c1[22] << 8) | c1[21]);
    s_calib_bme680.par_p10 = c1[23];

    s_calib_bme680.par_h1 = (uint16_t)(((uint16_t)c2[2] << 4) | (c2[1] & 0x0FU));
    s_calib_bme680.par_h2 = (uint16_t)(((uint16_t)c2[0] << 4) | (c2[1] >> 4));
    s_calib_bme680.par_h3 = (int8_t)c2[3];
    s_calib_bme680.par_h4 = (int8_t)c2[4];
    s_calib_bme680.par_h5 = (int8_t)c2[5];
    s_calib_bme680.par_h6 = c2[6];
    s_calib_bme680.par_h7 = (int8_t)c2[7];

    return true;
}

static bool read_environment_bme680(Env_Data *data)
{
    /* 1. Oversampling humidité x2, température x8, pression x4 + Forced Mode */
    if (!write_reg(BME680_REG_CTRL_HUM, 0x02U)) return false;
    uint8_t ctrl_meas = (uint8_t)((0x04U << 5) | (0x03U << 2) | 0x01U);
    if (!write_reg(BME680_REG_CTRL_MEAS, ctrl_meas)) return false;

    /* 2. Attente de fin de conversion */
    uint32_t t0 = HAL_GetTick();
    uint8_t status = 0U;
    bool ready = false;
    while ((HAL_GetTick() - t0) < 100U)
    {
        if (!read_reg(BME680_REG_STATUS, &status, 1U)) return false;
        if ((status & BME680_NEW_DATA_MSK) != 0U) { ready = true; break; }
    }
    if (!ready) return false;

    /* 3. Lecture groupée des registres bruts (P, T, H) */
    uint8_t raw[8] = {0};
    if (!read_reg(BME680_REG_PRESS_MSB, raw, sizeof(raw))) return false;

    uint32_t press_adc = ((uint32_t)raw[0] << 12) | ((uint32_t)raw[1] << 4) | ((uint32_t)raw[2] >> 4);
    uint32_t temp_adc  = ((uint32_t)raw[3] << 12) | ((uint32_t)raw[4] << 4) | ((uint32_t)raw[5] >> 4);
    uint16_t hum_adc   = (uint16_t)(((uint16_t)raw[6] << 8) | raw[7]);

    /* 4. Compensation officielle Bosch BME680 */
    float var1 = (((float)temp_adc / 16384.0f) - ((float)s_calib_bme680.par_t1 / 1024.0f)) * (float)s_calib_bme680.par_t2;
    float diff = ((float)temp_adc / 131072.0f) - ((float)s_calib_bme680.par_t1 / 8192.0f);
    float var2 = (diff * diff) * ((float)s_calib_bme680.par_t3 * 16.0f);
    float t_fine = var1 + var2;
    float calc_temp = t_fine / 5120.0f;

    float p_var1 = (t_fine / 2.0f) - 64000.0f;
    float p_var2 = p_var1 * p_var1 * ((float)s_calib_bme680.par_p6 / 131072.0f);
    p_var2 = p_var2 + (p_var1 * (float)s_calib_bme680.par_p5 * 2.0f);
    p_var2 = (p_var2 / 4.0f) + ((float)s_calib_bme680.par_p4 * 65536.0f);
    p_var1 = ((((float)s_calib_bme680.par_p3 * p_var1 * p_var1) / 16384.0f) + ((float)s_calib_bme680.par_p2 * p_var1)) / 524288.0f;
    p_var1 = (1.0f + (p_var1 / 32768.0f)) * (float)s_calib_bme680.par_p1;

    float calc_pres_hpa = 0.0f;
    if (p_var1 != 0.0f)
    {
        float calc_pres = 1048576.0f - (float)press_adc;
        calc_pres = ((calc_pres - (p_var2 / 4096.0f)) * 6250.0f) / p_var1;
        p_var1 = ((float)s_calib_bme680.par_p9 * calc_pres * calc_pres) / 2147483648.0f;
        p_var2 = calc_pres * ((float)s_calib_bme680.par_p8 / 32768.0f);
        float p_div = calc_pres / 256.0f;
        float p_var3 = p_div * p_div * p_div * ((float)s_calib_bme680.par_p10 / 131072.0f);
        calc_pres = calc_pres + (p_var1 + p_var2 + p_var3 + ((float)s_calib_bme680.par_p7 * 128.0f)) / 16.0f;
        calc_pres_hpa = calc_pres / 100.0f;
    }

    float h_var1 = (float)hum_adc - (((float)s_calib_bme680.par_h1 * 16.0f) + (((float)s_calib_bme680.par_h3 / 2.0f) * calc_temp));
    float h_var2 = h_var1 * (((float)s_calib_bme680.par_h2 / 262144.0f) *
                   (1.0f + (((float)s_calib_bme680.par_h4 / 16384.0f) * calc_temp) +
                    (((float)s_calib_bme680.par_h5 / 1048576.0f) * calc_temp * calc_temp)));
    float h_var3 = (float)s_calib_bme680.par_h6 / 16384.0f;
    float h_var4 = (float)s_calib_bme680.par_h7 / 2097152.0f;
    float calc_hum = h_var2 + ((h_var3 + (h_var4 * calc_temp)) * h_var2 * h_var2);

    if (calc_hum < 0.0f)   calc_hum = 0.0f;
    if (calc_hum > 100.0f) calc_hum = 100.0f;

    data->temp               = calc_temp;
    data->hum                = calc_hum;
    data->press              = calc_pres_hpa;
    data->gas_resistance_ohm = 0.0f;
    return true;
}

/* =========================================================================
 * Calibration et lecture BME280
 * ========================================================================= */
static bool read_calibration_bme280(void)
{
    uint8_t buf1[24] = {0};
    if (!read_reg(BME280_REG_CALIB_00, buf1, sizeof(buf1))) return false;

    s_calib_bme280.dig_t1 = (uint16_t)(((uint16_t)buf1[1] << 8) | buf1[0]);
    s_calib_bme280.dig_t2 = (int16_t)(((uint16_t)buf1[3] << 8) | buf1[2]);
    s_calib_bme280.dig_t3 = (int16_t)(((uint16_t)buf1[5] << 8) | buf1[4]);

    s_calib_bme280.dig_p1 = (uint16_t)(((uint16_t)buf1[7] << 8) | buf1[6]);
    s_calib_bme280.dig_p2 = (int16_t)(((uint16_t)buf1[9] << 8) | buf1[8]);
    s_calib_bme280.dig_p3 = (int16_t)(((uint16_t)buf1[11] << 8) | buf1[10]);
    s_calib_bme280.dig_p4 = (int16_t)(((uint16_t)buf1[13] << 8) | buf1[12]);
    s_calib_bme280.dig_p5 = (int16_t)(((uint16_t)buf1[15] << 8) | buf1[14]);
    s_calib_bme280.dig_p6 = (int16_t)(((uint16_t)buf1[17] << 8) | buf1[16]);
    s_calib_bme280.dig_p7 = (int16_t)(((uint16_t)buf1[19] << 8) | buf1[18]);
    s_calib_bme280.dig_p8 = (int16_t)(((uint16_t)buf1[21] << 8) | buf1[20]);
    s_calib_bme280.dig_p9 = (int16_t)(((uint16_t)buf1[23] << 8) | buf1[22]);

    uint8_t h1 = 0U;
    if (!read_reg(0xA1U, &h1, 1U)) return false;
    s_calib_bme280.dig_h1 = h1;

    uint8_t buf2[7] = {0};
    if (!read_reg(BME280_REG_CALIB_26, buf2, sizeof(buf2))) return false;

    s_calib_bme280.dig_h2 = (int16_t)(((uint16_t)buf2[1] << 8) | buf2[0]);
    s_calib_bme280.dig_h3 = buf2[2];
    s_calib_bme280.dig_h4 = (int16_t)(((int16_t)(int8_t)buf2[3] << 4) | (buf2[4] & 0x0FU));
    s_calib_bme280.dig_h5 = (int16_t)(((int16_t)(int8_t)buf2[5] << 4) | (buf2[4] >> 4));
    s_calib_bme280.dig_h6 = (int8_t)buf2[6];

    return true;
}

static bool read_environment_bme280(Env_Data *data)
{
    /* 1. Configuration suréchantillonnage humidité x1 (0xF2) */
    if (!write_reg(BME280_REG_CTRL_HUM, 0x01U)) return false;

    /* 2. Configuration température x1, pression x1, mode FORCED (01) -> 0x25 (0xF4) */
    if (!write_reg(BME280_REG_CTRL_MEAS, 0x25U)) return false;

    /* 3. Attente fin de mesure (bit 3 de 0xF3 passe à 0) */
    uint32_t t0 = HAL_GetTick();
    uint8_t status = 0U;
    while ((HAL_GetTick() - t0) < 50U)
    {
        if (!read_reg(BME280_REG_STATUS, &status, 1U)) return false;
        if ((status & 0x08U) == 0U) break;
    }

    /* 4. Lecture groupée des 8 octets 0xF7 à 0xFE (P, T, H) */
    uint8_t raw[8] = {0};
    if (!read_reg(BME280_REG_PRESS_MSB, raw, sizeof(raw))) return false;

    uint32_t press_adc = ((uint32_t)raw[0] << 12) | ((uint32_t)raw[1] << 4) | ((uint32_t)raw[2] >> 4);
    uint32_t temp_adc  = ((uint32_t)raw[3] << 12) | ((uint32_t)raw[4] << 4) | ((uint32_t)raw[5] >> 4);
    uint32_t hum_adc   = ((uint32_t)raw[6] << 8)  | (uint32_t)raw[7];

    /* 5. Compensation officielle Bosch BME280 */
    float var1 = (((float)temp_adc) / 16384.0f - ((float)s_calib_bme280.dig_t1) / 1024.0f) * ((float)s_calib_bme280.dig_t2);
    float diff = (((float)temp_adc) / 131072.0f - ((float)s_calib_bme280.dig_t1) / 8192.0f);
    float var2 = (diff * diff) * ((float)s_calib_bme280.dig_t3);
    float t_fine = var1 + var2;
    float calc_temp = t_fine / 5120.0f;

    float p_var1 = (t_fine / 2.0f) - 64000.0f;
    float p_var2 = p_var1 * p_var1 * ((float)s_calib_bme280.dig_p6) / 32768.0f;
    p_var2 = p_var2 + p_var1 * ((float)s_calib_bme280.dig_p5) * 2.0f;
    p_var2 = (p_var2 / 4.0f) + (((float)s_calib_bme280.dig_p4) * 65536.0f);
    p_var1 = (((float)s_calib_bme280.dig_p3) * p_var1 * p_var1 / 524288.0f + ((float)s_calib_bme280.dig_p2) * p_var1) / 524288.0f;
    p_var1 = (1.0f + p_var1 / 32768.0f) * ((float)s_calib_bme280.dig_p1);
    float calc_pres = 0.0f;
    if (p_var1 > 0.0f)
    {
        float p = 1048576.0f - (float)press_adc;
        p = (p - (p_var2 / 4096.0f)) * 6250.0f / p_var1;
        p_var1 = ((float)s_calib_bme280.dig_p9) * p * p / 2147483648.0f;
        p_var2 = p * ((float)s_calib_bme280.dig_p8) / 32768.0f;
        p = p + (p_var1 + p_var2 + ((float)s_calib_bme280.dig_p7)) / 16.0f;
        calc_pres = p / 100.0f; /* Pa -> hPa */
    }

    float h_var = t_fine - 76800.0f;
    h_var = (((float)hum_adc - (((float)s_calib_bme280.dig_h4) * 64.0f + ((float)s_calib_bme280.dig_h5) / 16384.0f * h_var)) *
             (((float)s_calib_bme280.dig_h2) / 65536.0f * (1.0f + ((float)s_calib_bme280.dig_h6) / 67108864.0f * h_var *
              (1.0f + ((float)s_calib_bme280.dig_h3) / 67108864.0f * h_var))));
    h_var = h_var * (1.0f - ((float)s_calib_bme280.dig_h1) * h_var / 524288.0f);
    if (h_var > 100.0f)   h_var = 100.0f;
    else if (h_var < 0.0f) h_var = 0.0f;

    data->temp               = calc_temp;
    data->press              = calc_pres;
    data->hum                = h_var;
    data->gas_resistance_ohm = 0.0f;
    return true;
}

/* =========================================================================
 * API Publique
 * ========================================================================= */
bool GroveBME680_ReadChipId(uint8_t *chip_id)
{
    if (chip_id == NULL) return false;
    return read_reg(BME680_REG_ID, chip_id, 1U);
}

bool GroveBME680_Init(void)
{
    uint8_t id = 0U;
    s_calib_loaded = false;
    s_chip_id      = 0U;

    /* Test de l'adresse I2C 0x76 (par défaut) puis 0x77 */
    s_bme_addr = BME680_ADDR_PRIMARY;
    bool found = (GroveBME680_ReadChipId(&id) && (id == BME680_CHIP_ID || id == BME280_CHIP_ID));
    if (!found)
    {
        uint8_t id2 = 0U;
        s_bme_addr = BME680_ADDR_SECONDARY;
        if (GroveBME680_ReadChipId(&id2) && (id2 == BME680_CHIP_ID || id2 == BME280_CHIP_ID))
        {
            found = true;
            id = id2;
        }
    }

    if (!found)
    {
        return false;
    }

    s_chip_id = id;

    /* Soft reset + attente courte de démarrage interne (5 ms) */
    (void)write_reg(BME680_REG_RESET, BME680_SOFT_RESET_CMD);
    uint32_t t0 = HAL_GetTick();
    while ((HAL_GetTick() - t0) < 10U) {}

    if (s_chip_id == BME280_CHIP_ID)
    {
        if (!read_calibration_bme280()) return false;
        printf("[BME] Module BME280 initialise avec succes a l'adresse 0x%02X !\r\n", s_bme_addr >> 1);
    }
    else
    {
        if (!read_calibration_bme680()) return false;
        (void)write_reg(BME680_REG_CTRL_GAS_1, 0x00U);
        (void)write_reg(BME680_REG_CONFIG, (uint8_t)(0x02U << 2));
        printf("[BME] Module BME680 initialise avec succes a l'adresse 0x%02X !\r\n", s_bme_addr >> 1);
    }

    s_calib_loaded = true;
    return true;
}

bool GroveBME680_ReadEnvironment(Env_Data *data)
{
    if (data == NULL) return false;

    if (!s_calib_loaded)
    {
        if (!GroveBME680_Init()) return false;
    }

    if (s_chip_id == BME280_CHIP_ID)
    {
        return read_environment_bme280(data);
    }
    else if (s_chip_id == BME680_CHIP_ID)
    {
        return read_environment_bme680(data);
    }

    return false;
}

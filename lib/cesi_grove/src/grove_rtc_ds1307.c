#include "grove_rtc_ds1307.h"
#include "board_pins.h"
#include <stddef.h>

#define DS1307_ADDR     (0x68U << 1)
#define DS1307_REG_SEC  0x00U
#define DS1307_CH_BIT   0x80U

static uint8_t bcd_to_dec(uint8_t bcd)
{
    return (uint8_t)(((bcd >> 4) * 10U) + (bcd & 0x0FU));
}

static uint8_t dec_to_bcd(uint8_t dec)
{
    return (uint8_t)(((dec / 10U) << 4) | (dec % 10U));
}

bool GroveRTC_Init(void)
{
    if (HAL_I2C_IsDeviceReady(&GROVE_I2C_HANDLE, DS1307_ADDR, 3U, 100U) != HAL_OK)
    {
        return false;
    }

    /* P1-13 : Contrôle du bit CH (Clock Halt, bit 7 du registre 0x00).
     * Si CH = 1 (premier démarrage avec pile CR1225 neuve), l'oscillateur est à l'arrêt :
     * on force CH = 0 tout en préservant les secondes existantes. */
    uint8_t sec_reg = 0U;
    if (HAL_I2C_Mem_Read(&GROVE_I2C_HANDLE,
                         DS1307_ADDR,
                         DS1307_REG_SEC,
                         I2C_MEMADD_SIZE_8BIT,
                         &sec_reg,
                         1U,
                         100U) != HAL_OK)
    {
        return false;
    }

    if ((sec_reg & DS1307_CH_BIT) != 0U)
    {
        sec_reg &= (uint8_t)(~DS1307_CH_BIT);
        if (HAL_I2C_Mem_Write(&GROVE_I2C_HANDLE,
                              DS1307_ADDR,
                              DS1307_REG_SEC,
                              I2C_MEMADD_SIZE_8BIT,
                              &sec_reg,
                              1U,
                              100U) != HAL_OK)
        {
            return false;
        }
    }

    return true;
}

bool GroveRTC_GetDateTime(RTC_DateTime *dt)
{
    if (dt == NULL)
    {
        return false;
    }

    uint8_t data[7] = {0};

    if (HAL_I2C_Mem_Read(&GROVE_I2C_HANDLE,
                         DS1307_ADDR,
                         DS1307_REG_SEC,
                         I2C_MEMADD_SIZE_8BIT,
                         data,
                         sizeof(data),
                         100U) != HAL_OK)
    {
        return false;
    }

    if ((data[0] & DS1307_CH_BIT) != 0U)
    {
        return false;
    }

    dt->seconds   = bcd_to_dec(data[0] & 0x7FU);
    dt->minutes   = bcd_to_dec(data[1] & 0x7FU);
    dt->hours     = bcd_to_dec(data[2] & 0x3FU);
    dt->dayOfWeek = bcd_to_dec(data[3] & 0x07U);
    dt->day       = bcd_to_dec(data[4] & 0x3FU);
    dt->month     = bcd_to_dec(data[5] & 0x1FU);
    dt->year      = (uint16_t)(2000U + bcd_to_dec(data[6]));

    return true;
}

bool GroveRTC_SetDateTime(const RTC_DateTime *dt)
{
    if (dt == NULL || dt->year < 2000U || dt->year > 2099U)
    {
        return false;
    }

    uint8_t data[7];
    data[0] = (uint8_t)(dec_to_bcd(dt->seconds) & 0x7FU); /* CH = 0 : oscillateur actif */
    data[1] = dec_to_bcd(dt->minutes);
    data[2] = (uint8_t)(dec_to_bcd(dt->hours) & 0x3FU);   /* Mode 24h */
    data[3] = dec_to_bcd(dt->dayOfWeek > 0U ? dt->dayOfWeek : 1U);
    data[4] = dec_to_bcd(dt->day);
    data[5] = dec_to_bcd(dt->month);
    data[6] = dec_to_bcd((uint8_t)(dt->year - 2000U));

    return HAL_I2C_Mem_Write(&GROVE_I2C_HANDLE,
                             DS1307_ADDR,
                             DS1307_REG_SEC,
                             I2C_MEMADD_SIZE_8BIT,
                             data,
                             sizeof(data),
                             100U) == HAL_OK;
}

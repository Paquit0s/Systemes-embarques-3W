#include "grove_light.h"
#include "board_pins.h"

/**
 * @brief Initialise le canal ADC1_IN9 (PA4) et lance la calibration matérielle
 *        de l'ADC 12 bits (P2-3 : HAL_ADCEx_Calibration_Start).
 */
void GroveLight_Init(void)
{
    /* Calibration interne de l'ADC1 en mode Single-Ended avant toute mesure (P2-3) */
    (void)HAL_ADC_Stop(&LIGHT_ADC_HANDLE);
    (void)HAL_ADCEx_Calibration_Start(&LIGHT_ADC_HANDLE, ADC_SINGLE_ENDED);

    /* Configuration du canal PA4 (ADC_CHANNEL_9) */
    ADC_ChannelConfTypeDef sConfig = {0};
    sConfig.Channel      = LIGHT_ADC_CHANNEL;
    sConfig.Rank         = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_47CYCLES_5;
    sConfig.SingleDiff   = ADC_SINGLE_ENDED;
    sConfig.OffsetNumber = ADC_OFFSET_NONE;
    sConfig.Offset       = 0;

    (void)HAL_ADC_ConfigChannel(&LIGHT_ADC_HANDLE, &sConfig);
}

/**
 * @brief Lit la valeur brute 12 bits (0..4095) de l'ADC1.
 * @return Valeur entre 0 et 4095, ou GROVE_LIGHT_RAW_ERROR (0xFFFF) en cas d'erreur.
 */
uint16_t GroveLight_ReadRaw(void)
{
    if (HAL_ADC_Start(&LIGHT_ADC_HANDLE) != HAL_OK)
    {
        return GROVE_LIGHT_RAW_ERROR;
    }

    if (HAL_ADC_PollForConversion(&LIGHT_ADC_HANDLE, 100U) != HAL_OK)
    {
        (void)HAL_ADC_Stop(&LIGHT_ADC_HANDLE);
        return GROVE_LIGHT_RAW_ERROR;
    }

    uint16_t value = (uint16_t)HAL_ADC_GetValue(&LIGHT_ADC_HANDLE);

    (void)HAL_ADC_Stop(&LIGHT_ADC_HANDLE);

    if (value > GROVE_LIGHT_ADC_MAX)
    {
        value = GROVE_LIGHT_ADC_MAX;
    }

    return value;
}

/**
 * @brief Lit la luminosité brute 12 bits (0..4095) avec statut booléen.
 *        Compatible avec l'appel dans capteur_manager.c.
 */
bool GroveLight_Read(uint16_t *raw_out)
{
    if (raw_out == NULL)
    {
        return false;
    }

    uint16_t val = GroveLight_ReadRaw();
    if (val == GROVE_LIGHT_RAW_ERROR)
    {
        return false;
    }

    *raw_out = val;
    return true;
}

/**
 * @brief Convertit une mesure brute 12 bits (0..4095) en pourcentage (0.0 .. 100.0 %) (P2-3)
 */
float GroveLight_RawToPercent(uint16_t raw)
{
    if (raw >= GROVE_LIGHT_ADC_MAX)
    {
        return 100.0f;
    }
    return ((float)raw * 100.0f) / (float)GROVE_LIGHT_ADC_MAX;
}

/**
 * @brief Convertit une mesure brute 12 bits (0..4095) en estimation d'éclairement (lux) (P2-3)
 *        Modèle linéaire/quadratique approché pour le phototransistor LS06-S du Grove Light v1.3
 *        alimenté sous 3,3 V (pleine échelle ~1000 lux).
 */
float GroveLight_RawToLux(uint16_t raw)
{
    if (raw > GROVE_LIGHT_ADC_MAX)
    {
        return 0.0f;
    }
    float normalized = (float)raw / (float)GROVE_LIGHT_ADC_MAX;
    return normalized * normalized * 1000.0f;
}

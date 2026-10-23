#include "grove_light.h"
#include "board_pins.h"

void GroveLight_Init(void)
{
    /* Le périphérique ADC1 (hadc1) et la broche PA4 sont initialisés dans setup.c */
    ADC_ChannelConfTypeDef sConfig = {0};
    sConfig.Channel      = LIGHT_ADC_CHANNEL;
    sConfig.Rank         = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_47CYCLES_5;
    sConfig.SingleDiff   = ADC_SINGLE_ENDED;
    sConfig.OffsetNumber = ADC_OFFSET_NONE;
    sConfig.Offset       = 0;

    HAL_ADC_ConfigChannel(&LIGHT_ADC_HANDLE, &sConfig);
}

uint16_t GroveLight_ReadRaw(void)
{
    if (HAL_ADC_Start(&LIGHT_ADC_HANDLE) != HAL_OK)
        return 0xFFFFU;

    if (HAL_ADC_PollForConversion(&LIGHT_ADC_HANDLE, 100U) != HAL_OK)
    {
        HAL_ADC_Stop(&LIGHT_ADC_HANDLE);
        return 0xFFFFU;
    }

    uint16_t value = (uint16_t)HAL_ADC_GetValue(&LIGHT_ADC_HANDLE);

    HAL_ADC_Stop(&LIGHT_ADC_HANDLE);

    return value;
}
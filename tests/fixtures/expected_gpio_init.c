/*
 * Сгенерировано MIK32-Configurator.
 * МК: MIK32, версия конфигурации: 2.0.
 * Блоков периферии: 1 — GPIO_2.
 */
#include "mik32_init.h"
#include "mik32_hal_gpio.h"
#include "mik32_hal_pcc.h"

/*
 * GPIO_2, блоков конфигурации: 1.
 *   пины 6: mode=output, pull=none, drive_strength=2mA.
 */
void GPIO_2_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_PCC_GPIO_2_CLK_ENABLE();

    GPIO_InitStruct.Pin = GPIO_PIN_6;
    GPIO_InitStruct.Mode = HAL_GPIO_MODE_GPIO_OUTPUT;
    GPIO_InitStruct.Pull = HAL_GPIO_PULL_NONE;
    GPIO_InitStruct.DS = HAL_GPIO_DS_2MA;
    HAL_GPIO_Init(GPIO_2, &GPIO_InitStruct);
}

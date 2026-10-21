/*
 * Сгенерировано MIK32-Configurator.
 * МК: MIK32, версия конфигурации: 2.0.
 * Блоков периферии: 1 — UART_0.
 */
#include "mik32_init.h"
#include "mik32_hal_usart.h"

USART_HandleTypeDef husart_0;

/*
 * UART_0: baud_rate=38400, data_bits=8, stop_bits=1, parity=none, flow_control=false, direction=duplex.
 * Фактическая скорость при HSI=32000000Гц: DIVIDER=833 -> 38415.366 бод, погрешность 0.040%
 * mode=asynchronous, bit_direction=lsb_first, channel_mode=duplex.
 */
void UART_0_Init(void)
{
    /* USER CODE BEGIN UART_0_Init 0 */
    /* USER CODE END UART_0_Init 0 */

    husart_0.Instance = UART_0;
    husart_0.transmitting = Enable;
    husart_0.receiving = Enable;
    husart_0.frame = Frame_8bit;
    husart_0.parity_bit = Disable;
    husart_0.parity_bit_inversion = Disable;
    husart_0.bit_direction = LSB_First;
    husart_0.data_inversion = Disable;
    husart_0.tx_inversion = Disable;
    husart_0.rx_inversion = Disable;
    husart_0.swap = Disable;
    husart_0.lbm = Disable;
    husart_0.stop_bit = StopBit_1;
    husart_0.mode = Asynchronous_Mode;
    husart_0.xck_mode = XCK_Mode0;
    husart_0.last_byte_clock = Disable;
    husart_0.overwrite = Disable;
    husart_0.rts_mode = AlwaysEnable_mode;
    husart_0.dma_tx_request = Disable;
    husart_0.dma_rx_request = Disable;
    husart_0.channel_mode = Duplex_Mode;
    husart_0.tx_break_mode = Disable;
    husart_0.Interrupt.ctsie = Disable;
    husart_0.Interrupt.eie = Disable;
    husart_0.Interrupt.idleie = Disable;
    husart_0.Interrupt.lbdie = Disable;
    husart_0.Interrupt.peie = Disable;
    husart_0.Interrupt.rxneie = Disable;
    husart_0.Interrupt.tcie = Disable;
    husart_0.Interrupt.txeie = Disable;
    husart_0.Modem.rts = Disable;
    husart_0.Modem.cts = Disable;
    husart_0.Modem.dtr = Disable;
    husart_0.Modem.dcd = Disable;
    husart_0.Modem.dsr = Disable;
    husart_0.Modem.ri = Disable;
    husart_0.Modem.ddis = Disable;
    husart_0.baudrate = 38400;

    HAL_USART_Init(&husart_0);

    /* USER CODE BEGIN UART_0_Init 2 */
    /* USER CODE END UART_0_Init 2 */
}

/*
 * Приёмник вывода xprintf. Штатная слабая реализация xputc
 * (mik32_hal_usart.c:435) жёстко привязана к UART_0 — эта заменяет её
 * и печатает в выбранный конфигурацией UART_0.
 */
void xputc(char c)
{
    HAL_USART_Transmit(&husart_0, c, USART_TIMEOUT_DEFAULT);
}

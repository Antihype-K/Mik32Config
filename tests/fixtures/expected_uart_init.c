#include "mik32_init.h"
#include "mik32_hal_usart.h"

USART_HandleTypeDef husart;

/*
 * Сгенерировано MIK32-Configurator.
 * UART_1: baud_rate=115200, data_bits=8, stop_bits=1, parity=none, flow_control=false.
 * Только Asynchronous_Mode — синхронный режим вне скоупа этой сессии
 * (Errata MIK32V2 v1.0.1, §10.1, см. knowledge_base/uart.json).
 */
void UART_Init(void)
{
    husart.Instance = UART_1;
    husart.transmitting = Enable;
    husart.receiving = Enable;
    husart.frame = Frame_8bit;
    husart.parity_bit = Disable;
    husart.parity_bit_inversion = Disable;
    husart.bit_direction = LSB_First;
    husart.data_inversion = Disable;
    husart.tx_inversion = Disable;
    husart.rx_inversion = Disable;
    husart.swap = Disable;
    husart.lbm = Disable;
    husart.stop_bit = StopBit_1;
    husart.mode = Asynchronous_Mode;
    husart.xck_mode = XCK_Mode3;
    husart.last_byte_clock = Disable;
    husart.overwrite = Disable;
    husart.rts_mode = AlwaysEnable_mode;
    husart.dma_tx_request = Disable;
    husart.dma_rx_request = Disable;
    husart.channel_mode = Duplex_Mode;
    husart.tx_break_mode = Disable;
    husart.Interrupt.ctsie = Disable;
    husart.Interrupt.eie = Disable;
    husart.Interrupt.idleie = Disable;
    husart.Interrupt.lbdie = Disable;
    husart.Interrupt.peie = Disable;
    husart.Interrupt.rxneie = Disable;
    husart.Interrupt.tcie = Disable;
    husart.Interrupt.txeie = Disable;
    husart.Modem.rts = Disable;
    husart.Modem.cts = Disable;
    husart.Modem.dtr = Disable;
    husart.Modem.dcd = Disable;
    husart.Modem.dsr = Disable;
    husart.Modem.ri = Disable;
    husart.Modem.ddis = Disable;
    husart.baudrate = 115200;

    HAL_USART_Init(&husart);
}

#ifndef SETUPUART_H
#define SETUPUART_H

#include "hardware/uart.h"

#define BAUD_RATE 230400
#define DATA_BITS 8
#define STOP_BITS 1
#define PARITY UART_PARITY_NONE

#define UART_TX_PIN 0
#define UART_RX_PIN 1

// Function pointer type for UART interrupt handler
typedef void (*uart_handler_t)(void);

void setupUart(uart_inst_t *uartId, uart_handler_t uart_irq_handler)
{
    // Initialize UART with the specified baud rate
    uart_init(uartId, BAUD_RATE);

    // Set the TX and RX pins by using the function select on the GPIO
    // Set datasheet for more information on function select
    gpio_set_function(UART_TX_PIN, UART_FUNCSEL_NUM(uartId, UART_TX_PIN));
    gpio_set_function(UART_RX_PIN, UART_FUNCSEL_NUM(uartId, UART_RX_PIN));

    // Set UART flow control CTS/RTS, we don't want these, so turn them off
    uart_set_hw_flow(uartId, false, false);

    // Set our data format
    uart_set_format(uartId, DATA_BITS, STOP_BITS, PARITY);

    // Enable FIFO for better handling of streaming data
    uart_set_fifo_enabled(uartId, true);

    // Set the RX FIFO interrupt threshold to 1/8 full (~4 of 32 bytes) instead
    // of the default 1/2. Firing the RX IRQ earlier leaves more margin before
    // the 32-byte FIFO overruns during high-rate bursts.
    hw_write_masked(&uart_get_hw(uartId)->ifls, 0u << 3, UART_UARTIFLS_RXIFLSEL_BITS);

    // Select correct interrupt for the UART we are using
    int UART_IRQ = uartId == uart1 ? UART1_IRQ : UART0_IRQ;

    // And set up and enable the interrupt handlers
    irq_set_exclusive_handler(UART_IRQ, uart_irq_handler);
    irq_set_enabled(UART_IRQ, true);

    // Now enable the UART to send interrupts - RX only
    uart_set_irq_enables(uartId, true, false);

    printf("UART Settings:\n");
    printf("UART Id: %s\n", uartId == uart1 ? "uart1" : "uart0");
    printf("Baud Rate: %d\n", BAUD_RATE);
    printf("Data Bits: %d\n", DATA_BITS);
    printf("Stop Bits: %d\n", STOP_BITS);
    printf("Parity: %s\n", PARITY == UART_PARITY_NONE ? "None" : (PARITY == UART_PARITY_ODD ? "Odd" : "Even"));
    printf("TX Pin: %d\n", UART_TX_PIN);
    printf("RX Pin: %d\n", UART_RX_PIN);
}

#endif // SETUPUART_H
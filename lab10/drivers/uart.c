// uart.c
// TODO: Include necessary headers (avr/io, stdio, uart.h)
// TODO: Define F_CPU and BAUD, and calculate UBRR_VAL
#include <avr/io.h>
#include <stdio.h>
#include "uart.h"
#define F_CPU 16000000UL
#define BAUD 9600
#define UBRR_VAL ((F_CPU / (16UL * BAUD)) - 1)

static int uart_putchar(char c, FILE *stream);
static FILE uart_output = FDEV_SETUP_STREAM(uart_putchar, NULL, _FDEV_SETUP_WRITE);

// Function to be linked to stdout for printf.
static int uart_putchar(char c, FILE *stream) {
    // TODO: Wait for the UDRE0 flag in UCSR0A to be set.
    // TODO: Write the character 'c' to the UDR0 register.
    while (!(UCSR0A & (1 << UDRE0)));
    UDR0 = c;
    return 0;
}
// TODO: Create the FILE stream object using FDEV_SETUP_STREAM.
void uart_init(void) {
    // TODO: Set the baud rate in UBRR0H and UBRR0L.
    // TODO: Enable the transmitter (TXEN0) in UCSR0B.
    // TODO: Set the frame format to 8 data, 1 stop bit (UCSZ01, UCSZ00 in UCSR0C).
    UBRR0H = (uint8_t)(UBRR_VAL >> 8);
    UBRR0L = (uint8_t)(UBRR_VAL & 0xFF);
    UCSR0B = (1 << TXEN0); // Enable transmitter
    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00); // 8 data bits, 1 stop bit

    // In `uart_init()`, enable the receiver (`RXEN0`) and the Receive Complete Interrupt (`RXCIE0`) in the `UCSR0B` register.
    UCSR0B |= (1 << RXEN0) | (1 << RXCIE0); // Enable receiver and RX complete interrupt

    stdout = &uart_output;
}
// TODO: Implement uart_get_stream to return a pointer to your stream object.
FILE *uart_get_stream(void) {
    return &uart_output;
}


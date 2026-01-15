// uart.h
#ifndef UART_H
#define UART_H
#include <stdio.h> // Required for FILE type
void uart_init(void);
FILE* uart_get_stream(void);
#endif
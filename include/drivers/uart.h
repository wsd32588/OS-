#ifndef TINYOS_UART_H
#define TINYOS_UART_H

void uart_putchar(char ch);
void uart_puts(const char *str);
void uart_put_hex(unsigned long value);

#endif
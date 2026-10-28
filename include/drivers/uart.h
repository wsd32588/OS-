#ifndef TINYOS_UART_H
#define TINYOS_UART_H

#ifdef __cplusplus
extern "C" {
#endif

void uart_putchar(char ch);
void uart_puts(const char *str);
void uart_put_hex(unsigned long value);

#ifdef __cplusplus
}
#endif

#endif

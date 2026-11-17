#ifndef TINYOS_UART_H
#define TINYOS_UART_H

#ifdef __cplusplus
extern "C" {
#endif

void uart_putchar(char ch);
void uart_puts(const char *str);
void uart_put_hex(unsigned long value);

/* 0..255：读到一个字节；-1：暂时没有输入。 */
int uart_try_getchar(void);

#ifdef __cplusplus
}
#endif

#endif

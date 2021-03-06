#ifndef __UART0_H__
#define __UART0_H__

#include <stdint.h>

#include "bcomp.h"

/* ���������� ���������� ��� ������ ������? */
#if ( VIRTUINO_SUPPORT == 1 ) 
#define __UART_IRQ   1
extern void virtuino_proc(uint8_t data);
#define UART_DISPATCHER(a) virtuino_proc(a)
#elif defined( BCOMP_V2 )
#define __UART_IRQ   1
extern void nmea_proc(uint8_t ch);
#define UART_DISPATCHER(a) nmea_proc(a)
#else
#define __UART_IRQ   0
#endif

void    uart0_init(uint32_t baudrate);
void    uart0_putchar(uint8_t ch);
uint8_t uart0_getchar(void);
void    uart0_puts(const uint8_t *str);

#endif

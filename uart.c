
/*
 * Copyright (c) 2006-2012 by Roland Riegel <feedback@roland-riegel.de>
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <avr/sfr_defs.h>
#include <avr/sleep.h>

#include "uart.h"

#define BAUD 57600
#define UBRR_VALUE (((F_CPU) + 8UL * (BAUD)) / (16UL * (BAUD)) -1UL)
#define UBRRL_VALUE (UBRR_VALUE & 0xff)
#define UBRRH_VALUE (UBRR_VALUE >> 8)

uint8_t uart_buf[UART_BUFFER_SIZE];
uint8_t uart_buf_start = 0;
uint8_t uart_buf_end = 0;

ISR(USART_RX_vect)
{
	uart_buf[uart_buf_end] = UDR;
	uart_buf_end++;
	if(uart_buf_end == UART_BUFFER_SIZE) uart_buf_end = 0;
}

void uart_init()
{
	DDRD |= 1<<PD1;
	DDRD &= ~(1<<PD0);
	UBRRH = UBRRH_VALUE;
	UBRRL = UBRRL_VALUE;
    /* set frame format: 8 bit, no parity, 1 bit */
    UCSRC = UCSRC_SELECT | (1 << UCSZ1) | (1 << UCSZ0);
    /* enable serial receiver and transmitter */
    UCSRB = (1 << RXEN) | (1 << TXEN) | (1 << RXCIE);
	sei();
}

void uart_putc(uint8_t c)
{
    /* wait until transmit buffer is empty */
    while(!(UCSRA & (1 << UDRE)));

    /* send next byte */
    UDR = c;
}

void uart_putc_hex(uint8_t b)
{
    /* upper nibble */
    if((b >> 4) < 0x0a)
        uart_putc((b >> 4) + '0');
    else
        uart_putc((b >> 4) - 0x0a + 'A');

    /* lower nibble */
    if((b & 0x0f) < 0x0a)
        uart_putc((b & 0x0f) + '0');
    else
        uart_putc((b & 0x0f) - 0x0a + 'A');
}

void uart_putw_hex(uint16_t w)
{
    uart_putc_hex((uint8_t) (w >> 8));
    uart_putc_hex((uint8_t) (w & 0xff));
}

void uart_putdw_hex(uint32_t dw)
{
    uart_putw_hex((uint16_t) (dw >> 16));
    uart_putw_hex((uint16_t) (dw & 0xffff));
}

void uart_putw_dec(uint16_t w)
{
    uint16_t num = 10000;
    uint8_t started = 0;

    while(num > 0)
    {
        uint8_t b = w / num;
        if(b > 0 || started || num == 1)
        {
            uart_putc('0' + b);
            started = 1;
        }
        w -= b * num;

        num /= 10;
    }
}

void uart_putdw_dec(uint32_t dw)
{
    uint32_t num = 1000000000;
    uint8_t started = 0;

    while(num > 0)
    {
        uint8_t b = dw / num;
        if(b > 0 || started || num == 1)
        {
            uart_putc('0' + b);
            started = 1;
        }
        dw -= b * num;

        num /= 10;
    }
}

void uart_puts(const char* str)
{
    while(*str)
        uart_putc(*str++);
}

void uart_puts_p(const char *str)
{
	uint8_t b = 0;
    while( (b = pgm_read_byte(str++)) ) uart_putc(b);
}

uint8_t uart_getc(uint8_t* b)
{
	cli();
	if(uart_buf_start != uart_buf_end){
		*b = uart_buf[uart_buf_start];
		uart_buf_start++;
		if(uart_buf_start == UART_BUFFER_SIZE) uart_buf_start = 0;
		sei();
		return UART_OK;
	}
	sei();
	return UART_FAIL;
}

uint8_t uart_gets(char* str)
{
	uint8_t start, i=0;
	cli();
	for(start=uart_buf_start;start!=uart_buf_end;start++){
		if(start == UART_BUFFER_SIZE) start = 0;
		str[i] = uart_buf[start];
		i++;
		if(uart_buf[start] == '\n') break;		
	}
	
	if(i == 0 || str[i-1] != '\n'){ sei(); return UART_FAIL; }
	str[i-1] = 0;
	uart_buf_start = uart_buf_end;
	sei();
	return UART_OK;
}
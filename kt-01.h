/*
 * kt_01.h
 *
 * Created: 27.01.2016 23:59:37
 *  Author: Elektron
 */ 
#pragma once

#define KT_PORT	PORTC
#define KT_DDR	DDRC
#define KT_PIN	PINC
#define KT_LINE 0
#define KT_PROG 1

#define KT_READ_ROM_OK	0
#define KT_NO_KEY		1
#define KT_CRC_ERR		2

void kt_init(void);
//uint8_t kt_crc(uint8_t* data, uint8_t len);
//uint8_t kt_crc_check();
uint8_t kt_read_rom(uint8_t* data);
uint8_t kt_reset(void);
//uint8_t kt_read_byte(void);
//void kt_write_byte(uint8_t data);
uint8_t kt_write_rom(uint8_t* data);
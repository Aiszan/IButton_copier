/*
 * metakom.h
 *
 * Created: 30.01.2016 2:10:17
 *  Author: Elektron
 */ 
#pragma once

#define MK_PORT	PORTC
#define MK_DDR	DDRC
#define MK_ADC	0

enum enum_mk{MK_READ_OK, MK_NO_KEY};

uint8_t mk_code[9];

uint8_t mk_crc(uint8_t* data);
uint8_t mk_read(uint8_t* data);
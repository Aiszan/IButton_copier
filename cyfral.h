 /*
 * cyfral.h
 *
 * Created: 08.02.2016 20:39:34
 *  Author: Elektron
 */
#pragma once

#define CL_PORT	PORTC
#define CL_DDR	DDRC
#define CL_ADC	0

enum enum_cl{CL_READ_OK, CL_NO_KEY};

uint8_t cl_code[2];
uint8_t cl_buffer[14];

uint8_t cl_decode(uint8_t* data);
uint8_t cl_read(uint8_t* data);
/*
 * metakom.h
 *
 * Created: 30.01.2016 2:10:17
 *  Author: Elektron
 */ 
#pragma once

#define MK_READ_OK		0
#define MK_NO_KEY		1

uint8_t mk_data[4];

void mk_init(void);
//uint8_t mk_crc(void);
uint8_t mk_read(uint8_t* data);
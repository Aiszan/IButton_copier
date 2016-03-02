/*
 * dallas.h
 *
 * Created: 20.01.2016 ??2:01:57
 *  Author: Elektron
 */ 
#pragma once
#define DS_READ_ROM_OK       0
#define DS_READ_ROM_NO_PRES  1
#define DS_READ_ROM_CRC_ERR  2

#define DS_PORT PORTC
#define DS_DDR DDRC
#define DS_PIN PINC

#define DS_LINE 0

//const uint8_t ds_crc_table[256];

void ds_init();

//void ds_out(uint8_t data_byte);

//uint8_t ds_in(void);

//void ds_write_bit(uint8_t value);

//uint8_t ds_read_bit(void);

uint8_t ds_crc(uint8_t crc, uint8_t data);

void ds_write_byte(uint8_t data);

uint8_t ds_read_byte(void);

uint8_t ds_reset(void);

uint8_t ds_crc_check();

uint8_t ds_read_rom();

void ds_program_byte(uint8_t data);

uint8_t ds_timeslot(void);

uint8_t ds_program_tm08v2(uint8_t* p);

uint8_t ds_program_tm2004(uint8_t* p);

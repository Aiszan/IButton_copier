#include <avr/io.h>
#include <util/delay.h>
#include <stdint.h>
#include "dallas.h"

void ds_init()
{
	DS_PORT &= ~(1<<DS_LINE);
	DS_DDR &= ~(1<<DS_LINE);
}

uint8_t ds_crc(uint8_t crc, uint8_t data)
{
	crc ^= data;
	for (uint8_t i=0; i<8; i++){
		if (crc & 0x01)
		crc = (crc >> 1) ^ 0x8C;
		else
		crc >>= 1;
	}
	return crc;
}

uint8_t ds_crc_check(uint8_t* data)
{
	uint8_t zero = 0, crc = 0;
	
	for(uint8_t i=0;i<8;i++){
		crc = ds_crc(crc, data[i]);
	}

	for (uint8_t i=0;i<8;i++) if(data[i] != 0) zero++;

	if(zero == 0) return 1;
	if(crc == 0)return 0;
	return 2;
}

void ds_out(uint8_t data_byte)
{
	if (data_byte==0)
	DS_DDR |= (1<<DS_LINE);
	else
	DS_DDR &= ~(1<<DS_LINE);
}

uint8_t ds_in(void)
{
	return DS_PIN & (1<<DS_LINE);
}

void ds_write_bit(uint8_t value)
{
	if (value)
	{
		ds_out(0);
		_delay_us(1);
		ds_out(1);
		_delay_us(64);
		}else{
		ds_out(0);
		_delay_us(60);
		ds_out(1);
		_delay_us(10);
	}
}

uint8_t ds_read_bit()
{
	uint8_t result;

	ds_out(0);
	_delay_us(1);
	ds_out(1);
	_delay_us(9);
	result = ds_in();
	_delay_us(55);

	return result;
}

void ds_write_byte(uint8_t data)
{
	for (uint8_t loop=0;loop<8;loop++){
		ds_write_bit(data & 0x01);
		data >>= 1;
	}
}

void ds_program_byte(uint8_t data)
{
	data = ~data;
	for (uint8_t i=0;i<8;i++){
		ds_write_bit(data & 0x01);
		data >>= 1;
		_delay_ms(10);
	}
}

void ds_program_pulse()
{
	_delay_us(600);
	ds_write_bit(1);
	_delay_ms(50);
}

uint8_t ds_read_byte(void)
{
	uint8_t result = 0;

	for (uint8_t i=0;i<8;i++){
		result >>= 1;
		if (ds_read_bit()) result |= 0x80;
	}
	return result;
}

uint8_t ds_reset(void)
{
	ds_out(0);
	_delay_us(900);
	ds_out(1);
	_delay_us(10);
	if(ds_in()==0){return 1;}
	_delay_us(70);
	if(ds_in()!=0){return 2;}
	_delay_us(410);
	return 0;
}

uint8_t ds_timeslot()
{
	uint8_t time;
	ds_out(0);
	_delay_us(900);
	ds_out(1);
	_delay_us(10);
	for(time=10;time<100;time++){
		asm volatile ("nop");
		asm volatile ("nop");
		asm volatile ("nop");
		asm volatile ("nop");
		asm volatile ("nop");
		asm volatile ("nop");
		asm volatile ("nop");
		asm volatile ("nop");
		asm volatile ("nop");
		asm volatile ("nop");
		if(ds_in() == 0) break;
	}
	_delay_us(410);
	if(ds_in() == 0) return 0;
	else if(time < 99) return time;
	return 0;
}

uint8_t ds_read_rom(uint8_t* data)
{
	ds_time = ds_timeslot();
	if(ds_time < 10) return DS_READ_ROM_NO_PRES;
	ds_write_byte(0x33);
	for(uint8_t i=0;i<8;i++) data[i] = ds_read_byte();
	if(ds_crc_check(data)){
		ds_time = ds_timeslot();
		if(ds_time < 10) return DS_READ_ROM_NO_PRES;
		ds_write_byte(0x33);
		for(uint8_t i=0;i<8;i++)
			if(data[i] != ds_read_byte()) return DS_READ_ROM_NO_PRES;
		return DS_READ_ROM_CRC_ERR;
	}
	return DS_READ_ROM_OK;
}

uint8_t ds_program_RW1990_2(uint8_t* data)
{
	if(ds_reset()) return DS_READ_ROM_NO_PRES;
	ds_write_byte(0x1D);
	ds_write_bit(1);
	_delay_ms(10);
	if(ds_reset()) return DS_READ_ROM_NO_PRES;
	ds_write_byte(0xD5);
	for(unsigned char i=0;i<8;i++) ds_program_byte(data[i]);
	if(ds_reset()) return DS_READ_ROM_NO_PRES;
	ds_write_byte(0x33);
	for(uint8_t i=0;i<8;i++) if(data[i] != ds_read_byte()) return DS_READ_ROM_CRC_ERR;
	if(ds_reset()) return DS_READ_ROM_NO_PRES;
	ds_write_byte(0x1D);
	ds_write_bit(0);
	_delay_ms(10);
	return DS_READ_ROM_OK;
}

uint8_t ds_program_tm08v2(uint8_t* data)
{
	if(ds_reset()) return DS_READ_ROM_NO_PRES;
	ds_write_byte(0xD1);
	ds_write_bit(0);
	_delay_ms(10);
	if(ds_reset()) return DS_READ_ROM_NO_PRES;
	ds_write_byte(0xD5);
	for(unsigned char i=0;i<8;i++) ds_program_byte(data[i]);
	if(ds_reset()) return DS_READ_ROM_NO_PRES;
	ds_write_byte(0x33);
	for(uint8_t i=0;i<8;i++) if(data[i] != ds_read_byte()) return DS_READ_ROM_CRC_ERR;
	if(ds_reset()) return DS_READ_ROM_NO_PRES;
	ds_write_byte(0xD1);
	ds_write_bit(1);
	_delay_ms(10);
	return DS_READ_ROM_OK;
}

uint8_t ds_program_tm2004(uint8_t* data)
{
	uint8_t crc = 0x65;
	if(ds_reset()) return DS_READ_ROM_NO_PRES;
	ds_write_byte(0x3c);
	ds_write_byte(0x00);
	ds_write_byte(0x00);
	for(uint8_t i=0;i<8;i++){
		ds_write_byte(data[i]);
		crc = ds_crc(crc,data[i]);
		if(ds_read_byte() != crc) return DS_READ_ROM_CRC_ERR;
		ds_program_pulse();
		if(ds_read_byte() != data[i]) return DS_READ_ROM_CRC_ERR;
		crc = i + 1;
	}
	return DS_READ_ROM_OK;
}

uint8_t ds_program_tm01c(uint8_t* data, uint8_t type)
{
	if(ds_reset()) return DS_READ_ROM_NO_PRES;
	ds_write_byte(0xC1);
	ds_write_bit(0);
	_delay_ms(10);
	if(ds_reset()) return DS_READ_ROM_NO_PRES;
	ds_write_byte(0xC5);
	_delay_ms(10);
	for(unsigned char i=0;i<8;i++) ds_program_byte(data[i]);
	if(ds_reset()) return DS_READ_ROM_NO_PRES;
	ds_write_byte(0x33);
	for(uint8_t i=0;i<8;i++) if(data[i] != ds_read_byte()) return DS_READ_ROM_CRC_ERR;
	if(type == TM01C_DALLAS) return DS_READ_ROM_OK;
	
	if(ds_reset()) return DS_READ_ROM_NO_PRES;
	if(type == TM01C_METAKOM) ds_write_byte(0xCB);
	if(type == TM01C_CYFRAL) ds_write_byte(0xCA);
	ds_write_bit(0);
	_delay_ms(30);
	return DS_READ_ROM_OK;
}

uint8_t ds_erase_tm01c(uint8_t type)
{
	if(ds_reset()) return DS_READ_ROM_NO_PRES;
	if(type == TM01C_METAKOM) ds_write_byte(0xB4);
	if(type == TM01C_METAKOM) ds_write_byte(0xB6);
	ds_write_bit(1);
	if(ds_reset()) return DS_READ_ROM_NO_PRES;
	if(type == TM01C_METAKOM) ds_write_byte(0xCB);
	if(type == TM01C_METAKOM) ds_write_byte(0xCA);
	ds_write_bit(0);
	_delay_ms(10);
	if(ds_reset()) return DS_READ_ROM_NO_PRES;
	if(type == TM01C_METAKOM) ds_write_byte(0xB4);
	if(type == TM01C_METAKOM) ds_write_byte(0xB6);
	ds_write_bit(0);
	return DS_READ_ROM_OK;
}
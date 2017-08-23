/*
 * kt_01.c
 *
 * Created: 28.01.2016 0:09:48
 *  Author: Elektron
 */ 
#include <avr/io.h>
#include <util/delay.h>
#include "kt-01.h"

void kt_init()
{

	KT_PORT &= ~(1<<KT_LINE);
	KT_DDR &= ~(1<<KT_LINE);
	KT_PORT &= ~(1<<KT_PROG);
	KT_DDR |= 1<<KT_PROG;
	
}

uint8_t kt_crc(uint8_t* data, uint8_t len)
{
	uint8_t crc = 0;

	while (len--)
	{
		crc ^= *data++;
		for (uint8_t i=0; i<8; i++){
			if (crc & 0x01)
			crc = (crc >> 1) ^ 0x0B;
			else
			crc >>= 1;
		}
	}

	return crc;
}

uint8_t kt_crc_check(uint8_t* data)
{
	uint8_t zero = 0;
	uint8_t crc = kt_crc(data,16);

	for (uint8_t i=0;i<16;i++) if(data[i]!=0) zero++;

	for (uint8_t i=0;i<8;i++)
		if(data[i] != data[i+8]) return 1;

	if(zero==0) return 1;
	if(crc==0)return 0;
	return 2;
}

void kt_out(uint8_t databyte)
{
	if (databyte == 0){
		KT_DDR |= (1<<KT_LINE);
	}else{
		KT_DDR &= ~(1<<KT_LINE);
	}
}

uint8_t kt_in(void)
{
	return KT_PIN & (1<<KT_LINE);
}

void kt_write_byte(uint8_t data)
{
	for (uint8_t i=0;i<8;i++){
		if (data & 0x01){
			kt_out(0);
			_delay_us(5);
			kt_out(1);
		}else{
			kt_out(0);
			_delay_us(90);
			kt_out(1);
		}
		_delay_ms(13);
		data >>= 1;
	}
}

uint8_t kt_read_byte(void)
{
	uint8_t bit,result = 0;

	for (uint8_t i=0;i<8;i++){
		kt_out(0);
		_delay_us(10);
		kt_out(1);
		_delay_us(25);
		bit = kt_in();
		_delay_us(175);
		
		result >>= 1;
		if(bit) result |= 0x80;
	}
	return result;
}

uint8_t kt_reset(void)
{
	kt_out(0);
	_delay_ms(1);
	kt_out(1);
	_delay_us(20);
	if(kt_in() == 0) return 1;
	for(uint8_t i=0;;i++){
		if(kt_in() == 0) break;
		if(i > 20) return 2;
		_delay_us(10);
	}
	_delay_us(240);
	return 0;
}

uint8_t kt_read_rom(uint8_t* data)
{
	uint8_t temp[16];
	if(kt_reset()) return KT_NO_KEY;
	for(uint8_t i=0;i<16;i++) temp[i] = kt_read_byte();
	for(uint8_t i=0;i<8;i++) data[i] = temp[i];
	if(kt_crc_check(temp)){
		for(uint8_t t=0;t<8;t++){
			if(kt_reset()) return KT_NO_KEY;
			for(uint8_t i=0;i<16;i++)
				if(temp[i] != kt_read_byte()) return KT_NO_KEY;
		}
	}
	return KT_READ_ROM_OK;
}

uint8_t kt_write_rom(uint8_t* data)
{
	if(kt_reset()) return KT_NO_KEY;
	KT_PORT |= 1<<KT_PROG;
	_delay_ms(45);
	for(uint8_t i=0;i<8;i++) kt_write_byte(data[i]);
	for(uint8_t i=0;i<8;i++) kt_write_byte(data[i]);
	KT_PORT &= ~(1<<KT_PROG);
	if(kt_reset()) return KT_NO_KEY;
	for(uint8_t i=0;i<8;i++)
		if(kt_read_byte() != data[i]) return KT_CRC_ERR;
	for(uint8_t i=0;i<8;i++)
		if(kt_read_byte() != data[i]) return KT_CRC_ERR;
	return KT_READ_ROM_OK;
}
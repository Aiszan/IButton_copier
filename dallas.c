#include <avr/io.h>
#include <util/delay.h>
#include <stdint.h>
#include "dallas.h"

#define delay_A 1
#define delay_B 64
#define delay_C 60
#define delay_D 10
#define delay_E 9
#define delay_F 55
#define delay_G 2
#define delay_H 900  //480
#define delay_I 70
#define delay_J 410
#define delay_S 15

uint8_t ds_data[8] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

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
        _delay_us(delay_A);
        ds_out(1);
        _delay_us(delay_B);
    }else{
    	ds_out(0);
        _delay_us(delay_C);
        ds_out(1);
        _delay_us(delay_D);
    }
}

uint8_t ds_read_bit()
{
    uint8_t result;

    ds_out(0);
    _delay_us(delay_A);
    ds_out(1);
    _delay_us(delay_E);
    result = ds_in();
    _delay_us(delay_F);

    return result;
}

uint8_t ds_timeslot()
{
	uint8_t time;
	ds_out(0);
    _delay_us(delay_H);
    ds_out(1);
	_delay_us(delay_D);
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
		if(ds_in()==0) break;
	}
	_delay_us(250);
	if(ds_in()==0) return 0;
	else if(time < 99) return time;
	return 0;
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
		_delay_ms(delay_S);
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
    uint8_t result=0;

    for (uint8_t i=0;i<8;i++){
        result >>= 1;
        if (ds_read_bit()) result |= 0x80;
	}
	return result;
}

uint8_t ds_reset(void)
{
    ds_out(0);
    _delay_us(delay_H);
    ds_out(1);
	_delay_us(delay_D);
	if(ds_in()==0){return 1;}
    _delay_us(delay_I);
    if(ds_in()!=0){return 2;}
    _delay_us(delay_J);
	return 0;
}

uint8_t ds_read_rom(uint8_t* data)
{
	if(ds_reset()) return DS_READ_ROM_NO_PRES;
	ds_write_byte(0x33);
	for(uint8_t i=0;i<8;i++) data[i] = ds_read_byte();
	if(ds_crc_check(data)) return DS_READ_ROM_CRC_ERR;
	return DS_READ_ROM_OK;
}

uint8_t ds_program_tm08v2(uint8_t* data)
{
	//if(ds_reset()){return DS_READ_ROM_NO_PRES;}
	//ds_write_byte(0xD1);
	//_delay_ms(delay_S);
	//ds_write_bit(0);
	//_delay_ms(delay_S);
	if(ds_reset()) return DS_READ_ROM_NO_PRES;
	ds_write_byte(0xD5);
	_delay_ms(delay_S);
	for(unsigned char i=0;i<8;i++)
		ds_program_byte(data[i]);
	if(ds_reset()) return DS_READ_ROM_NO_PRES;
	ds_write_byte(0x33);
	for(uint8_t i=0;i<8;i++)
		if(data[i] != ds_read_byte()) return DS_READ_ROM_CRC_ERR;
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
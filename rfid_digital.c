#include <avr/io.h>
#include <stdint.h>
#include <util/delay.h>
#include "rfid.h"

uint8_t rfid_buffer [RFID_BUFFER_SIZE];  //Ѕуфер RFID
uint8_t rfid_code [64];

void rfid_init()
{
	TCCR0A = (1<<COM0A0)|(1<<WGM01);
	TCCR0B = (0<<CS02)|(0<<CS01)|(1<<CS00);
	OCR0A = 63;
	DDRD |= 1<<PD6;

	RFID_DDR |= 1<<RFID_OUT;
	RFID_DDR &= ~(1<<RFID_IN);
	RFID_PORT |= (1<<RFID_IN)|(1<<RFID_OUT);
}

void inline rfid_on()
{
	DDRD |= 1<<PD6;
	RFID_PORT |= 1<<RFID_OUT;
}

void inline rfid_off()
{
	DDRD &= ~(1<<PD6);
	RFID_PORT &= ~(1<<RFID_OUT);
}

void rfid_write(uint8_t bit){
	if(bit)_delay_us(250);
	_delay_us(180);
	rfid_off();
	_delay_us(160);
	rfid_on();
}

void rfid_write_block(uint8_t* data, uint8_t address)
{
	rfid_off();		//start gap
	_delay_us(240);//
	rfid_on();
	rfid_write(1); //
	rfid_write(0); // opcode + lock bit
	rfid_write(0); //
	for(uint8_t i=0;i<32;i++) rfid_write(data[i]);				//data
	for(uint8_t i=0;i<3;i++) rfid_write(address & (0x04>>i));	//address
	_delay_ms(30);		//wait for writing
}

void rfid_encode(uint8_t* data)
{
	for(uint8_t i=0;i<9;i++)rfid_code[i] = 1;
	
	for(uint8_t nibble=0;nibble<10;nibble++){
		uint8_t parity = 0;
		for(uint8_t bit=0;bit<4;bit++){
			rfid_code[nibble*5+bit+9] = data[(nibble*4+bit)/8] & (0x80>>((nibble*4+bit)%8));
			if(rfid_code[nibble*5+bit+9]) parity ^=1;
		}
		rfid_code[nibble*5+4+9] = parity;
	}
	for(uint8_t col=0;col<4;col++){
		uint8_t parity = 0;
		for(uint8_t row=0;row<10;row++){
			if(rfid_code[row*5+col+9]) parity ^= 1;
		}
		rfid_code[col+59] = parity;
	}
	rfid_code[63] = 0;
}

void rfid_config()
{
	uint8_t temp[] = {0x00,0x14,0x80,0x40};
	for(uint8_t i=0;i<32;i++)
	if(temp[i/8] & (0x80>>(i%8)))rfid_code[i] = 1;
	else rfid_code[i] = 0;
}

uint8_t rfid_in()
{
	if(RFID_PIN & 1<<RFID_IN) return 1;
	else return 0;
}

uint8_t rfid_decode()
{
	uint8_t offset=0,start=0,bit=0;
	uint8_t prev,now,next;
	
	for (uint16_t i=1;i<RFID_BUFFER_SIZE*8-1;i++){
		
		if(rfid_buffer[(i-1)/8] & 1<<((i-1)%8)) prev = 1; else prev = 0;
		if(rfid_buffer[i/8] & 1<<(i%8)) now = 1; else now = 0;
		if(rfid_buffer[(i+1)/8] & 1<<((i+1)%8)) next = 1; else next = 0;
		
		if((now == 0 && now != prev) || now == next) continue;
		if(prev == now) bit = now;
		
		rfid_code[offset] = bit;
		offset++;
		if(offset >= 64) return RFID_OK;
		
		if(start < 9){
			if(bit == 1) start++; else {start = 0; offset = 0;}
		}
	}
	return RFID_PARITY_ERR;
}

uint8_t rfid_check()
{
	for(uint8_t i=0;i<10;i++){
		uint8_t p = 0;
		for(uint8_t j=0;j<4;j++) p ^= rfid_code[i*5+j+9];
		if(rfid_code[i*5+4+9] != p) return RFID_PARITY_ERR;
	}
	for(uint8_t i=0;i<4;i++){
		uint8_t pc = 0;
		for(uint8_t j=0;j<10;j++) pc ^= rfid_code[j*5+i+9];
		if(rfid_code[i+50+9] != pc) return RFID_PARITY_ERR;
	}
	if(rfid_code[63] != 0) return RFID_PARITY_ERR;
	return RFID_OK;
}

uint8_t rfid_read(uint8_t* data)
{
	uint8_t temp, time = 0;
	for (uint8_t i=0;i<RFID_BUFFER_SIZE;i++) rfid_buffer[i] = 0;
	
	for (uint16_t i=0;i<RFID_BUFFER_SIZE*8;i++){
		temp = rfid_in();
		for(time=0;time<70;time++){
			_delay_us(10);
			if(temp != rfid_in())break;
		}
		if((time < 9) || (time > 64)) return RFID_NO_KEY;
		rfid_buffer[i / 8] |= temp << (i % 8);
		if(time > 37){
			i++;
			rfid_buffer[i / 8] |= temp << (i % 8);
		}
	}
	if(rfid_decode() != RFID_OK) return RFID_PARITY_ERR; //занимает до 3мс
	if(rfid_check() != RFID_OK) return RFID_PARITY_ERR; //занимает 50мкс

	for(uint8_t byte=0;byte<5;byte++){
		data[byte] = 0;
		for(uint8_t nibble=0;nibble<2;nibble++){
			for(uint8_t bit=0;bit<4;bit++)
			data[byte] |= rfid_code[byte*10+nibble*5+bit+9] << (7-(bit+nibble*4));
		}
	}
	return RFID_OK;
}

uint8_t rfid_force_read(uint8_t* data)
{
	uint8_t temp = 0;
	for(uint8_t i=0; i<5;i++){
		temp = rfid_read(data);
		if(temp == RFID_OK) break;
	}
	return temp;
}

uint8_t rfid_program(uint8_t* data)
{
	rfid_config();
	rfid_write_block(rfid_code, 0x00);
	rfid_encode(data);
	rfid_write_block(rfid_code, 0x01);
	rfid_write_block(rfid_code+32, 0x02);
	
	rfid_off();
	_delay_ms(20);
	rfid_on();
	_delay_ms(20);
	
	uint8_t temp = 0;
	for(uint8_t i=0;i<5;i++) data[i+8] = ~data[i];
	temp = rfid_force_read(data+8);
	if(temp != RFID_OK) return temp;
	for(uint8_t i=0;i<5;i++)if(data[i] != data[i+8]) return RFID_PARITY_ERR;
	return RFID_OK;
}
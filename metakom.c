/*
 * metakom.c
 *
 * Created: 30.01.2016 2:13:32
 *  Author: Elektron
 */ 
#include <avr/io.h>
#include <util/delay.h>
#include "metakom.h"

#define AVG_U_SUM 100
#define AVG_T_SUM 100

uint8_t mk_crc(uint8_t* data)
{
	for(uint8_t i=0;i<8;i++) data[i] = 0;				//очищаем массив кода ключа
	
	if((mk_code[0] & 0xE0) != 0b01000000) return MK_NO_KEY;	//провер€ем наличие стартового слова
	
	for(uint8_t i=0;i<8;i++)								//провер€ем совпадение двух копий кода ключа
		if(((mk_code[i/8]<<(i%8)) & 0x80) != ((mk_code[(i+35)/8]<<((i+35)%8)) & 0x80)) return MK_NO_KEY;
															
	for(uint8_t i=0;i<32;i++)								//копируем код ключа в массив
		if(mk_code[(i+3)/8] & 0x80>>((i+3)%8)) data[4-(i/8)] |= 0x80>>(i%8);
	
	for(uint8_t i=0;i<4;i++){								//провер€ем четность
		uint8_t parity = 0;
		for(uint8_t j=0;j<7;j++){
			if(data[4-i] & 0x80>>j) parity ^= 0x01;
		}
		if((data[4-i] & 0x01) != parity) return MK_NO_KEY;
	}
	return MK_READ_OK;
}

uint8_t mk_read(uint8_t* data)
{
	uint16_t sum = 0;
	uint8_t avg_t = 0, avg_u = 0, temp = 0;
	
	ADMUX = (0 << REFS1)|(1 << REFS0) 					// опорное напр€жение AVCC
	|(1 << ADLAR)										// смещение результата (влево при 1, читаем 8 бит из ADCH)
	|(MK_ADC); 											// вход MK_ADC
	_delay_us(20);
	
	for(uint8_t i=0;i<9;i++)mk_code[i] = 0;					//чистим массив приема

	for(uint8_t i=0;i<AVG_U_SUM;i++){						//определ€ем среднее напр€жение
		sum += ADCH;
		_delay_us(10);
	}
	avg_u = sum / AVG_U_SUM;
	sum = 0;
	for(uint8_t u=0,i=0;i<AVG_T_SUM;i++){					//определ€ем длительность цикла генератора ключа
		for(uint8_t t=0;t<200;t++){
			if(ADCH < avg_u && u){sum += t; u = 0;break;}
			if(ADCH > avg_u && !u){sum += t; u = 1;break;}
			_delay_us(4);
		}
	}
	avg_t = sum / AVG_T_SUM;
	if(avg_t < 5 || avg_t > 23) return MK_NO_KEY;

	for(uint8_t i=0;i<255;i++){								//ждем синхронизирующий бит
		uint8_t t;
		_delay_us(10);
		if(ADCH >= avg_u) continue;
		for (t=0;t<100;t++){
			_delay_us(10);
			if(ADCH > avg_u) break;
		}
		if(t > avg_t) {temp = 1;break;}
	}
	if(temp == 0) return MK_NO_KEY;
	
	for(uint8_t i=0;i<70;i++){							//пишем в буфер приема стартовое слово и код ключа дважды
		uint8_t t;
		for (t=0;t<=150;t++){
			if(ADCH < avg_u) break;
			_delay_us(4);
		}
		if(t == 150) return MK_NO_KEY;
		
		if(t > avg_t) mk_code[i/8] |= 0x80>>(i%8);
		
		for (t=0;t<=150;t++){
			if(ADCH > avg_u) break;
			_delay_us(4);
		}
		if(t == 150) return MK_NO_KEY;		
	}
	
	return mk_crc(data);
}
/*
 * cyfral.c
 *
 * Created: 08.02.2016 20:39:55
 *  Author: Elektron
 */ 
#include <avr/io.h>
#include <util/delay.h>
#include "cyfral.h"

#define MK_PORT PORTC
#define MK_DDR  DDRC
#define MK_LINE 0

#define ADC0 0b00000000
#define ADC1 0b00000001
#define ADC2 0b00000010
#define ADC3 0b00000011
#define ADC4 0b00000100
#define ADC5 0b00000101
#define ADC6 0b00000110
#define ADC7 0b00000111
#define ADC8 0b00001000

#define AVG_U_SUM 100
#define AVG_T_SUM 100

 uint8_t cl_code[2];
 uint8_t cl_buffer[14];

void cl_init()
{
    ADCSRA = (1 << ADEN) 								// разрешение АЦП
    |(1 << ADSC) 										// запуск преобразования
    |(1 << ADATE) 										// непрерывный режим работы АЦП
    |(0 << ADPS2)|(1 << ADPS1)|(1 << ADPS0) 			// предделитель на 8 (частота АЦП 148kHz)
    |(0 << ADIE); 										// запрет прерывания
    ADCSRB = (0 << ADTS2)|(0 << ADTS1)|(0 << ADTS0); 	// непрерывный режим работы АЦП

    ADMUX = (0 << REFS1)|(1 << REFS0) 					// опорное напряжение AVCC
    |(1 << ADLAR)										// смещение результата (влево при 1, читаем 8 бит из ADCH)
    |(ADC0); 											// вход ADC0
}

uint8_t cl_decode(uint8_t* data)
{
	uint8_t start = 0;
	data[0] = 0;
	data[1] = 0;
	cl_code[0] = 0;
	cl_code[1] = 0;	
	for(uint8_t temp=0;start<112;start++){							//находим стартовое слово
		temp = temp<<1;
		if((cl_buffer[start/8]>>(start%8)) & 0x01) temp |= 0x01;
		else temp &= ~(0x01);
		temp &= 0x0F;
		if(temp == 0x01 && start > 2){start -= 3;break;}
	}
	for(uint8_t i=0;i<36;i++){										//проверяем правильность данных
		uint8_t temp = 0;
		if((cl_buffer[(start+i)/8]>>((start+i)%8)) & 0x01) temp |= 0x01;
		if(((cl_buffer[(start+i+36)/8]>>((start+i+36)%8)) & 0x01) != temp) return CL_NO_KEY;
	}
	for(uint8_t i=4;i<36;i+=4){										//декодируем данные
		uint8_t temp = 0;
		for(uint8_t bit=0;bit<4;bit++){
			temp = temp<<1;
			if((cl_buffer[(start+i+bit)/8]>>((start+i+bit)%8)) & 0x01) temp |= 0x01;
		}
		switch(temp){
			case 0b00001110: temp = 0b00000000;break;
			case 0b00001101: temp = 0b00000001;break;
			case 0b00001011: temp = 0b00000010;break;
			case 0b00000111: temp = 0b00000011;break;
			default: return CL_NO_KEY;
		}
		cl_code[(i-4)/16] |= temp << (((i-4)%16)/2);
	}
	data[0] |= cl_code[0]<<4;									//меняем порядок нибблов
	data[0] |= cl_code[0]>>4;
	data[1] |= cl_code[1]<<4;
	data[1] |= cl_code[1]>>4;
	return CL_READ_OK;
}

uint8_t cl_read(uint8_t* data)
{
	uint16_t sum = 0;
	uint8_t avg_t = 0, avg_u = 0;
	
	for(uint8_t i=0;i<14;i++)cl_buffer[i] = 0;				//чистим массив приема

	for(uint8_t i=0;i<AVG_U_SUM;i++){						//определяем среднее напряжение
		sum += ADCH;
		_delay_us(10);
	}
	avg_u = sum / AVG_U_SUM;
	sum = 0;
	for(uint8_t u=0,i=0;i<AVG_T_SUM;i++){					//определяем длительность цикла генератора ключа
		for(uint8_t t=0;t<200;t++){
			if(ADCH < avg_u && u){sum += t; u = 0;break;}
			if(ADCH > avg_u && !u){sum += t; u = 1;break;}
			_delay_us(4);
		}
	}
	avg_t = sum / AVG_T_SUM;
	if(avg_t < 5 || avg_t > 23) return CL_NO_KEY;
	
	for(uint8_t i=0;i<112;i++){								//пишем в буфер приема стартовое слово и код ключа трижды
		uint8_t t;
		for (t=0;t<=150;t++){
			if(ADCH > avg_u) break;
			_delay_us(4);
		}
		if(t == 150) return CL_NO_KEY;
		
		for (t=0;t<=150;t++){
			if(ADCH < avg_u) break;
			_delay_us(4);
		}
		if(t == 150) return CL_NO_KEY;
		
		if(t > avg_t) cl_buffer[i/8] |= 0x01<<(i%8);
	}		
	
	return cl_decode(data);
}

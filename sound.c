/*
 * sound.c
 *
 * Created: 15.02.2016 16:01:25
 *  Author: GunMan
 */ 
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <util/delay.h>
#include "sound.h"

//Таблицы частот нот и длительностей
const uint16_t notefreq[] PROGMEM = {0,30577,28863,27243,25712,24269,22907,21621,20407,19262,18180,17161,16197,15288,14431,13621,12856,12135,11454,10811,10204,9631,9091,8581,8099,7645,7215,6811,6428,6068,5727,5405};
const uint16_t pausedelay[] PROGMEM = {32,64,128,256,512,1024,2048,4096};

// Своя задержка, так как _delay_ms на вход принимает только константу
void delay_note (uint16_t delay)
{
	for (uint16_t i=0;i<delay;i++) _delay_ms(1);
}

void sound_init()
{
	TCCR1A = 0x00;
	TCCR1B = (1<<WGM12)|(1<<CS10);//CTC mode, no prescaling
	
	SOUND_DDR |= 1<<SOUND_OUT;
	SOUND_PORT &= ~(1<<SOUND_OUT);
}

void sound_play(const uint8_t* melody)
{
	uint8_t tmp = 1;
	for(uint16_t i=0;;i++)
	{
		uint8_t freqnote, delaynote;
		tmp = pgm_read_byte(&(melody[i]));
		if(tmp == 0) return;
		freqnote = tmp & 0x1F;		// Код ноты
		delaynote = (tmp>>5) & 0x07;// Код длительности
	
		if (freqnote!=0)			// Если не пауза
		{							// включаем звук
			OCR1A = pgm_read_word(&(notefreq[freqnote]));
			TCCR1A = 1<<COM1A0;
		}

		delay_note(pgm_read_word(&(pausedelay[delaynote]))); // выдерживаем длительность ноты
		TCCR1A = 0x00; 	//выключаем звук
	}
}
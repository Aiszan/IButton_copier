#include <avr/io.h>
#include <stdint.h>
#include <util/delay.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include "uart.h"
#include "sound.h"
#include "lcd.h"
#include "dallas.h"
#include "rfid.h"
#include "kt-01.h"
#include "metakom.h"
#include "cyfral.h"
#include "mmc.h"
#include "pff.h"

#define BUF_SIZE    128UL

#define BUTTON_PORT PORTB
#define BUTTON_PIN  PINB
#define BUTTON_DDR  DDRB
#define BUTTON_LINE 0

enum enum_key{KEY_NO_KEY, KEY_DALLAS, KEY_RFID, KEY_KT01, KEY_METAKOM, KEY_MK_DAL_1, KEY_MK_DAL_2, KEY_CYFRAL, KEY_CY_DAL_1, KEY_CY_DAL_2, KEY_RESIST};
enum enum_tag{TAG_RW1990, TAG_TM08, TAG_TM2004, TAG_T5557, TAG_KT01, TAG_AUTO, TAG_DEFAULT};
enum enum_mode{MODE_DEFAULT, MODE_READ, MODE_WRITE, MODE_MENU};
enum enum_button{BUTTON_OFF, BUTTON_ON, BUTTON_HOLD};
enum enum_res{RES_READ_OK, RES_NO_PRES};
enum enum_user{USER_DEFAULT, USER_CMD};

const uint8_t sound_read[] PROGMEM = {C2+T1,D2+T1,E2+T1,F2+T1,G2+T1,MUTE};
const uint8_t sound_write[] PROGMEM = {G2+T1,F2+T1,E2+T1,D2+T1,C2+T1,MUTE};
const uint8_t sound_error[] PROGMEM = {C1+T2,T1,C1+T2,MUTE};
const uint8_t sound_exist[] PROGMEM = {C3+T2,MUTE};
const uint8_t sound_button[] PROGMEM = {F3+T1,MUTE};
const uint8_t sound_button2[] PROGMEM = {F3+T2,MUTE};
const uint8_t cy_dal_2_tbl[] PROGMEM = {0x0F, 0x0B, 0x07, 0x03, 0x0E, 0x0A, 0x06, 0x02, 0x0D, 0x09, 0x05, 0x01, 0x0C, 0x08, 0x04, 0x00};

volatile uint8_t button = BUTTON_OFF;
volatile uint8_t user_rx;
FATFS fs;
FRESULT res;
uint8_t mode = MODE_READ;
uint8_t key = KEY_NO_KEY;
uint8_t in_data[8];
uint8_t out_data[8];
uint16_t keys_seek = 0;
char	user_cmd[BUF_SIZE];
static char keys[] = "keys.csv";
//static char logs[] = "key.log";
void (*reset)() = 0;

ISR(TIMER2_OVF_vect)
{
	static uint8_t button_state = 0;

	if(uart_gets(user_cmd) == UART_OK) user_rx = USER_CMD;
		
	if(BUTTON_PIN & (1<<BUTTON_LINE)){
		button_state = 0;
		if(button == BUTTON_HOLD) button = BUTTON_OFF;
		return;
	}
	if((BUTTON_PIN & (1<<BUTTON_LINE)) == 0 && button_state == 1){
		button_state++;
		sound_play(sound_button);
		button = BUTTON_ON;
		return;
	}
	if(button_state > 60){
		if(button_state < 62){
			button_state++;
			sound_play(sound_button2);
			button = BUTTON_HOLD;
		}
		return;
	}
	button_state++;
}

void button_init()
{
	BUTTON_DDR &= ~(1 << BUTTON_LINE);
	BUTTON_PORT |= 1 << BUTTON_LINE;
	TCCR2A = 0;
	TCCR2B = (1<<CS22)|(1<<CS21)|(1<<CS20);
	TIMSK2 = (1<<TOIE2);
	sei();
}

void adc_init()
{
	ADCSRA = (1 << ADEN) 								// разрешение АЦП
	|(1 << ADSC) 										// запуск преобразования
	|(1 << ADATE) 										// непрерывный режим работы АЦП
	|(0 << ADPS2)|(1 << ADPS1)|(1 << ADPS0) 			// предделитель на 8 (частота АЦП 148kHz)
	|(0 << ADIE); 										// запрет прерывания
	ADCSRB = (0 << ADTS2)|(0 << ADTS1)|(0 << ADTS0); 	// непрерывный режим работы АЦП

	ADMUX = (0 << REFS1)|(1 << REFS0) 					// опорное напряжение AVCC
	|(1 << ADLAR)										// смещение результата (влево при 1, читаем 8 бит из ADCH)
	|(0); 												// вход ADC0
}

void view_key_type()
{
	lcd_goto_xy(1,1);
	lcd_pstr("Тип: ");
	uart_puts_pstr("Type: ");
	switch(key){
		case KEY_NO_KEY: return;
		case KEY_DALLAS: lcd_pstr("Даллас"); uart_puts_pstr("Dallas"); if(ds_crc_check(out_data)){ lcd_chr(MARK); uart_putc('*'); } break;
		case KEY_RFID: lcd_pstr("Прокси"); uart_puts_pstr("RFID"); break;
		case KEY_KT01: lcd_pstr("КТ-01"); uart_puts_pstr("KT-01"); break;
		case KEY_METAKOM: lcd_pstr("Метаком"); uart_puts_pstr("Metakom"); break;
		case KEY_MK_DAL_1: lcd_pstr("Мет.MK"); uart_puts_pstr("Metakom.MK"); break;
		case KEY_MK_DAL_2: lcd_pstr("Мет.ELC"); uart_puts_pstr("Metakom.ELC"); break;
		case KEY_CYFRAL: lcd_pstr("Цифрал"); uart_puts_pstr("Cyfral"); break;
		case KEY_CY_DAL_1: lcd_pstr("Циф.2094"); uart_puts_pstr("Cyfral.2094.1"); break;
		case KEY_CY_DAL_2: lcd_pstr("Циф.TC-01"); uart_puts_pstr("Cyfral.TC-01"); break;
		case KEY_RESIST: lcd_pstr("Резистор"); uart_puts_pstr("Resistor"); break;
	}
	uart_puts_pstr("\r\n");
}

void view_key_code()
{
	lcd_goto_xy(1,2);
	if(key == KEY_DALLAS || key == KEY_KT01 || key == KEY_CY_DAL_1 || key == KEY_CY_DAL_2 || key == KEY_MK_DAL_1 || key == KEY_MK_DAL_2){
		lcd_hex(out_data[7]);
		lcd_pstr(":CRC  FAM:");
		lcd_hex(out_data[0]);
		if(ds_time && key != KEY_KT01){
			lcd_goto_xy(1,4);
			lcd_pstr("таймслот ");
			lcd_chr(ds_time % 100 / 10 + '0');
			lcd_chr(ds_time % 10 + '0');
			lcd_chr(MU);
			lcd_chr('s');
		
			//uart_puts_pstr("timeslot ");
			//uart_putc(ds_time % 100 / 10 + '0');
			//uart_putc(ds_time % 10 + '0');
			//uart_puts_pstr(" us\r\n");
		
			ds_time = 0;
		}
	}
	lcd_goto_xy(1,3);
	for(uint8_t i=0;i<6;i++){
		lcd_hex(out_data[6-i]);
		if(i<5)lcd_sep();
	}
	
	for(uint8_t i=0;i<8;i++){
		uart_putc_hex(out_data[7-i]);
		if(i<7)uart_putc(':');
	}
	uart_puts_pstr("\r\n");
	

}

void view_write()
{
	lcd_clear();
	lcd_goto_xy(4,3);
	lcd_pstr("Записываю");
	uart_puts_pstr("Writing...\r\n");
}

void view_recorded()
{
	lcd_clear();
	lcd_goto_xy(1,3);
	lcd_pstr("\x8E уже записан");
	uart_puts_pstr("Key already recorded\r\n");
	sound_play(sound_exist);
	_delay_ms(1000);
}

void view_error()
{
	lcd_clear();
	lcd_goto_xy(1,3);
	lcd_pstr("Ошибка записи");
	uart_puts_pstr("Write error\r\n");
	sound_play(sound_error);
	_delay_ms(1000);
}

void set_mode_write()
{
	mode = MODE_WRITE;
	for(uint8_t i=0;i<8;i++) out_data[i] = in_data[i];
	sound_play(sound_read);
}

uint8_t dallas_write()
{
	uint8_t result = DS_READ_ROM_NO_PRES;
	uint8_t tag = TAG_DEFAULT;
	
	result = ds_read_rom(in_data);
	if(result == DS_READ_ROM_NO_PRES) return 1;
	view_write();
						
	for(uint8_t i=0;i<8;i++)
	if(in_data[i] != out_data[i]) result = DS_READ_ROM_NO_PRES;
	if(result != DS_READ_ROM_NO_PRES){
		view_recorded();
		return 0;
	}

	for(uint8_t i=0;i<4;i++){
		result = ds_program_tm2004(out_data);
		if(result == DS_READ_ROM_OK){ tag = TAG_TM2004; break; }
		if(result == DS_READ_ROM_NO_PRES) break;
		result = ds_program_tm08v2(out_data);
		if(result == DS_READ_ROM_OK){ tag = TAG_TM08; break; }
		if(result == DS_READ_ROM_NO_PRES) break;
		result = ds_program_RW1990_2(out_data);
		if(result == DS_READ_ROM_OK){ tag = TAG_RW1990; break; }
		if(result == DS_READ_ROM_NO_PRES) break;
	}
	if(result == DS_READ_ROM_OK){
		lcd_clear();
		lcd_goto_xy(1,3);
		if(tag == TAG_RW1990){
			lcd_pstr("RW1990");
			uart_puts_pstr("RW1990");
		}
		if(tag == TAG_TM08){
			lcd_pstr("tm08v2");
			uart_puts_pstr("tm08v2");
		}
		if(tag == TAG_TM2004){
			lcd_pstr("tm2004");
			uart_puts_pstr("tm2004");
		}
		lcd_pstr(" записан");
		uart_puts_pstr(" is recorded\r\n");
		sound_play(sound_write);
		_delay_ms(1000);
		return 0;
	}
	if(result == DS_READ_ROM_CRC_ERR){
		view_error();
		return 0;
	}
	return 0;
}

uint8_t resist_read(uint8_t* data)
{
	static uint8_t repeat = 0;
	uint8_t chr = 0, temp = 0;
	
	ADMUX = (0 << REFS1)|(1 << REFS0) 					// опорное напряжение AVCC
	|(1 << ADLAR)										// смещение результата (влево при 1, читаем 8 бит из ADCH)
	|(0); 												// вход ADC0
	_delay_us(100);
	
	if(ADCH < 0xFE){
		if(repeat < 8){
			repeat++;
			return RES_NO_PRES;
		}
		
		uint32_t u = 0;
		for(uint8_t i=0;i<128;i++){						//определяем среднее напряжение
			u += ADC>>6;
			_delay_us(10);
		}
		u = u / 128;
		
		for(uint8_t i=0;i<8;i++) data[i] = 0;
		uint32_t r = u * 1000 / (1024 - u);
		if(r > 30000) data[chr++] = '*';
		for(uint8_t i=1, zero=1;i<6;i++){
			temp = r % 100000 / 10000;
			if(temp == 0 && zero == 1 && i < 5){
				data[chr++] = ' ';
			} else {
				data[chr++] = (temp + '0');
				zero = 0;
			}
			r = r % 100000 * 10;
		}
		return RES_READ_OK;
	}
	repeat = 0;
	return RES_NO_PRES;
}

uint8_t cmd_compare(char* str, const char* progmem_str)
{
	for(uint8_t i=0;pgm_read_byte(&progmem_str[i]);i++){
		if (str[i] == 0) return 2;
		if(str[i] != pgm_read_byte(&progmem_str[i])) return 1;
	}
	return 0;
}

void cmd_parse(char* string)
{
	uint8_t _mode = MODE_DEFAULT, _key = KEY_NO_KEY;
	user_rx = USER_DEFAULT;
	
	if(cmd_compare(string, PSTR("read")) == 0){
		_mode = MODE_READ;
		string += 4;
	} else
	if(cmd_compare(string, PSTR("write")) == 0){
		_mode = MODE_WRITE;
		string += 5;
	}

	while(string[0] == ' ') string++;
	
	if(cmd_compare(string, PSTR("dallas")) == 0){
		_key = KEY_DALLAS;
		string += 6;
	} else
	if(cmd_compare(string, PSTR("rfid")) == 0){
		_key = KEY_RFID;
		string += 4;
	} else
	if(cmd_compare(string, PSTR("kt-01")) == 0){
		_key = KEY_KT01;
		string += 5;
	} else
	if(cmd_compare(string, PSTR("metakom")) == 0){
		_key = KEY_METAKOM;
		string += 7;
	} else
	if(cmd_compare(string, PSTR("cyfral")) == 0){
		_key = KEY_CYFRAL;
		string += 6;
	}
	
	while(string[0] == ' ') string++;
	
	for(uint8_t i=0;i<8;i++) in_data[i] = 0;
	
	for(uint8_t i=0,temp;string[i];i++){
		if(i/2 == 8) break;
		temp = string[i];
		in_data[i/2] = in_data[i/2]<<4;
		if(temp < '0') continue;
		if(temp <= '9') in_data[i/2] |= temp - '0';
		if(temp < 'A') continue;
		if(temp <= 'F') in_data[i/2] |= temp - 'A' + 10;
		if(temp < 'a') continue;
		if(temp <= 'f') in_data[i/2] |= temp - 'a' + 10;
	}
	
	if(_mode == MODE_DEFAULT || (_mode == MODE_WRITE && _key == KEY_NO_KEY)){
		sound_play(sound_error);
		return;
	}
	if(_mode == MODE_WRITE) for(uint8_t i=0;i<8;i++) out_data[i] = in_data[i];
	mode = _mode;
	key = _key;
	sound_play(sound_button);
}

void encode_metakom_tm01c(uint8_t* in, uint8_t* out)
{
	for(uint8_t i=0;i<8;i++) out[i] = 0;
	out[0] = 0x04;
	
	for(uint8_t i=0,byte=0,revers=0;i<8;i++){
		if((i & 0x01) == 0){
			byte = in[i/2];
			revers = byte;
			for(uint8_t j=0;j<8;j++){
				byte <<= 1;
				if((revers >> j) & 0x01) byte |= 1;
			}
			revers = byte >> 4;
			byte <<= 4;
			byte |= revers;
			out[(i+1)>>1] |= byte & 0xF0;
		} else out[(i+1)>>1] |= byte & 0x0F;
	}
	out[5] |= 0xF0;
}

void encode_cyfral_tm01c(uint8_t* in, uint8_t* out)
{
	for(uint8_t i=0;i<8;i++) out[i] = 0;
	out[0] = 0x08;
	
	for(uint8_t i=0,in_temp=0,nibble=0,out_temp=0;i<8;i++){
		if((i & 0x01) == 0){
			if((i & 0x03) == 0)in_temp = in[i/4];
			out_temp = 0;
			for(uint8_t j=0;j<2;j++){
				nibble = in_temp & 0xC0;
				nibble >>= 6;
				out_temp >>= 4;
				out_temp |= ~(0x80 >> nibble) & 0xF0;
				in_temp <<= 2;
			}
			out[(i+1)>>1] |= out_temp & 0xF0;
		} else out[(i+1)>>1] |= out_temp & 0x0F;
	}
	out[5] |= 0xF0;
}

uint8_t test_bat()
{
    ADMUX = (1 << REFS1)|(1 << REFS0) 					// опорное напряжение 1,1v
    |(1 << ADLAR)										// смещение результата (влево при 1, читаем 8 бит из ADCH)
    |(6); 											    // вход ADC6
	_delay_ms(1);
	return ADCH;
}

void keys_read()
{
	uint16_t size, i;
	
	pf_lseek(keys_seek);	
	pf_read(user_cmd, BUF_SIZE, &size);
	if(size == 0){
		keys_seek = 0;
		keys_read();
		return;
	}
	for(i=0;i<size;i++){
		if(user_cmd[i] == '\r'){
			user_cmd[i] = 0;
			keys_seek += i+2;
			break;
		}
	}

	i = 0;
	for(uint8_t byte=0,temp,nibble=0,data = 0;user_cmd[i];i++){
		temp = user_cmd[i];
		if(temp == ' ' || temp == ';' || nibble == 2){
			nibble = 0;
			out_data[7-byte] = data;
			byte++;
			data = 0;
			if(temp == ';' || byte > 7) break;
			continue;
		}
		if(nibble++ == 1) data <<= 4;
		if(temp <= '9') data |= temp - '0';
		else
		if(temp <= 'F') data |= temp - 'A' + 10;
		else
		if(temp <= 'f') data |= temp - 'a' + 10;
	}
	i++;
	key = KEY_NO_KEY;
	if(cmd_compare(&user_cmd[i], PSTR("Даллас")) == 0) key = KEY_DALLAS;
	if(cmd_compare(&user_cmd[i], PSTR("Прокси")) == 0) key = KEY_RFID;
	if(cmd_compare(&user_cmd[i], PSTR("Цифрал")) == 0) key = KEY_CYFRAL;
	if(cmd_compare(&user_cmd[i], PSTR("Метаком")) == 0) key = KEY_METAKOM;
	if(cmd_compare(&user_cmd[i], PSTR("КТ-01")) == 0) key = KEY_KT01;
	for(;user_cmd[i] != ';';i++);
	i++;
	
	lcd_clear();
	ds_time = 0;
	view_key_type();
	view_key_code();
	lcd_goto_xy(1,5);
	for(uint8_t byte=0;user_cmd[i] && byte<28;byte++){
		if(user_cmd[i++] != ';'){
			lcd_chr(user_cmd[i-1]);
		} else {
			lcd_goto_xy(1,6);
			byte = 13;
		}
	}
}

/***************************************Главная функция*********************************************/
int main (void)
{
	uint16_t temp;
	lcd_init();
	lcd_contrast(0x44);
	lcd_image();
	button_init();
	adc_init();
	uart_init();
	sound_init();
	ds_init();
	rfid_init();
	kt_init();
	for(uint8_t i=0;i<100;i++){
		res = pf_mount(&fs);
		if(res == FR_OK) break;
	}
	if(res != FR_OK) uart_puts_pstr("SDcard init ERROR\r\n");
	_delay_ms(500);
	for(;;){
		while(mode == MODE_WRITE){		//****************************************************************** WRITING
			if(key == KEY_NO_KEY){
				mode = MODE_READ;
				break;
			}
			if(key == KEY_DALLAS){		//****************************************************************** DALLAS
				lcd_clear();
				view_key_type();
				view_key_code();
				
				while(1){
					if(button == BUTTON_ON){
						button = BUTTON_OFF;
						mode = MODE_READ;
						break;
					}
					if(user_rx == USER_CMD){
						cmd_parse(user_cmd);
						break;
					}
					
					if(dallas_write() == 0) break;
				}
			}
				
			if(key == KEY_RFID){		//****************************************************************** RFID
				lcd_clear();
				view_key_type();
				view_key_code();
				
				while(1){
					uint8_t result = RFID_NO_KEY;
					
					if(button == BUTTON_ON){
						button = BUTTON_OFF;
						mode = MODE_READ;
						break;
					}
					if(user_rx == USER_CMD){
						cmd_parse(user_cmd);
						break;
					}

					result = rfid_force_read(in_data);
					if(result == RFID_OK){
						for(uint8_t i=0;i<8;i++)
						if(in_data[i] != out_data[i]) result = RFID_NO_KEY;
						if(result == RFID_OK){
							view_recorded();
							break;
						}
					}
					
					for(uint8_t i=0;i<4;i++){
						result = rfid_program(out_data);
						if(result == RFID_OK) break;
						if(result == RFID_NO_KEY) break;
					}
					if(result == RFID_OK){
						lcd_clear();
						lcd_goto_xy(1,3);
						lcd_pstr("t5557 записан");
						uart_puts_pstr("t5557 is recorded\r\n");
						sound_play(sound_write);
						_delay_ms(1000);
						break;
					}
					if(result == RFID_PARITY_ERR){
						view_error();
						break;
					}
				}
			}
				
			if(key == KEY_KT01){		//****************************************************************** KT-01
				lcd_clear();
				view_key_type();
				view_key_code();
				lcd_goto_xy(1,4);
				lcd_pstr("Подключите щуп");
				
				while(1){
					uint8_t result = KT_NO_KEY;
					
					if(button == BUTTON_ON){
						button = BUTTON_OFF;
						mode = MODE_READ;
						break;
					}
					if(user_rx == USER_CMD){
						cmd_parse(user_cmd);
						break;
					}

					result = kt_read_rom(in_data);
					if(result == KT_NO_KEY) continue;
					view_write();
					
					for(uint8_t i=0;i<8;i++)
					if(in_data[i] != out_data[i]) result = KT_NO_KEY;
					if(result != KT_NO_KEY){
						view_recorded();
						break;
					}

					for(uint8_t i=0;i<4;i++){
						result = kt_write_rom(out_data);
						if(result == KT_READ_ROM_OK) break;
						if(result == KT_NO_KEY) break;
					}
					if(result == KT_READ_ROM_OK){
						lcd_clear();
						lcd_goto_xy(1,3);
						lcd_pstr("KT-01 записан");
						uart_puts_pstr("KT-01 is recorded\r\n");
						sound_play(sound_write);
						_delay_ms(1000);
						break;
					}
					if(result == KT_CRC_ERR){
						view_error();
						break;
					}
					if(result == KT_NO_KEY) break;
				}
			}
				
			if(key == KEY_METAKOM){		//****************************************************************** METAKOM
				lcd_clear();
				view_key_type();
				view_key_code();
				
				while(1){
					//uint8_t result = DS_READ_ROM_NO_PRES;
					
					if(button == BUTTON_ON){
						button = BUTTON_OFF;
						key = KEY_MK_DAL_1;
						out_data[0] = 0x01;
						out_data[5] = 0;
						out_data[6] = 0;
						uint8_t temp = 0;
						for(uint8_t i=0;i<7;i++) temp = ds_crc(temp,out_data[i]);
						out_data[7] = temp;
						break;
					}
					if(button == BUTTON_HOLD){
						mode = MODE_READ;
						button = BUTTON_OFF;
						break;
					}
					if(user_rx == USER_CMD){
						cmd_parse(user_cmd);
						break;
					}
					
/*
					if(mk_read(in_data) == MK_READ_OK)	ds_erase_tm01c(TM01C_METAKOM);
					if(ds_read_rom(in_data) == DS_READ_ROM_NO_PRES) continue;
					view_write();
					encode_metakom_tm01c(out_data, in_data);

					for(uint8_t i=0;i<4;i++){
						result = ds_program_tm01c(in_data, TM01C_METAKOM);
						if(result == DS_READ_ROM_OK) break;
						if(result == DS_READ_ROM_NO_PRES) break;
					}
					if(result == DS_READ_ROM_OK){
						lcd_clear();
						lcd_goto_xy(1,3);
						lcd_pstr("TM-01C записан");
						uart_puts_pstr("TM-01C is recorded\r\n");
						sound_play(sound_write);
						_delay_ms(1000);
						break;
					}
					if(result == DS_READ_ROM_CRC_ERR){
						view_error();
						break;
					}
					if(result == DS_READ_ROM_NO_PRES) break;*/
				}
			}
			
			if(key == KEY_MK_DAL_1 || key == KEY_MK_DAL_2){		//****************************************** METAKOM 2
				lcd_clear();
				view_key_type();
				view_key_code();
				
				while(1){
					if(button == BUTTON_ON){
						button = BUTTON_OFF;
						if(key == KEY_MK_DAL_1){
							key = KEY_MK_DAL_2;
							for(uint8_t i=1;i<3;i++){
								temp = out_data[i];
								out_data[i] = out_data[5-i];
								out_data[5-i] = temp;
							}
							temp = 0;
							for(uint8_t i=0;i<7;i++) temp = ds_crc(temp,out_data[i]);
							out_data[7] = temp;
							break;
						} else {
							key = KEY_METAKOM;
							for(uint8_t i=1;i<3;i++){
								temp = out_data[i];
								out_data[i] = out_data[5-i];
								out_data[5-i] = temp;
							}
							break;
						}
					}
					if(button == BUTTON_HOLD){
						mode = MODE_READ;
						button = BUTTON_OFF;
						break;
					}
					if(user_rx == USER_CMD){
						cmd_parse(user_cmd);
						break;
					}
					
					if(dallas_write() == 0) break;
				}
			}
				
			if(key == KEY_CYFRAL){		//****************************************************************** CYFRAL
				lcd_clear();
				view_key_type();
				view_key_code();
				
				while(1){
					//uint8_t result = DS_READ_ROM_NO_PRES;
					
					if(button == BUTTON_ON){
						button = BUTTON_OFF;
						key = KEY_CY_DAL_1;
						out_data[0] = 0x01;
						temp = 0;
						for(uint8_t i=0;i<7;i++) temp = ds_crc(temp,out_data[i]);
						out_data[7] = temp;
						break;
					}
					if(button == BUTTON_HOLD){
						mode = MODE_READ;
						button = BUTTON_OFF;
						break;
					}
					if(user_rx == USER_CMD){
						cmd_parse(user_cmd);
						break;
					}
					
/*
					if(cl_read(in_data) == CL_READ_OK)	ds_erase_tm01c(TM01C_CYFRAL);
					if(ds_read_rom(in_data) == DS_READ_ROM_NO_PRES) continue;
					view_write();
					encode_cyfral_tm01c(out_data, in_data);

					for(uint8_t i=0;i<4;i++){
						result = ds_program_tm01c(in_data, TM01C_CYFRAL);
						if(result == DS_READ_ROM_OK) break;
						if(result == DS_READ_ROM_NO_PRES) break;
					}
					if(result == DS_READ_ROM_OK){
						lcd_clear();
						lcd_goto_xy(1,3);
						lcd_pstr("TM-01C записан");
						uart_puts_pstr("TM-01C is recorded\r\n");
						sound_play(sound_write);
						_delay_ms(1000);
						break;
					}
					if(result == DS_READ_ROM_CRC_ERR){
						view_error();
						break;
					}
					if(result == DS_READ_ROM_NO_PRES) break;*/
				}
			}
			
			if(key == KEY_CY_DAL_1 || key == KEY_CY_DAL_2){		//************************************************************** CYFRAL2
				lcd_clear();
				view_key_type();
				view_key_code();
				
				while(1){
					if(button == BUTTON_ON){
						button = BUTTON_OFF;
						if(key == KEY_CY_DAL_1){
							key = KEY_CY_DAL_2;
							for(uint8_t i=1;i<3;i++){
								temp = out_data[i];
								out_data[i] = 0;
								out_data[i] |= pgm_read_byte(&cy_dal_2_tbl[temp & 0x0F]);						
								out_data[i] |= pgm_read_byte(&cy_dal_2_tbl[(temp >> 4) & 0x0F]) << 4;
							}
							out_data[3] = 0x80;
							temp = 0;
							for(uint8_t i=0;i<7;i++) temp = ds_crc(temp,out_data[i]);
							out_data[7] = temp;
							break;
						} else if(key == KEY_CY_DAL_2){
							key = KEY_CYFRAL;
							for(uint8_t i=1;i<3;i++){
								temp = out_data[i];
								out_data[i] = 0;
								out_data[i] |= pgm_read_byte(&cy_dal_2_tbl[temp & 0x0F]);
								out_data[i] |= pgm_read_byte(&cy_dal_2_tbl[(temp >> 4) & 0x0F]) << 4;
							}
							out_data[3] = 0x01;
							break;							
						}
					}
					if(button == BUTTON_HOLD){
						mode = MODE_READ;
						button = BUTTON_OFF;
						break;
					}
					if(user_rx == USER_CMD){
						cmd_parse(user_cmd);
						break;
					}
					
					if(dallas_write() == 0) break;
				}
			}
			if(key >= KEY_RESIST) mode = MODE_READ;
		}
		while(mode == MODE_READ){		//****************************************************************** READING
			lcd_clear();
			lcd_goto_xy(11,1);
			temp = test_bat()*162;
			if(temp == 0){
				lcd_pstr("USB");
			}else{
				lcd_chr(temp % 10000 / 1000 + '0');
				lcd_chr(',');
				lcd_chr(temp % 1000 / 100 + '0');
				lcd_chr('в');
			}
			lcd_goto_xy(4,3);
			lcd_pstr("Жду ключ \x8E");
			uart_puts_pstr("Wait for a key\r\n");
			
			while(1){
				if(ds_read_rom(in_data) != DS_READ_ROM_NO_PRES){
					key = KEY_DALLAS;
					set_mode_write();
					break;
				}
				
				if(rfid_read(in_data) == RFID_OK){
					key = KEY_RFID;
					set_mode_write();
					break;
				}
				
				if(kt_read_rom(in_data) == KT_READ_ROM_OK){
					key = KEY_KT01;
					set_mode_write();
					break;
				}
				
				if(mk_read(in_data) == MK_READ_OK){
					key = KEY_METAKOM;
					set_mode_write();
					break;
				}
				
				if(cl_read(in_data) == CL_READ_OK){
					key = KEY_CYFRAL;
					set_mode_write();
					break;
				}
				
				if(resist_read(in_data) == RES_READ_OK){
					lcd_clear();
					view_key_type();
					lcd_goto_xy(4,3);
					lcd_str((char*)in_data);
					uart_puts((char*)in_data);
					lcd_pstr(" Ом");
					uart_puts_pstr(" Ohm\r\n");
					sound_play(sound_read);
					while(button == BUTTON_OFF);
					break;
				}
				if(button == BUTTON_ON){
					button = BUTTON_OFF;
					mode = MODE_MENU;
					break;
				}
				if(button == BUTTON_HOLD){
					button = BUTTON_OFF;
					break;
				}
				if(user_rx == USER_CMD){
					cmd_parse(user_cmd);
					break;
				}
			}
		}
		while(mode == MODE_MENU){
			res = pf_open(keys);
			if(res != FR_OK){
				uart_puts_pstr("Can't open keys\r\n");
				mode = MODE_READ;
				break;
			}
			uint16_t time = 0;
			keys_seek = 0;
			button = BUTTON_ON;
			while(1){
				time++;
				_delay_ms(10);
				if(time > 500){
					mode = MODE_WRITE;
					sound_play(sound_read);
					break;
				}
				if(button == BUTTON_ON){
					button = BUTTON_OFF;
					keys_read();
					time = 0;
				}
				if(button == BUTTON_HOLD){
					keys_read();
					_delay_ms(200);
					time = 0;
				}
				if(user_rx == USER_CMD){
					cmd_parse(user_cmd);
					break;
				}
			}
		}
		if(mode != MODE_READ && mode != MODE_WRITE) mode = MODE_READ;
	}
}

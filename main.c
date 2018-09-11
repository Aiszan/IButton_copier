#include <avr/io.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <util/delay.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include "sound.h"
#include "lcd.h"
#include "dallas.h"
#include "rfid.h"
#include "kt-01.h"
#include "metakom.h"
#include "cyfral.h"
#include "i2c.h"
#include "sd_raw.h"
#include "partition.h"
#include "fat.h"
//раскомментировать если нужна связь по UART
//#define UART

#ifdef UART
#include "uart.h"
#endif // UART

//максимальный размер прошивки для ардуины 30704 байт
#define FILE_BUF_SIZE    64UL

#define BUTTON_PORT PORTB
#define BUTTON_PIN  PINB
#define BUTTON_DDR  DDRB
#define BUTTON_LINE 0

enum enum_key{KEY_NO_KEY, KEY_DALLAS, KEY_RFID, KEY_KT01, KEY_METAKOM, KEY_MK_DAL_1, KEY_MK_DAL_2, KEY_CYFRAL, KEY_CY_DAL_1, KEY_CY_DAL_2, KEY_RESIST};
enum enum_tag{TAG_RW1990, TAG_TM08, TAG_TM2004, TAG_T5557, TAG_KT01, TAG_AUTO, TAG_DEFAULT};
enum enum_mode{MODE_DEFAULT, MODE_MENU, MODE_WRITE, MODE_READ, MODE_LIST, MODE_RAND_DALLAS, MODE_RAND_PROXY, MODE_LOG, MODE_CLEAR, MODE_TO_PAGE_2,\
			   MODE_EEPROM_24C16, MODE_24C16_TO_FILE, MODE_EEPROM_24C64, MODE_24C64_TO_FILE, MODE_END};
enum enum_button{BUTTON_OFF, BUTTON_ON, BUTTON_HOLD};
enum enum_res{RES_READ_OK, RES_NO_PRES};
enum enum_user{USER_DEFAULT, USER_CMD};

const uint8_t sound_read[] PROGMEM = {C2+T1,D2+T1,E2+T1,F2+T1,G2+T1,MUTE};
const uint8_t sound_write[] PROGMEM = {G2+T1,F2+T1,E2+T1,D2+T1,C2+T1,MUTE};
const uint8_t sound_error[] PROGMEM = {C1+T2,T1,C1+T2,MUTE};
const uint8_t sound_exist[] PROGMEM = {C3+T2,MUTE};
const uint8_t sound_button[] PROGMEM = {F3+T1,MUTE};
const uint8_t sound_button2[] PROGMEM = {D3+T1,MUTE};
const uint8_t sound_timer[] PROGMEM = {C3+T2,C2+T2,MUTE+T2,C3+T2,C2+T2,MUTE};
const uint8_t cy_dal_2_tbl[] PROGMEM = {0x0F, 0x0B, 0x07, 0x03, 0x0E, 0x0A, 0x06, 0x02, 0x0D, 0x09, 0x05, 0x01, 0x0C, 0x08, 0x04, 0x00};

#ifdef UART
volatile uint8_t user_rx;
char	user_cmd[64];
#endif // UART
volatile uint8_t button = BUTTON_OFF;
uint8_t mode = MODE_READ;
uint8_t mode_loop = MODE_WRITE;
uint8_t key = KEY_NO_KEY;
uint8_t in_data[8];
uint8_t out_data[8];
char	file_buf[FILE_BUF_SIZE];
int32_t file_seek;
uint32_t file_size;
static char keys[] = "keys.csv";
static char logs[] = "log.csv";
static char eeprom[] = "eeprom___.bin";
//static char eename[16];
struct partition_struct* partition;
struct fat_dir_entry_struct directory;
struct fat_fs_struct* fs;
struct fat_dir_struct* dd;
struct fat_file_struct* fd;
void (*reset)() = 0;

uint16_t test_bat()
{
	ADMUX = (1 << REFS1)|(1 << REFS0) 					// опорное напряжение 1,1v
	|(1 << ADLAR)										// смещение результата (влево при 1, читаем 8 бит из ADCH)
	|(6); 											    // вход ADC6
	_delay_ms(1);
	return ADCH*162;
}

ISR(TIMER2_OVF_vect)									//опрос кнопки
{
	static uint8_t button_state = 0;
	static uint16_t timer = 0;
	
	#ifdef UART
	if(uart_gets(user_cmd) == UART_OK) user_rx = USER_CMD;
	#endif // UART
	
	if(BUTTON_PIN & (1<<BUTTON_LINE)){					//кнопка отпущена
		if(button == BUTTON_OFF){
			if(button_state >= 60) button_state = 0;
			if(button_state > 1){
				button_state = 0;
				button = BUTTON_ON;
				sound_play(sound_button);
			}
		}else if(button == BUTTON_HOLD && mode == MODE_LIST) button = BUTTON_OFF;
	}else{												//кнопка нажата
		if(button_state == 60){
			button_state++;
			button = BUTTON_HOLD;
			sound_play(sound_button2);
		}
		if(button_state < 60)button_state++;
		timer = 0;
	}
	if(timer > 20000){									//таймер бездействия, 5 минут
		if(test_bat() < 600){timer = 0; return;}
		timer -= 400;
		sound_play(sound_timer);
	}
	timer++;
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

uint8_t find_file_in_dir(struct fat_fs_struct* fs, struct fat_dir_struct* dd, const char* name, struct fat_dir_entry_struct* dir_entry)
{
	while(fat_read_dir(dd, dir_entry))
	{
		if(strcmp(dir_entry->long_name, name) == 0)
		{
			file_size = dir_entry->file_size;
			fat_reset_dir(dd);
			return 1;
		}
	}

	return 0;
}

struct fat_file_struct* open_file_in_dir(struct fat_fs_struct* fs, struct fat_dir_struct* dd, const char* name)
{
	struct fat_dir_entry_struct file_entry;
	if(!find_file_in_dir(fs, dd, name, &file_entry))
	return 0;

	return fat_open_file(fs, &file_entry);
}

uint8_t file_init()
{
	/* setup sd card slot */
    if(!sd_raw_init())
    {
		#ifdef UART
		uart_puts_pstr("MMC/SD initialization failed\r\n");
		#endif // UART
		return 1;
	}

	/* open first partition */
	partition = partition_open(sd_raw_read, sd_raw_read_interval, sd_raw_write, sd_raw_write_interval, 0);

	if(!partition)
	{
    /* If the partition did not open, assume the storage device
    * is a "superfloppy", i.e. has no MBR.
    */
		partition = partition_open(sd_raw_read, sd_raw_read_interval, sd_raw_write, sd_raw_write_interval, -1);
		if(!partition){
			#ifdef UART
			uart_puts_pstr("opening partition failed\r\n");
			#endif // UART
			return 2;
		}
	}

	/* open file system */
	fs = fat_open(partition);
	if(!fs){
		#ifdef UART
		uart_puts_pstr("opening filesystem failed\r\n");
		#endif // UART
		return 3;
	}

    /* open root directory */
    fat_get_dir_entry_of_path(fs, "/", &directory);

    dd = fat_open_dir(fs, &directory);
    if(!dd){
		#ifdef UART
        uart_puts_pstr("opening root directory failed\r\n");
		#endif // UART
		return 4;
    }

    /* search file in current directory and open it */
	fd = open_file_in_dir(fs, dd, logs);
    if(!fd)
    {
		if(!fat_create_file(dd, logs, &directory))
		{
			#ifdef UART
			uart_puts_pstr("error opening file: log.csv\r\n");
			#endif // UART
			return 5;
		}
	}
	fat_close_file(fd);
    /* search file in current directory and open it */
    fd = open_file_in_dir(fs, dd, keys);
    if(!fd)
    {
		#ifdef UART
		uart_puts_pstr("error opening file: keys.csv\r\n");
		#endif // UART
		return 6;
    }
    fat_close_file(fd);
	return 0;
}

void view_key_type()
{
	lcd_goto_xy(1,1);
	lcd_pstr("Тип: ");
	#ifdef UART
	uart_puts_pstr("Type: ");
	#endif // UART
	switch(key){
		case KEY_NO_KEY: return;
		case KEY_DALLAS:{
			lcd_pstr("Даллас");
			#ifdef UART
			uart_puts_pstr("Dallas");
			#endif // UART
			if(ds_crc_check(out_data)){
				lcd_chr(MARK);
				#ifdef UART
				uart_putc('*');
				#endif // UART
			}
			break;
		} 
		case KEY_RFID:{
			lcd_pstr("Прокси");
			#ifdef UART
			uart_puts_pstr("RFID");
			#endif // UART
			break;
		} 
		case KEY_KT01:{
			lcd_pstr("КТ-01");
			#ifdef UART
			uart_puts_pstr("KT-01");
			#endif // UART
			if(kt_crc(out_data,8)){
				lcd_chr(MARK);
				#ifdef UART
				uart_putc('*');
				#endif // UART
			}
			break;
		}
		case KEY_METAKOM:{
			lcd_pstr("Метаком");
			#ifdef UART
			uart_puts_pstr("Metakom");
			#endif // UART
			break;
		}
		case KEY_MK_DAL_1:{
			lcd_pstr("Мет.MK99");
			#ifdef UART
			uart_puts_pstr("Metakom.MK99");
			#endif // UART
			break;
		}
		case KEY_MK_DAL_2:{
			lcd_pstr("Мет.ELC");
			#ifdef UART
			uart_puts_pstr("Metakom.ELC");
			#endif // UART
			break;
		}
		case KEY_CYFRAL:{
			lcd_pstr("Цифрал");
			#ifdef UART
			uart_puts_pstr("Cyfral");
			#endif // UART
			break;
		}
		case KEY_CY_DAL_1:{
			lcd_pstr("Циф.2094");
			#ifdef UART
			uart_puts_pstr("Cyfral.2094.1");
			#endif // UART
			break;
		}
		case KEY_CY_DAL_2:{
			lcd_pstr("Циф.TC-01");
			#ifdef UART
			uart_puts_pstr("Cyfral.TC-01");
			#endif // UART
			break;
		}
		case KEY_RESIST:{
			lcd_pstr("Резистор");
			#ifdef UART
			uart_puts_pstr("Resistor");
			#endif // UART
			break;
		}
	}
	#ifdef UART
	uart_puts_pstr("\r\n");
	#endif // UART
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
		
			#ifdef UART
			uart_puts_pstr("timeslot ");
			uart_putc(ds_time % 100 / 10 + '0');
			uart_putc(ds_time % 10 + '0');
			uart_puts_pstr(" us\r\n");
			#endif // UART
		
			ds_time = 0;
		}
	}
	lcd_goto_xy(1,3);
	for(uint8_t i=0;i<6;i++){
		lcd_hex(out_data[6-i]);
		if(i<5)lcd_sep();
	}
	
	#ifdef UART
	for(uint8_t i=0;i<8;i++){
		uart_putc_hex(out_data[7-i]);
		if(i<7)uart_putc(':');
	}
	uart_puts_pstr("\r\n");
	#endif // UART
	

}

void view_menu(uint8_t new_mode)
{
	lcd_clear();
	if(new_mode <= MODE_TO_PAGE_2){
		lcd_pstr(" Список       ");
		lcd_pstr(" Рандом Даллас");
		lcd_pstr(" Рандом Прокси");
		lcd_pstr(" Смотреть лог ");
		lcd_pstr(" Очистить лог ");
		lcd_pstr(" Дальше...    ");
	}else{
		lcd_pstr(" Читать 24С16 ");
		lcd_pstr(" 24С16 в файл ");
		lcd_pstr(" Читать 24С64 ");
		lcd_pstr(" 24С64 в файл ");
	}
	if(new_mode <= MODE_TO_PAGE_2)lcd_goto_xy(1,new_mode-MODE_LIST+1);
	else lcd_goto_xy(1,new_mode-MODE_TO_PAGE_2);
	lcd_chr(ARROW_RIGHT);
}

void view_write()
{
	lcd_clear();
	lcd_goto_xy(4,3);
	lcd_pstr("Записываю");
	#ifdef UART
	uart_puts_pstr("Writing...\r\n");
	#endif // UART
}

void view_recorded()
{
	lcd_clear();
	lcd_goto_xy(1,3);
	lcd_pstr("\x8E уже записан");
	#ifdef UART
	uart_puts_pstr("Key already recorded\r\n");
	#endif // UART
	sound_play(sound_exist);
	_delay_ms(1000);
}

void view_error()
{
	lcd_clear();
	lcd_goto_xy(1,3);
	lcd_pstr("Ошибка записи");
	#ifdef UART
	uart_puts_pstr("Write error\r\n");
	#endif // UART
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
			#ifdef UART
			uart_puts_pstr("RW1990");
			#endif // UART
		}
		if(tag == TAG_TM08){
			lcd_pstr("tm08v2");
			#ifdef UART
			uart_puts_pstr("tm08v2");
			#endif // UART
		}
		if(tag == TAG_TM2004){
			lcd_pstr("tm2004");
			#ifdef UART
			uart_puts_pstr("tm2004");
			#endif // UART
		}
		lcd_pstr(" записан");
		#ifdef UART
		uart_puts_pstr(" is recorded\r\n");
		#endif // UART
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

void str_add_p(char* buffer, const char *progmem_s)
{
	register char c;

	for(uint16_t i=0;;i++){
		c = pgm_read_byte(progmem_s++);
		buffer[i] = c;
		if(!c) break;
	}
}

void str_putdw_dec(char* string, uint32_t dw)
{
	uint32_t num = 1000000000;
	uint8_t started = 0;
	uint8_t position = 0;

	while(num > 0)
	{
		uint8_t b = dw / num;
		if(b > 0 || started || num == 1)
		{
			string[position++] = '0' + b;
			started = 1;
		}
		dw -= b * num;

		num /= 10;
	}
	string[position] = 0;
}

void str_put_hex(char* string, uint8_t hex)
{
	/* upper nibble */
	if((hex >> 4) < 0x0a)
	string[0] = ((hex >> 4) + '0');
	else
	string[0] = (hex >> 4) - 0x0a + 'A';

	/* lower nibble */
	if((hex & 0x0f) < 0x0a)
	string[1] = (hex & 0x0f) + '0';
	else
	string[1] = (hex & 0x0f) - 0x0a + 'A';

	string[2] = 0;
}

uint8_t cmd_compare(char* str, const char* progmem_str)
{
	for(uint8_t i=0;pgm_read_byte(&progmem_str[i]);i++){
		if (str[i] == 0) return 2;
		if(str[i] != pgm_read_byte(&progmem_str[i])) return 1;
	}
	return 0;
}

#ifdef UART
void cmd_parse(char* string)
{
	uint8_t _mode = MODE_DEFAULT, _key = KEY_NO_KEY;
	#ifdef UART
	user_rx = USER_DEFAULT;
	#endif // UART
	
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
#endif // UART

uint8_t file_read(char* file, uint8_t search)
{
	uint16_t size, i;
	static uint8_t street[14];
	uint8_t skip = 0;
	do{
		fd = open_file_in_dir(fs, dd, file);
		fat_seek_file(fd, &file_seek, FAT_SEEK_SET);
		size = fat_read_file(fd, (uint8_t*)file_buf, sizeof(file_buf));
		fat_close_file(fd);
		if(size == 0){
			file_seek = 0;
			return 1;
		}
		for(i=0;i<size;i++){
			if(file_buf[i] == '\r'){
				file_buf[i] = 0;
				file_seek += i+2;
				break;
			}
		}

		i = 0;
		for(uint8_t byte=0,temp,nibble=0,data = 0;file_buf[i];i++){
			temp = file_buf[i];
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
		if(cmd_compare(&file_buf[i], PSTR("Даллас")) == 0) key = KEY_DALLAS;
		if(cmd_compare(&file_buf[i], PSTR("Прокси")) == 0) key = KEY_RFID;
		if(cmd_compare(&file_buf[i], PSTR("Цифрал")) == 0) key = KEY_CYFRAL;
		if(cmd_compare(&file_buf[i], PSTR("Метаком")) == 0) key = KEY_METAKOM;
		if(cmd_compare(&file_buf[i], PSTR("КТ-01")) == 0) key = KEY_KT01;
		for(;file_buf[i] != ';';i++);
		i++;
		for(uint8_t byte=0; file_buf[i+byte] && byte<14;byte++){
			if(file_buf[i+byte]==';')break;
			if(street[byte] != file_buf[i+byte]) search = 0;
		}
		skip++;
		if(skip > 20) search = 0;
	}while(search);

	lcd_clear();
	ds_time = 0;
	view_key_type();
	view_key_code();
	
	for(uint8_t byte=0;file_buf[i+byte] && byte<14;byte++){
		if(file_buf[i+byte]==';')break;
		street[byte] = file_buf[i+byte];
	}
	lcd_goto_xy(1,5);
	for(uint8_t byte=0;file_buf[i] && byte<28;byte++){
		if(file_buf[i++] != ';'){
			lcd_chr(file_buf[i-1]);
		} else {
			lcd_goto_xy(1,6);
			byte = 13;
		}
	}
	return 0;
}

void logs_write()
{
	for(uint8_t i=0;i<8;i++){
		str_put_hex(file_buf+i*3, out_data[7-i]);
		file_buf[i*3+2] = ' ';
	}
	switch(key){
		case KEY_DALLAS: str_add_p(file_buf+23, PSTR(";Даллас;")); break;
		case KEY_RFID: str_add_p(file_buf+23, PSTR(";Прокси;")); break;
		case KEY_KT01: str_add_p(file_buf+23, PSTR(";КТ-01;")); break;
		case KEY_METAKOM: str_add_p(file_buf+23,PSTR(";Метаком;")); break;
		case KEY_CYFRAL: str_add_p(file_buf+23, PSTR(";Цифрал;")); break;
		default: return;
	}
	int32_t offset = 0;
	fd = open_file_in_dir(fs, dd, logs);
	if(!fat_seek_file(fd, &offset, FAT_SEEK_END))
	{
		fat_close_file(fd);
		return;
	}

	uint8_t size = strlen(file_buf);
	str_putdw_dec(file_buf+size, file_size/35);
	size = strlen(file_buf);
	str_add_p(file_buf+size, PSTR(";\r\n"));

	/* write text to file */
	uint16_t data_len = strlen(file_buf);
	if(fat_write_file(fd, (uint8_t*) file_buf, data_len) != data_len)
	{
		fat_close_file(fd);
		return;
	}
	fat_close_file(fd);
	return;
}

/***************************************Главная функция*********************************************/
int main (void)
{
	uint16_t temp;
	lcd_init();
	lcd_contrast(0x3D);
	lcd_image();
	button_init();
	adc_init();
	#ifdef UART
	uart_init();
	#endif // UART
	sound_init();
	ds_init();
	rfid_init();
	kt_init();
	file_init();
	i2c_init();
	_delay_ms(500);
	for(;;){
		while(mode == MODE_WRITE){		//****************************************************************** WRITING
			if(key == KEY_NO_KEY){
				mode = MODE_READ;
				break;
			}
			
			lcd_clear();
			view_key_type();
			view_key_code();
			logs_write();
			
			if(key == KEY_DALLAS){		//****************************************************************** WRITE DALLAS
				while(1){
					if(button != BUTTON_OFF){
						button = BUTTON_OFF;
						mode = MODE_READ;
						break;
					}
					#ifdef UART
					if(user_rx == USER_CMD){
						cmd_parse(user_cmd);
						break;
					}
					#endif // UART
					
					if(dallas_write() == 0){
						mode = mode_loop;
						break;
					}
				}
			}
				
			if(key == KEY_RFID){		//****************************************************************** WRITE RFID
				while(1){
					uint8_t result = RFID_NO_KEY;
					uint8_t key_type = 0;
					
					if(button != BUTTON_OFF){
						button = BUTTON_OFF;
						mode = MODE_READ;
						break;
					}
					#ifdef UART
					if(user_rx == USER_CMD){
						cmd_parse(user_cmd);
						break;
					}
					#endif // UART

					if(rfid_check(out_data) == RFID_OK){
						view_recorded();
						break;
					}
					
					for(uint8_t i=0;i<4;i++){
						key_type = 0;
						result = rfid_em4305_write(out_data);
						if(result == RFID_OK) break;
						key_type = 1;
						result = rfid_t5557_write(out_data);
						if(result == RFID_OK) break;
						if(result == RFID_NO_KEY) break;
					}
			
					if(result == RFID_OK){
						lcd_clear();
						lcd_goto_xy(1,3);
						if(key_type == 0){
							lcd_pstr("EM4305 записан");
							#ifdef UART
							uart_puts_pstr("EM4305 is recorded\r\n");
							#endif // UART
						} else {
							lcd_pstr("T5557 записан");
							#ifdef UART
							uart_puts_pstr("t5557 is recorded\r\n");
							#endif // UART
						}
						sound_play(sound_write);
						_delay_ms(1000);
						mode = mode_loop;
						break;
					}
					if(result == RFID_MISMATCH){
						view_error();
						break;
					}
				}
			}
				
			if(key == KEY_KT01){		//****************************************************************** WRITE KT-01
				lcd_goto_xy(1,4);
				lcd_pstr("Подключите щуп");
				
				while(1){
					uint8_t result = KT_NO_KEY;
					
					if(button != BUTTON_OFF){
						button = BUTTON_OFF;
						mode = MODE_READ;
						break;
					}
					#ifdef UART
					if(user_rx == USER_CMD){
						cmd_parse(user_cmd);
						break;
					}
					#endif // UART

					result = kt_read_rom(in_data);
					if(result == KT_NO_KEY) continue;
					view_write();

					if(result == KT_READ_ROM_OK){
						for(uint8_t i=0;i<8;i++) if(in_data[i] != out_data[i]) result = KT_NO_KEY;
						if(result == KT_READ_ROM_OK){
							view_recorded();
							break;
						}
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
						#ifdef UART
						uart_puts_pstr("KT-01 is recorded\r\n");
						#endif // UART
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
				
			if(key == KEY_METAKOM){		//****************************************************************** WRITE METAKOM
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
						key = KEY_NO_KEY;
						button = BUTTON_OFF;
						break;
					}
					#ifdef UART
					if(user_rx == USER_CMD){
						cmd_parse(user_cmd);
						break;
					}
					#endif // UART
				}
			}
			
			if(key == KEY_MK_DAL_1 || key == KEY_MK_DAL_2){		//****************************************** WRITE METAKOM 2
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
					#ifdef UART
					if(user_rx == USER_CMD){
						cmd_parse(user_cmd);
						break;
					}
					#endif // UART
					
					if(dallas_write() == 0) break;
				}
			}
				
			if(key == KEY_CYFRAL){		//****************************************************************** WRITE CYFRAL
				while(1){					
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
					#ifdef UART
					if(user_rx == USER_CMD){
						cmd_parse(user_cmd);
						break;
					}
					#endif // UART
				}
			}
			
			if(key == KEY_CY_DAL_1 || key == KEY_CY_DAL_2){		//************************************************************** WRITE CYFRAL2
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
						} else {
							key = KEY_CYFRAL;
							for(uint8_t i=1;i<3;i++){
								temp = out_data[i];
								out_data[i] = 0;
								out_data[i] |= pgm_read_byte(&cy_dal_2_tbl[temp & 0x0F]);
								out_data[i] |= pgm_read_byte(&cy_dal_2_tbl[(temp >> 4) & 0x0F]) << 4;
							}
							out_data[0] = 0x00;
							out_data[3] = 0x01;
							out_data[7] = 0x00;
							break;
						}
					}
					if(button == BUTTON_HOLD){
						mode = MODE_READ;
						button = BUTTON_OFF;
						break;
					}
					#ifdef UART
					if(user_rx == USER_CMD){
						cmd_parse(user_cmd);
						break;
					}
					#endif // UART
					
					if(dallas_write() == 0) break;
				}
			}
			if(key >= KEY_RESIST) mode = MODE_READ;
		}
		while(mode == MODE_READ){		//****************************************************************** READING
			mode_loop = MODE_WRITE;
			lcd_clear();			
			fd = open_file_in_dir(fs, dd, keys);
			if(!fd){
				#ifdef UART
				uart_puts_pstr("Can't open keys\r\n");
				#endif // UART
			} else {
				lcd_goto_xy(1,1);
				lcd_pstr("SD");
			}
			fat_close_file(fd);

			lcd_goto_xy(11,1);
			temp = test_bat();
			if(temp < 600){
				lcd_pstr("USB");
			}else{
				lcd_chr(temp % 10000 / 1000 + '0');
				lcd_chr(',');
				lcd_chr(temp % 1000 / 100 + '0');
				lcd_chr('в');
			}
			lcd_goto_xy(4,3);
			lcd_pstr("Жду ключ \x8E");
			#ifdef UART
			uart_puts_pstr("Wait for a key\r\n");
			#endif // UART
			
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
				
				if(kt_read_rom(in_data) != KT_NO_KEY){
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
					sound_play(sound_read);
					while(button == BUTTON_OFF){
						key = KEY_RESIST;
						lcd_clear();
						view_key_type();
						lcd_goto_xy(4,3);
						lcd_str((char*)in_data);
						#ifdef UART
						uart_puts((char*)in_data);
						#endif // UART
						lcd_pstr(" Ом");
						#ifdef UART
						uart_puts_pstr(" Ohm\r\n");
						#endif // UART
						_delay_ms(200);
						if(resist_read(in_data) != RES_READ_OK) break;
					}
					break;
				}
				
				if(button == BUTTON_ON){
					button = BUTTON_OFF;
					break;
				}
				if(button == BUTTON_HOLD){
					button = BUTTON_OFF;
					mode = MODE_MENU;
					break;
				}
				#ifdef UART
				if(user_rx == USER_CMD){
					cmd_parse(user_cmd);
					break;
				}
				#endif // UART
			}
		}
		while(mode == MODE_MENU){ //************************************************************************ MENU
			srand(ADC+TCNT2);
			uint16_t time = 0;
			ds_time = 0;
			uint8_t new_mode = MODE_LIST;
			view_menu(new_mode);
			while(1){
				time++;
				_delay_ms(10);
				if(time > 500){
					button = BUTTON_OFF;
					mode = MODE_READ;
					break;
				}
				if(button == BUTTON_ON){
					button = BUTTON_OFF;
					new_mode++;
					if(new_mode == MODE_TO_PAGE_2) new_mode++;
					if(new_mode >= MODE_END) new_mode = MODE_LIST;
					view_menu(new_mode);
					time = 0;
				}
				if(button == BUTTON_HOLD){
					button = BUTTON_OFF;
					mode = new_mode;
					break;
				}
				#ifdef UART
				if(user_rx == USER_CMD){
					cmd_parse(user_cmd);
					break;
				}
				#endif // UART
			}
		}
		while(mode == MODE_LIST){ //************************************************************************ LIST
			fd = open_file_in_dir(fs, dd, keys);
			if(!fd){
				#ifdef UART
				uart_puts_pstr("Can't open keys\r\n");
				#endif // UART
				mode = MODE_READ;
				break;
			}
			fat_close_file(fd);
			ds_time = 0;
			uint16_t time = 0;
			file_seek = 0;
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
					if(file_read(keys,0)){mode = MODE_READ;break;}
					time = 0;
				}
				if(button == BUTTON_HOLD){
					file_read(keys,1);
					_delay_ms(700);
					time = 0;
				}
				#ifdef UART
				if(user_rx == USER_CMD){
					cmd_parse(user_cmd);
					break;
				}
				#endif // UART
			}
		}
		
		while(mode == MODE_LOG){ //************************************************************************* LOG
			fd = open_file_in_dir(fs, dd, logs);
			if(!fd){
				#ifdef UART
				uart_puts_pstr("Can't open log\r\n");
				#endif // UART
				mode = MODE_READ;
				break;
			}
			fat_close_file(fd);
			ds_time = 0;
			uint16_t time = 0;
			file_seek = 0;
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
					if(file_read(logs,0)){mode = MODE_READ;break;}
					time = 0;
				}
				if(button == BUTTON_HOLD){
					file_read(logs,0);
					_delay_ms(100);
					time = 0;
				}
				#ifdef UART
				if(user_rx == USER_CMD){
					cmd_parse(user_cmd);
					break;
				}
				#endif // UART
			}
		}
		while(mode == MODE_CLEAR){ //*********************************************************************** CLEAR
			mode = MODE_READ;
			fd = open_file_in_dir(fs, dd, logs);
			if(!fd){
				#ifdef UART
				uart_puts_pstr("Can't open log\r\n");
				#endif // UART
				break;
			}
			fat_resize_file(fd,0);
			fat_close_file(fd);
			file_seek = 0;
			lcd_clear();
			lcd_goto_xy(3,3);
			lcd_pstr("Лог очищен!");
		}
		while(mode == MODE_RAND_DALLAS){ //***************************************************************** RAND_DALLAS
			out_data[0] = 0x01;
			for(uint8_t i=1;i<5;i++) out_data[i] = rand();
			uint8_t temp = 0;
			out_data[5] = temp;
			out_data[6] = temp;
			for(uint8_t i=0;i<7;i++) temp = ds_crc(temp, out_data[i]);
			out_data[7] = temp;
			key = KEY_DALLAS;
			mode = MODE_WRITE;
			mode_loop = MODE_RAND_DALLAS;
		}
		while(mode == MODE_RAND_PROXY){ //****************************************************************** RAND_PROXY
			for(uint8_t i=1;i<6;i++) out_data[i] = 0;
			for(uint8_t i=1;i<6;i++) out_data[i] = rand();
			key = KEY_RFID;
			mode = MODE_WRITE;
			mode_loop = MODE_RAND_PROXY;
		}
		while(mode == MODE_EEPROM_24C16 || mode == MODE_EEPROM_24C64){ //*********************************** EEPROM_READ
			uint8_t error = 0;
			uint16_t offset = 0;
			VCC_ON();
			lcd_clear();
			if(mode == MODE_EEPROM_24C64) error = i2c_set_address_c64(0,1);
			else error = i2c_set_address_c16(0,1);
			if(error){
				lcd_goto_xy(1,3);
				lcd_pstr("Ошибка чтения!");
				sound_play(sound_error);
				mode = MODE_READ;
				VCC_OFF();
				break;
			}
			lcd_goto_xy(1,1);
			while(button != BUTTON_HOLD){
				for(uint8_t i=0;i<32;i++){
					uint8_t byte = 0;
					i2c_receive_ack(&byte);
					lcd_hex_mini(byte);
					if((i&7) == 7) lcd_chr(' ');
					else lcd_sep_mini();
				}
				lcd_goto_xy(1,6);
				lcd_pstr("Offset ");
				lcd_hex(offset>>8);
				lcd_hex(offset);
				offset+=32;
				lcd_goto_xy(1,1);
				while(button == BUTTON_OFF);
				if(button == BUTTON_ON) button = BUTTON_OFF;
			}
			VCC_OFF();
			button = BUTTON_OFF;
			mode = MODE_READ;
		}
		while(mode == MODE_24C16_TO_FILE || mode == MODE_24C64_TO_FILE){ //********************************* EEPROM_TO_FILE
			uint8_t error = 0;
			uint8_t no_file = 0;
			uint8_t last_mode = mode;
			mode = MODE_READ;
			VCC_ON();
			lcd_clear();
			for(uint8_t i=0;i<100;i++){
				eeprom[7] = i/10 + '0';
				eeprom[8] = i%10 + '0';
				fd = open_file_in_dir(fs, dd, eeprom);
				if(!fd){
					no_file = 1;
					if(!fat_create_file(dd, eeprom, &directory))
					{
						#ifdef UART
						uart_puts_pstr("error opening file: eeprom___.bin\r\n");
						#endif // UART
						lcd_goto_xy(1,3);
						lcd_pstr("Ошибка записи!");
						sound_play(sound_error);
						VCC_OFF();
						_delay_ms(1000);
						break;
					}
				}else{
					no_file = 0;
				}
				fat_close_file(fd);
				if(no_file) break;
			}
			fd = open_file_in_dir(fs, dd, eeprom);
			if(!fd){
				#ifdef UART
				uart_puts_pstr("Can't write file\r\n");
				#endif // UART
				lcd_goto_xy(1,3);
				lcd_pstr("Ошибка записи!");
				sound_play(sound_error);
				VCC_OFF();
				_delay_ms(1000);
				break;
			}else{
				if(last_mode == MODE_24C64_TO_FILE) error = i2c_set_address_c64(0,1);
				else error = i2c_set_address_c16(0,1);
				if(error){
					lcd_goto_xy(1,3);
					lcd_pstr("Ошибка чтения!");
					sound_play(sound_error);
					VCC_OFF();
					_delay_ms(1000);
					break;
				}
				uint16_t blocks;
				if(last_mode == MODE_24C64_TO_FILE) blocks = 8192/FILE_BUF_SIZE;
				else blocks = 2048/FILE_BUF_SIZE;
				for(uint16_t block=0;block<blocks;block++){
					for(uint8_t i=0;i<FILE_BUF_SIZE;i++){
						i2c_receive_ack((uint8_t*)file_buf+i);
					}
					if(fat_write_file(fd, (uint8_t*) file_buf, FILE_BUF_SIZE) != FILE_BUF_SIZE)
					{
						fat_close_file(fd);
						lcd_goto_xy(1,3);
						lcd_pstr("Ошибка записи!");
						sound_play(sound_error);
						VCC_OFF();
						_delay_ms(1000);
					}
				}
			}
			VCC_OFF();
			fat_close_file(fd);
			lcd_clear();
			lcd_goto_xy(1,3);
			lcd_pstr("ПЗУ в файле: ");
			lcd_goto_xy(1,4);
			lcd_str(eeprom);
			eeprom[7] = '_';
			eeprom[8] = '_';
			while(button == BUTTON_OFF);
			if(button == BUTTON_ON) button = BUTTON_OFF;
		}
		if(mode != MODE_READ && mode != MODE_WRITE) mode = MODE_READ;
	}
}

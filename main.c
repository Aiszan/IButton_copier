#include <avr/io.h>
#include <stdint.h>
#include <util/delay.h>
#include <avr/pgmspace.h>

#include "uart.h"
#include "sound.h"
#include "lcd.h"
#include "dallas.h"
#include "rfid.h"
#include "kt-01.h"
#include "metakom.h"
#include "cyfral.h"

#define KEY_NO_KEY	0
#define KEY_DALLAS	1
#define KEY_RFID	2
#define KEY_KT01	3
#define KEY_METAKOM	4
#define KEY_CYFRAL	5
#define KEY_RESIST	6

#define BUTTON_PORT PORTB
#define BUTTON_PIN  PINB
#define BUTTON_DDR  DDRB
#define BUTTON_LINE 0

#define BUTTON_OFF  0
#define BUTTON_ON   1
#define BUTTON_HOLD 2

uint8_t in_data[16];
uint8_t out_data[16];

void (*reset)() = 0;

const uint8_t PROGMEM sound_read[] = {C2+T1,D2+T1,E2+T1,F2+T1,G2+T1,MUTE};
const uint8_t PROGMEM sound_write[] = {G2+T1,F2+T1,E2+T1,D2+T1,C2+T1,MUTE};
const uint8_t PROGMEM sound_error[] = {C1+T2,T1,C1+T2,MUTE};
const uint8_t PROGMEM sound_exist[] = {C3+T2,MUTE};
const uint8_t PROGMEM sound_button[] = {F3+T1,MUTE};
const uint8_t PROGMEM sound_button2[] = {F3+T2,MUTE};

void button_init()
{
  BUTTON_DDR &= ~(1 << BUTTON_LINE);
  BUTTON_PORT |= 1 << BUTTON_LINE;
}

uint8_t button()
{
  static uint8_t button_state = 0;
  if(BUTTON_PIN & (1<<BUTTON_LINE)){
    button_state = 0;
    return BUTTON_OFF;
  }
  if((BUTTON_PIN & (1<<BUTTON_LINE)) == 0 && button_state == 1){
    button_state++;
    sound_play(sound_button);
    return BUTTON_ON;
  }
  if(button_state > 20){
    button_state = 1;
    sound_play(sound_button2);
    return BUTTON_HOLD;
  }
  _delay_ms(100);
  button_state++;
  return BUTTON_OFF;
}

uint8_t try_program_dallas()
{
  uint8_t temp = 0;
  for(uint8_t i=0; i<200;i++){
    temp = ds_read_rom(in_data);
    _delay_ms(5);
    if(temp == DS_READ_ROM_OK) break;
  }
  if(temp == DS_READ_ROM_NO_PRES){
    lcd_goto_xy(1,6);
    lcd_str_p("Ошибка записи");
    uart_puts_p("Write error\r\n");
    sound_play(sound_error);
    _delay_ms(1000);
    return 1;
  }
  for(uint8_t i=0;i<8;i++){
    if(in_data[i] != out_data[i]) break;
    if(i == 7){
      lcd_goto_xy(1,6);
      lcd_chr(KEY);
      lcd_str_p(" уже записан");
      uart_puts_p("Key is already recorded\r\n");
      sound_play(sound_exist);
      _delay_ms(1000);
      return 2;
    }
  }
  for(uint8_t i=0; i<3;i++){
    temp = ds_program_tm2004(out_data);
    if(temp == DS_READ_ROM_NO_PRES) i = 0;
    if(temp == DS_READ_ROM_OK) break;
  }
  if(temp == DS_READ_ROM_OK){
    lcd_goto_xy(1,6);
    lcd_str_p("tm2004 записан");
    uart_puts_p("tm2004 is recorded\r\n");
    sound_play(sound_write);
    _delay_ms(1000);
    return 0;
  }
  for(uint8_t i=0; i<3;i++){
    temp = ds_program_tm08v2(out_data);
    if(temp == DS_READ_ROM_NO_PRES) i = 0;
    if(temp == DS_READ_ROM_OK) break;
  }
  if(temp == DS_READ_ROM_OK){
    lcd_goto_xy(1,6);
    lcd_str_p("tm08v2 записан");
    lcd_str_p("tm08v2 is recorded\r\n");
    sound_play(sound_write);
    _delay_ms(1000);
    return 0;
  }

  lcd_goto_xy(1,6);
  lcd_str_p("Ошибка записи");
  uart_puts_p("Write error\r\n");
  sound_play(sound_error);
  _delay_ms(1000);
  return 1;
}

void view_dallas_code(uint8_t* data)
{
  lcd_goto_xy(1,3);
  lcd_hex(data[7]);
  uart_putc_hex(data[7]);
  lcd_str_p(":CRC  FAM:");
  uart_puts_p(":CRC  FAM:");
  lcd_hex(data[0]);
  uart_putc_hex(data[0]);
  uart_putc('\r');
  uart_putc('\n');
  for(uint8_t i=0;i<6;i++){
    lcd_hex(data[6-i]);
    uart_putc_hex(data[6-i]);
    if(i<5){lcd_sep();uart_putc(':');}
  }
  uart_putc('\r');
  uart_putc('\n');
}

uint8_t view_dallas()
{
  uint8_t status,temp;
  status = ds_read_rom(in_data);
  if(status == DS_READ_ROM_NO_PRES) return status;

  for(uint8_t i=0;i<100;i++){
    temp = ds_timeslot();
    if(temp > 10) break;
  }
  
  if(status == DS_READ_ROM_CRC_ERR){
    for(uint8_t i=0;i<5;i++){
      status = ds_read_rom(in_data);
      if(status == DS_READ_ROM_NO_PRES) return status;
      if(status == DS_READ_ROM_OK) break;
    }
  }
  
  if(status == DS_READ_ROM_OK || status == DS_READ_ROM_CRC_ERR){
    lcd_clear();
	uart_putc('\r');
    uart_putc('\n');
    lcd_goto_xy(2,1);
    lcd_str_p("Тип: Даллас");
    uart_puts_p("Type: Dallas\r\n");
    if(status == DS_READ_ROM_CRC_ERR)lcd_chr(MARK);
    view_dallas_code(in_data);
    lcd_goto_xy(1,6);
    lcd_str_p("таймслот ");
    uart_puts_p("timeslot ");
    lcd_chr(temp % 100 / 10 + 0x30);
    uart_putc(temp % 100 / 10 +'0');
    lcd_chr(temp % 10 + 0x30);
    uart_putc(temp % 10 +'0');
    lcd_chr(MU);
    lcd_chr('s');
    uart_puts_p("us\r\n");
    sound_play(sound_read);
    for(uint8_t i=0;i<8;i++) out_data[i] = in_data[i];
  }
  return status;
}

uint8_t program_dallas()
{
  lcd_goto_xy(1,6);
  lcd_str_p(" Запись ключа ");
  uart_puts_p("Writing...\r\n");
  _delay_ms(1000);
  while(ds_reset() != DS_READ_ROM_OK) if(button() == BUTTON_HOLD) return 3;
  return try_program_dallas();
}

uint8_t view_rfid()
{
  uint8_t status;
  status = rfid_read(in_data);
  if (status == RFID_OK){
    lcd_clear();
    lcd_goto_xy(2,1);
    lcd_str_p("Тип: Прокси");
    uart_puts_p("Type: RFID\r\n");
    lcd_goto_xy(1,3);
    for(uint8_t i=0;i<5;i++){
      lcd_hex(in_data[i]);
      uart_putc_hex(in_data[i]);
      if(i<4){lcd_chr(':');uart_putc(':');}
    }
	uart_putc('\r');
    uart_putc('\n');
    sound_play(sound_read);
    for(uint8_t i=0;i<5;i++) out_data[i] = in_data[i];
  }
  return status;
}

uint8_t program_rfid()
{
	uint8_t temp;
	lcd_goto_xy(1,6);
	lcd_str_p(" Запись ключа ");
	uart_puts_p("Writing...\r\n");
  
	while(rfid_read(in_data) == RFID_NO_KEY) if(button() == BUTTON_HOLD) return 3;
	
	temp = rfid_force_read(in_data);
	for(uint8_t i=0;i<5;i++) if(in_data[i] != out_data[i]) temp = RFID_PARITY_ERR;
	
	if(temp == RFID_OK){
		lcd_goto_xy(1,6);
		lcd_chr(KEY);
		lcd_str_p(" уже записан");
		uart_puts_p("Key is already recorded\r\n");
		sound_play(sound_exist);
		_delay_ms(1000);
		return 2;
	}
	
	for(uint8_t i=0;i<5;i++){
		if(rfid_program(out_data) == RFID_OK){
			lcd_goto_xy(1,6);
			lcd_str_p("T5557 записан ");
			uart_puts_p("T5557 recorded\r\n");
			sound_play(sound_write);
			_delay_ms(1000);
			return 0;
		}
	}

	lcd_goto_xy(1,6);
	lcd_str_p("Ошибка записи");
	uart_puts_p("Write error\r\n");
	sound_play(sound_error);
	_delay_ms(1000);
	return 1;
}

uint8_t view_kt01()
{
  uint8_t status = 0;
  status = kt_read_rom(in_data);
  if(status == KT_CRC_ERR){
    for(uint8_t i=0;i<5;i++){
      status = kt_read_rom(in_data);
      if(status == KT_NO_KEY) return status;
      if(status == KT_READ_ROM_OK) break;
    }
  }
  if(status == KT_READ_ROM_OK || status == KT_CRC_ERR){
    lcd_clear();
    lcd_goto_xy(2,1);
    lcd_str_p("Тип: КТ-01");
    uart_puts_p("Type: KT-01\r\n");
    if(status == KT_CRC_ERR) lcd_chr(MARK);
    lcd_goto_xy(2,3);
    for(uint8_t i=0;i<4;i++){
      lcd_hex(in_data[7-i]);
      uart_putc_hex(in_data[7-i]);
      if(i<3){lcd_chr(':');uart_putc(':');}
    }
	uart_putc('\r');
    uart_putc('\n');
    lcd_goto_xy(2,4);
    for(uint8_t i=0;i<4;i++){
      lcd_hex(in_data[3-i]);
      uart_putc_hex(in_data[3-i]);
      if(i<3){lcd_chr(':');uart_putc(':');}
    }
	uart_putc('\r');
    uart_putc('\n');
    sound_play(sound_read);
    for(uint8_t i=0; i<16;i++) out_data[i] = in_data[i];
  }
  return status;
}

uint8_t program_kt01()
{
  uint8_t status = 0;
  lcd_goto_xy(1,6);
  lcd_str_p("Подключите щуп");
  uart_puts_p("Connect probe\r\n");
  _delay_ms(2000);
  lcd_goto_xy(1,6);
  lcd_str_p(" Запись ключа ");
  uart_puts_p("Writing..\r\n");

  while(kt_read_rom(in_data) != 0) if(button() == BUTTON_HOLD) return 3;
  
  for(uint8_t i=0;i<16;i++){
    if(in_data[i] != out_data[i]) break;
    if(i == 7){
      lcd_goto_xy(1,6);
      lcd_chr(KEY);
      lcd_str_p(" уже записан");
      uart_puts_p("Key is already recorded\r\n");
      sound_play(sound_exist);
      _delay_ms(1000);
      return 2;
    }
  }
  
  for(uint8_t i=0; i<5;i++){
    status = kt_write_rom(out_data);
    if(status == KT_NO_KEY) i = 0;
    if(status == KT_READ_ROM_OK) break;
  }
  lcd_goto_xy(1,6);
  if(status == KT_READ_ROM_OK){
    lcd_str_p("KT-01 записан");
    uart_puts_p("KT-01 recorded\r\n");
    sound_play(sound_write);
    _delay_ms(1000);
    }else{
    lcd_str_p("Ошибка записи");
    uart_puts_p("Write error\r\n");
    sound_play(sound_error);
    _delay_ms(1000);
  }
  return status;
}

uint8_t view_mk()
{
  uint8_t status = 0, temp = 0;
  status = mk_read(in_data);
  if(status == MK_READ_OK){
    lcd_clear();
    lcd_goto_xy(2,1);
    lcd_str_p("Тип: Metakom");
    uart_puts_p("Type: Metakom\r\n");
    lcd_goto_xy(3,3);
    for(uint8_t i=0;i<4;i++){
      lcd_hex(in_data[i]);
      uart_putc_hex(in_data[i]);
      if(i<3){lcd_chr(':');uart_putc(':');}
    }
	uart_putc('\r');
    uart_putc('\n');
    out_data[0] = 0x01;
    for(uint8_t i=0;i<4;i++) out_data[i+1] = in_data[i];
    out_data[5] = 0;
    out_data[6] = 0;
    for(uint8_t i=0;i<7;i++) temp = ds_crc(temp,out_data[i]);
    out_data[7] = temp;

    sound_play(sound_read);
  }
  return status;
}

uint8_t program_mk()
{
  lcd_goto_xy(1,6);
  lcd_str_p(" Запись ключа ");
  uart_puts_p("Writing...\r\n");
  view_dallas_code(out_data);
  
  while(ds_reset() != DS_READ_ROM_OK){
    uint8_t temp = button();
    if(temp == BUTTON_ON){
      for(uint8_t i=1;i<5;i++) out_data[i+8] = out_data[i];
      for(uint8_t i=1;i<5;i++) out_data[i] = out_data[13-i];
      temp = 0;
      for(uint8_t i=0;i<7;i++) temp = ds_crc(temp,out_data[i]);
      out_data[7] = temp;
      view_dallas_code(out_data);
    } else if(temp == BUTTON_HOLD) return 3;
  }
  return try_program_dallas();
}

uint8_t view_cyfral()
{
  uint8_t status = 0, temp = 0;
  status = cl_read(in_data);
  if(status == CL_READ_OK){
    lcd_clear();
    lcd_goto_xy(2,1);
    lcd_str_p("Тип: Cyfral");
    uart_puts_p("Type: Cyfral\r\n");
    lcd_goto_xy(5,3);
    lcd_hex(in_data[0]);
    uart_putc_hex(in_data[0]);
    lcd_chr(':');
    uart_putc(':');
    lcd_hex(in_data[1]);
    uart_putc_hex(in_data[1]);
	uart_putc('\r');
    uart_putc('\n');
    sound_play(sound_read);
    
    out_data[0] = 0x01;
    out_data[1] = in_data[1];
    out_data[2] = in_data[0];
    out_data[3] = 0x80;
    for(uint8_t i=4;i<7;i++) out_data[i] = 0;
    for(uint8_t i=0;i<7;i++) temp = ds_crc(temp,out_data[i]);
    out_data[7] = temp;
  }
  return status;
}

uint8_t program_cyfral()
{
  lcd_goto_xy(1,6);
  lcd_str_p(" Запись ключа ");
  uart_puts_p("Wrirting...\r\n");
  view_dallas_code(out_data);
  while(ds_reset() != DS_READ_ROM_OK) if(button() == BUTTON_HOLD) return 3;
  return try_program_dallas();
}

uint8_t view_resistor()
{
  uint8_t status = 0, temp = 0;
  static uint8_t repeat = 0;
  status = ADCH;
  if(status < 0xFF){
    if(repeat < 5){
      repeat++;
      return 1;
    }
    uint32_t u = ADC>>6;
    uint32_t r = u * 1000 / (1024 - u);
    
    lcd_clear();
    lcd_goto_xy(2,1);
    lcd_str_p("Тип: Резистор");
    uart_puts_p("Type: Resistor\r\n");
    lcd_goto_xy(3,2);
    if(r > 30000) {lcd_chr('~');uart_putc('~');}
    status = 0;
    for(uint8_t i=0;i<5;i++){
      temp = r % 100000 / 10000;
      if(status == 0 && temp == 0 && i<4){
        lcd_chr(' ');
        uart_putc(' ');
        }else{
        status = 1;
        lcd_chr(temp + '0');
        uart_putc(temp + '0');
      }
      r = r % 100000 * 10;
    }
    lcd_str_p(" Ом");
    uart_puts_p(" Оhm\r\n");
    repeat = 0;
    sound_play(sound_read);
    while(1) if(button() != BUTTON_OFF) return 0;
    }else{
    repeat = 0;
  }
  return 1;
}

uint8_t test_bat()
{
  ADCSRA = (1 << ADEN) 								// разрешение АЦП
  |(1 << ADSC) 										// запуск преобразования
  |(1 << ADATE) 										// непрерывный режим работы АЦП
  |(0 << ADPS2)|(1 << ADPS1)|(1 << ADPS0) 			// предделитель на 8 (частота АЦП 148kHz)
  |(0 << ADIE); 										// запрет прерывания
  ADCSRB = (0 << ADTS2)|(0 << ADTS1)|(0 << ADTS0); 	// непрерывный режим работы АЦП

  ADMUX = (1 << REFS1)|(1 << REFS0) 					// опорное напряжение 1,1v
  |(1 << ADLAR)										// смещение результата (влево при 1, читаем 8 бит из ADCH)
  |(6); 											    // вход ADC6
  _delay_ms(1);
  return ADCH;
}

/**************************Главная функция******************************/
int main (void)
{
  uint16_t temp;
  button_init();
  uart_init();
  sound_init();
  ds_init();
  rfid_init();
  kt_init();
  mk_init();
  lcd_init();
  lcd_contrast(0x44);
  lcd_image();
  _delay_ms(1000);

  for(uint8_t detect=1;;){
    if(detect){
      lcd_clear();
      lcd_goto_xy(11,1);
      temp = test_bat()*162;
      if(temp == 0){
        lcd_str_p("USB");
        }else{
        lcd_chr(temp % 10000 / 1000 + '0');
        lcd_chr(',');
        lcd_chr(temp % 1000 / 100 + '0');
        lcd_chr('в');
      }
      mk_init();
      lcd_goto_xy(4,3);
      lcd_str_p("Жду ключ ");
      uart_puts_p("Wait for a key\r\n");
      lcd_chr(KEY);
      _delay_ms(1000);
    }
    detect = 1;
    
    temp = button();
    if(temp == BUTTON_ON) continue;
    if(temp == BUTTON_HOLD) reset();
    
    if(view_dallas() != DS_READ_ROM_NO_PRES){
      while(ds_reset() == 0);
      while(program_dallas() != 3);
      continue;
    }

    if(view_rfid() == RFID_OK){
      _delay_ms(1000);
      while(program_rfid() != 3);
      continue;
    }
    
    if(view_kt01() != KT_NO_KEY){
      while(kt_reset() == 0);
      while(program_kt01() != 3);
      continue;
    }
    
    if(view_mk() == MK_READ_OK){
      while(mk_read(in_data) == MK_READ_OK);
      while(program_mk() != 3);
      continue;
    }

    if(view_cyfral() == CL_READ_OK){
      while(cl_read(in_data) == CL_READ_OK);
      while(program_cyfral() != 3);
      continue;
    }

    if(view_resistor() == 0){
      continue;
    }
    detect = 0;
  }
}

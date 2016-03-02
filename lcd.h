/*
** lcd.h
**
** LCD 3310 driver
** Target: ATMEGA :: AVR-GCC
**
** Written by Tony Myatt - 2007
** Quantum Torque - www.quantumtorque.com
*/
#ifndef _NOKIALCD_H_
#define _NOKIALCD_H_

/* Lcd screen size */
#define LCD_X_RES 84
#define LCD_Y_RES 48
#define LCD_CACHE_SIZE ((LCD_X_RES * LCD_Y_RES) / 8)

/* Pinout for LCD */
#define LCD_CLK_PIN 	(1<<PD7)
#define LCD_DATA_PIN 	(1<<PD5)
#define LCD_DC_PIN 		(1<<PD4)
#define LCD_CE_PIN 		(1<<PD3)
#define LCD_RST_PIN 	(1<<PD2)
#define LCD_PORT		PORTD
#define LCD_DDR			DDRD

/* Special Chars */
#define STOP 		ICON(0)
#define ARROW_RIGHT	ICON(1)
#define ARROW_LEFT 	ICON(2)
#define ARROW_UP 	ICON(3)
#define ARROW_DOWN 	ICON(4)
#define BAT_FULL 	ICON(5)
#define BAT_3	 	ICON(6)
#define BAT_2	 	ICON(7)
#define BAT_1	 	ICON(8)
#define BAT_LOW	 	ICON(9)
#define WAIT_1	 	ICON(10)
#define WAIT_2	 	ICON(11)
#define WAIT_3	 	ICON(12)
#define WAIT_4	 	ICON(13)
#define KEY		 	ICON(14)
#define RF		 	ICON(15)
#define MARK	 	ICON(16)
#define MU		 	ICON(17)
#define GRAY	 	ICON(18)
#define DEGREE		ICON(19)

/* Function for my special characters */
#define	ICON(x)		'~'+2+x

void lcd_init(void);
void lcd_contrast(unsigned char contrast);
void lcd_clear(void);
void lcd_clear_area(unsigned char line, unsigned char startX, unsigned char endX);
void lcd_clear_line(unsigned char line);
void lcd_goto_xy(unsigned char x, unsigned char y);
void lcd_goto_xy_exact(unsigned char x, unsigned char y);
void lcd_chr(char chr);
void lcd_str(char* str);
void lcd_str_P(const char* progmem_s);
#define lcd_str_p(__s)         lcd_str_P(PSTR(__s))
void lcd_hex(char hex);
void lcd_sep(void);

void lcd_chr_mini(char chr);
void lcd_str_mini(char* str);
void lcd_hex_mini(char hex);
void lcd_sep_mini(void);

void lcd_image(void);

#endif




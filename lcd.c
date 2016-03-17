/*
** lcd.c
**
** LCD 3310 driver
** Unbuffered version - very small memory footprint
** Target: ATMEGA128 :: AVR-GCC
**
** Written by Tony Myatt - 2007
** Quantum Torque - www.quantumtorque.com
*/
#include <stdio.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include "lcd.h"
#include "lcd_graph.h"

/* delay macro function */
#define lcd_delay() for(int i=-32000;i<32000;i++)

/* Command type sent to the lcd */
typedef enum { LCD_CMD  = 0, LCD_DATA = 1 } LcdCmdData;

/* Function prototypes */
void lcd_base_addr(unsigned int addr);
void lcd_send(unsigned char data, LcdCmdData cd);

/* The lcd cursor position */
int lcdCacheIdx;

/* Performs IO & LCD controller initialization */
void lcd_init(void)
{
    // Pull-up on reset pin
    LCD_PORT |= LCD_RST_PIN;
	
	// Set output bits on lcd port
	LCD_DDR |= LCD_RST_PIN | LCD_CE_PIN | LCD_DC_PIN | LCD_DATA_PIN | LCD_CLK_PIN;
    
	// Wait after VCC high for reset (max 30ms)
    _delay_ms(15);
    
    // Toggle display reset pin
    LCD_PORT &= ~LCD_RST_PIN;
    lcd_delay();
    LCD_PORT |= LCD_RST_PIN;

    // Disable LCD controller
    LCD_PORT |= LCD_CE_PIN;

    lcd_send(0x21, LCD_CMD);  // LCD Extended Commands
    lcd_send(0xC8, LCD_CMD);  // Set LCD Vop(Contrast)
    lcd_send(0x06, LCD_CMD);  // Set Temp coefficent
    lcd_send(0x13, LCD_CMD);  // LCD bias mode 1:48
    lcd_send(0x20, LCD_CMD);  // Standard Commands, Horizontal addressing
    lcd_send(0x0C, LCD_CMD);  // LCD in normal mode
    
    // Clear lcd
    lcd_clear();
	
	// For using printf
	//fdevopen(lcd_chr, 0);
}

/* Set display contrast. Note: No change is visible at ambient temperature */
void lcd_contrast(unsigned char contrast)
{
	lcd_send(0x21, LCD_CMD);				// LCD Extended Commands
    lcd_send(0x80 | contrast, LCD_CMD);		// Set LCD Vop(Contrast)
    lcd_send(0x20, LCD_CMD);				// LCD std cmds, hori addr mode
}

/* Clears the display */
void lcd_clear(void)
{
	lcdCacheIdx = 0;
	
	lcd_base_addr(lcdCacheIdx);
	
    // Set the entire cache to zero and write 0s to lcd
    for(int i=0;i<LCD_CACHE_SIZE;i++) {
		lcd_send(0, LCD_DATA);
    }
}

/* Clears an area on a line */
void lcd_clear_area(unsigned char line, unsigned char startX, unsigned char endX)
{  
    // Start and end positions of line
    int start = (line-1)*84+(startX-1);
    int end = (line-1)*84+(endX-1);
	
	lcd_base_addr(start);
    
    // Clear all data in range from cache
    for(unsigned int i=start;i<end;i++) {
        lcd_send(0, LCD_DATA);
    }
}

/* Clears an entire text block. (rows of 8 pixels on the lcd) */
void lcd_clear_line(unsigned char line)
{
    lcd_clear_area(line, 1, LCD_X_RES);
}

/* Sets cursor location to xy location corresponding to basic font size */
void lcd_goto_xy(unsigned char x, unsigned char y)
{
    lcdCacheIdx = (x-1)*6 + (y-1)*84;
}

/* Sets cursor location to exact xy pixel location on the lcd */
void lcd_goto_xy_exact(unsigned char x, unsigned char y)
{
    lcdCacheIdx = (x-1) + (y-1)*84;
}

/* Displays a character at current cursor location */
void lcd_chr(char chr)
{
	lcd_base_addr(lcdCacheIdx);

    // 5 pixel wide characters and add space
    for(unsigned char i=0;i<5;i++) {
		lcd_send(pgm_read_byte(&font5x7[chr-32][i]) << 1, LCD_DATA);
    }
	lcd_send(0, LCD_DATA);
	
	lcdCacheIdx += 6;
}

/* Displays string at current cursor location and increment cursor location */
void lcd_str(char *str)
{
    while(*str) {
        lcd_chr(*str++);
    }
}

/* Displays string in progmem at current cursor location and increment cursor location */
void lcd_str_P(const char *progmem_s)
/* print string from program memory on lcd (no auto linefeed) */
{
    register char c;

    while ( (c = pgm_read_byte(progmem_s++)) ) {
        lcd_chr(c);
    }

}

/* Displays a separator at current cursor location */
void lcd_sep()
{
	lcd_base_addr(lcdCacheIdx);

    // 5 pixel wide characters and add space
	lcd_send(0x44, LCD_DATA);
	lcd_send(0, LCD_DATA);
	
	lcdCacheIdx += 2;
}

/* Displays a hex value at current cursor location */
void lcd_hex(char b)
{
    /* upper nibble */
    if((b >> 4) < 0x0a)
        lcd_chr((b >> 4) + '0');
    else
        lcd_chr((b >> 4) - 0x0a + 'A');

    /* lower nibble */
    if((b & 0x0f) < 0x0a)
        lcd_chr((b & 0x0f) + '0');
    else
        lcd_chr((b & 0x0f) - 0x0a + 'A');
}

/* function for view graphic on 84*48 display */
void lcd_image()
{
	char x,y;
	for(y=0;y<6;y++){
		lcd_goto_xy_exact(0,y);
		for(x=0;x<84;x++)
			lcd_send(pgm_read_byte(&lcd_image_nyan[y*84+x]), LCD_DATA);
	}
}

/* Displays a mini character at current cursor location */

void lcd_chr_mini(char chr)
{
	lcd_base_addr(lcdCacheIdx);

    // 3 pixel wide characters and add space
    for(unsigned char i=0;i<3;i++) {
		lcd_send(pgm_read_byte(&font3x5[chr-48][i]) << 1, LCD_DATA);
    }
	lcd_send(0, LCD_DATA);
	
	lcdCacheIdx += 4;
}

/* Displays mini string at current cursor location and increment cursor location */

void lcd_str_mini(char *str)
{
    while(*str) {
        lcd_chr_mini(*str++);
    }
}

/* Displays a mini hex value at current cursor location */
void lcd_hex_mini(char b)
{
	/* upper nibble */
	if((b >> 4) < 0x0a)
	lcd_chr_mini((b >> 4) + '0');
	else
	lcd_chr_mini((b >> 4) - 0x0a + 'A');

	/* lower nibble */
	if((b & 0x0f) < 0x0a)
	lcd_chr_mini((b & 0x0f) + '0');
	else
	lcd_chr_mini((b & 0x0f) - 0x0a + 'A');
}

/* Displays a mini separator at current cursor location */
void lcd_sep_mini()
{
	lcd_base_addr(lcdCacheIdx);

	// 5 pixel wide characters and add space
	lcd_send(0x28, LCD_DATA);
	lcd_send(0, LCD_DATA);
	
	lcdCacheIdx += 2;
}

// Set the base address of the lcd
void lcd_base_addr(unsigned int addr)
{
	lcd_send(0x80 |(addr % LCD_X_RES), LCD_CMD);
	lcd_send(0x40 |(addr / LCD_X_RES), LCD_CMD);
}

/* Sends data to display controller */
void lcd_send(unsigned char data, LcdCmdData cd)
{
	// Data/DC are outputs for the lcd (all low)
	LCD_DDR |= LCD_DATA_PIN | LCD_DC_PIN;
	
    // Enable display controller (active low)
    LCD_PORT &= ~LCD_CE_PIN;

    // Either command or data
    if(cd == LCD_DATA) {
        LCD_PORT |= LCD_DC_PIN;
    } else {
        LCD_PORT &= ~LCD_DC_PIN;
    }
	
	for(unsigned char i=0;i<8;i++) {
	
		// Set the DATA pin value
		if((data>>(7-i)) & 0x01) {
			LCD_PORT |= LCD_DATA_PIN;
		} else {
			LCD_PORT &= ~LCD_DATA_PIN;
		}
		
		// Toggle the clock
		LCD_PORT |= LCD_CLK_PIN;
		LCD_PORT &= ~LCD_CLK_PIN;
	}

	// Disable display controller
    LCD_PORT |= LCD_CE_PIN;
	
	// Data/DC can be used as button inputs when not sending to LCD (/w pullups)
	LCD_DDR &= ~(LCD_DATA_PIN | LCD_DC_PIN);
	LCD_PORT |= LCD_DATA_PIN | LCD_DC_PIN;
}


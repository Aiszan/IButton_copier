/*-------------------------------------------------------------------------*/
/* PFF - Low level disk control module for AVR            (C)ChaN, 2010    */
/*-------------------------------------------------------------------------*/

#include "mmc.h"

/*-------------------------------------------------------------------------*/
/* Platform dependent macros and functions needed to be modified           */
/*-------------------------------------------------------------------------*/

#include <avr/io.h>			/* Device include file */
#include <util/delay.h>

#define SPI_PORTX   PORTB
#define SPI_DDRX    DDRB

#define SPI_MISO   4
#define SPI_MOSI   3
#define SPI_SCK    5
#define SPI_SS     2

/*_______________ макросы ____________________ Author(s)...: Pashgan    http://ChipEnable.Ru    */ 

#define DESELECT()	do{SPI_PORTX |= (1<<(SPI_SS)); }while(0)				/* CS = H */
#define SELECT()		do{SPI_PORTX &= ~(1<<(SPI_SS)); }while(0)			/* CS = L */
#define MMC_SEL		(!(SPI_PORTX & (1<<(SPI_SS))))							/* CS status (true:CS == L) */
#define FORWARD(d)	         												/* Data forwarding function (Console out in this example) */

#define init_spi()  SPI_Init()    											/* Initialize SPI port (usi.S) */
#define dly_100us() _delay_us(100)											/* Delay 100 microseconds (usi.S) */
#define rcv_spi()	SPI_ReadByte_i()										/* Send a 0xFF to the MMC and get the received byte (usi.S) */
#define xmit_spi(d)  do{ SPDR = d; while(!(SPSR & (1<<SPIF))); }while(0)	/* Send a byte to the MMC (usi.S) */

/* ______________ встраиваемые функции _____________*/

inline static uint8_t SPI_ReadByte_i(void){					/*получить байт данных по SPI*/
	SPDR = 0xff;
	while(!(SPSR & (1<<SPIF)));
	return SPDR;
}

void SPI_Init(void)											/*инициализация SPI*/
{	
	SPI_DDRX |= (1<<SPI_MOSI)|(1<<SPI_SCK)|(1<<SPI_SS);		/*настройка портов ввода-вывода все выводы, кроме MISO выходы*/
	SPI_DDRX &= ~(1<<SPI_MISO);
	SPI_PORTX |= (1<<SPI_MOSI)|(1<<SPI_SCK)|(1<<SPI_SS)|(1<<SPI_MISO);
	SPCR = (0<<SPIE)|(1<<SPE)|(0<<DORD)|(1<<MSTR)|(0<<CPOL)|(0<<CPHA)|(0<<SPR1)|(0<<SPR0);/*разрешение spi,старший бит вперед,мастер, режим 0*/
	SPSR = (1<<SPI2X);
}

void SPI_WriteByte(uint8_t data)							/*отослать байт данных по SPI*/
{
	SPDR = data; 
	while(!(SPSR & (1<<SPIF)));
}

uint8_t SPI_ReadByte(void)									/*получить байт данных по SPI*/
{  
	SPDR = 0xff;
	while(!(SPSR & (1<<SPIF)));
	return SPDR; 
}

uint8_t SPI_WriteReadByte(uint8_t data)						/*отослать и получить байт данных по SPI*/
{  
	SPDR = data;
	while(!(SPSR & (1<<SPIF)));
	return SPDR; 
}

void SPI_WriteArray(uint8_t num, uint8_t *data)				/*отправить несколько байт данных по SPI*/
{
	while(num--){
		SPDR = *data++;
		while(!(SPSR & (1<<SPIF)));
	}
}

void SPI_WriteReadArray(uint8_t num, uint8_t *data)			/*отправить и получить несколько байт данных по SPI*/
{
	while(num--){
		SPDR = *data;
		while(!(SPSR & (1<<SPIF)));
		*data++ = SPDR;
	}
}

/*--------------------------------------------------------------------------

   Module Private Functions

---------------------------------------------------------------------------*/

/* Definitions for MMC/SDC command */
#define CMD0	(0x40+0)	/* GO_IDLE_STATE */
#define CMD1	(0x40+1)	/* SEND_OP_COND (MMC) */
#define	ACMD41	(0xC0+41)	/* SEND_OP_COND (SDC) */
#define CMD8	(0x40+8)	/* SEND_IF_COND */
#define CMD16	(0x40+16)	/* SET_BLOCKLEN */
#define CMD17	(0x40+17)	/* READ_SINGLE_BLOCK */
#define CMD24	(0x40+24)	/* WRITE_BLOCK */
#define CMD55	(0x40+55)	/* APP_CMD */
#define CMD58	(0x40+58)	/* READ_OCR */


/* Card type flags (CardType) */
#define CT_MMC				0x01	/* MMC ver 3 */
#define CT_SD1				0x02	/* SD ver 1 */
#define CT_SD2				0x04	/* SD ver 2 */
#define CT_BLOCK			0x08	/* Block addressing */


static uint8_t CardType;


/*-----------------------------------------------------------------------*/
/* Send a command packet to MMC                                          */
/*-----------------------------------------------------------------------*/

static
uint8_t send_cmd (
	uint8_t cmd,							/* 1st byte (Start + Index) */
	uint32_t arg							/* Argument (32 bits) */
)
{
	uint8_t n, res;

	if (cmd & 0x80) {						/* ACMD<n> is the command sequense of CMD55-CMD<n> */
		cmd &= 0x7F;
		res = send_cmd(CMD55, 0);
		if (res > 1) return res;
	}

											/* Select the card */
	DESELECT();
	rcv_spi();
	SELECT();
	rcv_spi();

											/* Send a command packet */
	xmit_spi(cmd);							/* Start + Command index */
	xmit_spi((uint8_t)(arg >> 24));			/* Argument[31..24] */
	xmit_spi((uint8_t)(arg >> 16));			/* Argument[23..16] */
	xmit_spi((uint8_t)(arg >> 8));			/* Argument[15..8] */
	xmit_spi((uint8_t)arg);					/* Argument[7..0] */
	n = 0x01;								/* Dummy CRC + Stop */
	if (cmd == CMD0) n = 0x95;				/* Valid CRC for CMD0(0) */
	if (cmd == CMD8) n = 0x87;				/* Valid CRC for CMD8(0x1AA) */
	xmit_spi(n);

											/* Receive a command response */
	n = 10;									/* Wait for a valid response in timeout of 10 attempts */
	do {
		res = rcv_spi();
	} while ((res & 0x80) && --n);

	return res;								/* Return with the response value */
}




/*--------------------------------------------------------------------------

   Public Functions

---------------------------------------------------------------------------*/

/*-----------------------------------------------------------------------*/
/* Initialize Disk Drive                                                 */
/*-----------------------------------------------------------------------*/

DSTATUS disk_initialize (void)
{
	uint8_t n, cmd, ty, ocr[4];
	uint16_t tmr;

#if _USE_WRITE
	if (CardType && MMC_SEL) disk_writep(0, 0);												/* Finalize write process if it is in progress */
#endif
	init_spi();																				/* Initialize ports to control MMC */
	DESELECT();
	for (n = 10; n; n--){
		rcv_spi();																			/* 80 dummy clocks with CS=H */
	}

	ty = 0;
	if (send_cmd(CMD0, 0) == 1) {															/* Enter Idle state */
		if (send_cmd(CMD8, 0x1AA) == 1) {													/* SDv2 */
			for (n = 0; n < 4; n++) ocr[n] = rcv_spi();										/* Get trailing return value of R7 resp */
			if (ocr[2] == 0x01 && ocr[3] == 0xAA) {											/* The card can work at vdd range of 2.7-3.6V */
				for (tmr = 10000; tmr && send_cmd(ACMD41, 1UL << 30); tmr--) dly_100us();	/* Wait for leaving idle state (ACMD41 with HCS bit) */
				if (tmr && send_cmd(CMD58, 0) == 0) {										/* Check CCS bit in the OCR */
					for (n = 0; n < 4; n++) ocr[n] = rcv_spi();
					ty = (ocr[0] & 0x40) ? CT_SD2 | CT_BLOCK : CT_SD2;						/* SDv2 (HC or SC) */
				}
			}
		} else {																			/* SDv1 or MMCv3 */
			if (send_cmd(ACMD41, 0) <= 1) 	{
				ty = CT_SD1; cmd = ACMD41;													/* SDv1 */
			} else {
				ty = CT_MMC; cmd = CMD1;													/* MMCv3 */
			}
			for (tmr = 10000; tmr && send_cmd(cmd, 0); tmr--) dly_100us();					/* Wait for leaving idle state */
			if (!tmr || send_cmd(CMD16, 512) != 0)											/* Set R/W block length to 512 */
				ty = 0;
		}
	}
	CardType = ty;
	DESELECT();
	rcv_spi();

	return ty ? 0 : STA_NOINIT;
}

/*-----------------------------------------------------------------------*/
/* Read partial sector                                                   */
/*-----------------------------------------------------------------------*/

DRESULT disk_readp (
	uint8_t *buff,							/* Pointer to the read buffer (NULL:Read bytes are forwarded to the stream) */
	uint32_t lba,							/* Sector number (LBA) */
	uint16_t ofs,							/* Byte offset to read from (0..511) */
	uint16_t cnt							/* Number of bytes to read (ofs + cnt mus be <= 512) */
)
{
	DRESULT res;
	uint8_t rc;
	uint16_t bc;


	if (!(CardType & CT_BLOCK)) lba *= 512;	/* Convert to byte address if needed */

	res = RES_ERROR;
	if (send_cmd(CMD17, lba) == 0) {		/* READ_SINGLE_BLOCK */

		bc = 40000;
		do {								/* Wait for data packet */
			rc = rcv_spi();
		} while (rc == 0xFF && --bc);

		if (rc == 0xFE) {					/* A data packet arrived */
			bc = 514 - ofs - cnt;

											/* Skip leading bytes */
			if (ofs) {
				do rcv_spi(); while (--ofs);
			}

											/* Receive a part of the sector */
			if (buff) {						/* Store data to the memory */
				do {
					*buff++ = rcv_spi();
				} while (--cnt);
			} else {						/* Forward data to the outgoing stream (depends on the project) */
				do {
					FORWARD(rcv_spi());
				} while (--cnt);
			}

											/* Skip trailing bytes and CRC */
			do rcv_spi(); while (--bc);

			res = RES_OK;
		}
	}

	DESELECT();
	rcv_spi();

	return res;
}



/*-----------------------------------------------------------------------*/
/* Write partial sector                                                  */
/*-----------------------------------------------------------------------*/

#if _USE_WRITE
DRESULT disk_writep (
	const uint8_t *buff,							/* Pointer to the bytes to be written (NULL:Initiate/Finalize sector write) */
	uint32_t sa										/* Number of bytes to send, Sector number (LBA) or zero */
)
{
	DRESULT res;
	uint16_t bc;
	static uint16_t wc;

	res = RES_ERROR;

	if (buff) {										/* Send data bytes */
		bc = (uint16_t)sa;
		while (bc && wc) {							/* Send data bytes to the card */
			xmit_spi(*buff++);
			wc--; bc--;
		}
		res = RES_OK;
	} else {
		if (sa) {									/* Initiate sector write process */
			if (!(CardType & CT_BLOCK)) sa *= 512;	/* Convert to byte address if needed */
			if (send_cmd(CMD24, sa) == 0) {			/* WRITE_SINGLE_BLOCK */
				xmit_spi(0xFF); xmit_spi(0xFE);		/* Data block header */
				wc = 512;							/* Set byte counter */
				res = RES_OK;
			}
		} else {									/* Finalize sector write process */
			bc = wc + 2;
			while (bc--) xmit_spi(0);				/* Fill left bytes and CRC with zeros */
			if ((rcv_spi() & 0x1F) == 0x05) {		/* Receive data resp and wait for end of write process in timeout of 500ms */
				for (bc = 5000; rcv_spi() != 0xFF && bc; bc--) dly_100us();	/* Wait ready */
				if (bc) res = RES_OK;
			}
			DESELECT();
			rcv_spi();
		}
	}

	return res;
}
#endif

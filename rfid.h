/*
 * rfid.h
 *
 * Created: 23.01.2016 18:59:30
 *  Author: Elektron
 */
#pragma once

#define RFID_PORT PORTC
#define RFID_DDR  DDRC
#define RFID_PIN  PINC
#define RFID_IN   2
#define RFID_OUT  3

#define RFID_OK			0
#define RFID_NO_KEY		1
#define RFID_PARITY_ERR 2


#define RFID_BUFFER_SIZE 40				//40*8=500 samples

void rfid_init(void);
uint8_t rfid_read(uint8_t* data);
uint8_t rfid_force_read(uint8_t* data);
uint8_t rfid_program(uint8_t* data);
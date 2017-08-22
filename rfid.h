/*
 * rfid.h
 *
 * Created: 23.01.2016 18:59:30
 *  Author: Elektron
 */
#pragma once

#define RFID_PORT PORTD
#define RFID_DDR  DDRD
#define RFID_PIN  PIND
#define RFID_IN   2
#define RFID_OUT  6

enum enum_rfid{RFID_OK, RFID_NO_KEY, RFID_PARITY_ERR, RFID_MISMATCH};

#define RFID_BUFFER_SIZE 25				//должен быть 9-31 байт

void rfid_init(void);
uint8_t rfid_read(uint8_t* data);
uint8_t rfid_force_read(uint8_t* data);
uint8_t rfid_check(uint8_t* data);
uint8_t rfid_em4305_write(uint8_t* data);
uint8_t rfid_t5557_write(uint8_t* data);
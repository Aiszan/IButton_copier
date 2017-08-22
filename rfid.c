#include <avr/io.h>
#include <stdint.h>
#include <util/delay.h>
#include "rfid.h"

#define FieldOn()	DDRD |= 1<<RFID_OUT;
#define FieldOff()	DDRD &= ~(1<<RFID_OUT);

uint8_t rfid_buffer [RFID_BUFFER_SIZE];  //буфер приема-передачи
uint8_t avg_u;

void rfid_init()
{
	TCCR0A = (1<<COM0A0)|(1<<WGM01);
	TCCR0B = (0<<CS02)|(0<<CS01)|(1<<CS00);
	OCR0A = 63;
	DDRD |= 1<<RFID_OUT;

	RFID_DDR |= 1<<RFID_OUT;
	RFID_DDR &= ~(1<<RFID_IN);
	RFID_PORT |= (1<<RFID_OUT);
}

void rfid_encode(uint8_t* data)
{
	rfid_buffer[0] = 0xFF;
	rfid_buffer[1] = 0x80;
	for(uint8_t i=2;i<8;i++)rfid_buffer[i]=0;
	
	for(uint8_t b=9,nibble=0;nibble<10;nibble++){
		uint8_t parity = 0;
		for(uint8_t bit=0;bit<4;bit++){
			if(data[5-((nibble*4+bit)/8)] & (0x80>>((nibble*4+bit)%8))){
				rfid_buffer[b>>3] |= 0x80 >> (b & 0x07);
				parity ^=1;
			}
			b++;
		}
		if(parity) rfid_buffer[b>>3] |= 0x80 >> (b & 0x07);
		b++;
	}
	for(uint8_t col=0;col<4;col++){
		uint8_t parity = 0;
		for(uint8_t row=0;row<10;row++){
			if(rfid_buffer[(row*5+col+9)/8] & 0x80>>((row*5+col+9)%8)) parity ^= 1;
		}
		if(parity) rfid_buffer[7] |= 0x80 >> (3+col);
	}
}

uint8_t inline rfid_in()
{
	if(ADCH > avg_u) return 1;
	return 0;
}

uint8_t rfid_read(uint8_t* data)
{
	uint16_t sum = 0;
	ADMUX = (0 << REFS1)|(1 << REFS0) 					// опорное напряжение AVCC
	|(1 << ADLAR)										// смещение результата (влево при 1, читаем 8 бит из ADCH)
	|(RFID_IN);											// вход RFID_IN
	
	for(uint8_t i=0;i<100;i++){							//определяем среднее напряжение
		sum += ADCH;
		_delay_us(20);
	}
	avg_u = sum / 100;
	
	for (uint8_t i=0;i<RFID_BUFFER_SIZE;i++) rfid_buffer[i] = 0;//очищаем буфер приема
	
	for (uint8_t i=0,phase=0,temp,time;i<RFID_BUFFER_SIZE*8;){	//принимаем код ключа
		temp = rfid_in();
		for(time=5;time<70;time++){
			_delay_us(10);
			if(temp != rfid_in()){_delay_us(50); break;}
		}
		if((time < 9) || (time > 64)) return RFID_NO_KEY;
		if(time > 37){											//декодируем манчестер на ходу
			if(temp) rfid_buffer[i/8] |= 1<<(i%8);
			phase = temp;
			i++;
		}else if(phase != temp){
			if(temp == 0) rfid_buffer[i/8] |= 1<<(i%8);
			i++;
		}
	}
	for (uint8_t s=0,ones=0,error=0;s<RFID_BUFFER_SIZE*8-54;s++){//обрабатываем код ключа
		if(rfid_buffer[s/8] & 1<<(s%8)) ones++;
		else ones = 0;
		if(ones == 9){											//находим стартовый заголовок
			ones = 0;
			s++;
			for(uint8_t r=0;r<10;r++){							//проверяем четность по строкам
				uint8_t p = 0;
				for(uint8_t c=0;c<5;c++) if(rfid_buffer[(r*5+c+s)/8] & 1<<((r*5+c+s)%8)) p ^= 1;				
				if(p) error = 1;
			}
			for(uint8_t c=0;c<4;c++){							//проверяем четность по столбцам
				uint8_t pc = 0;
				for(uint8_t r=0;r<11;r++) if(rfid_buffer[(r*5+c+s)/8] & 1<<((r*5+c+s)%8)) pc ^= 1;
				if(pc) error = 1;
			}
			if(rfid_buffer[(54+s)/8] & 1<<((54+s)%8)) error = 1;//проверяем наличие стоп-бита
			
			if(error){											//если есть ошибки ищем код ключа дальше
				error = 0;
				continue;
			}
			
			for(uint8_t i=0;i<8;i++) data[i] = 0;				//очищаем буфер кода ключа
			
			for(uint8_t byte=0;byte<5;byte++){					//сохраняем код ключа
				for(uint8_t nibble=0;nibble<2;nibble++){
					for(uint8_t bit=0;bit<4;bit++)
						if(rfid_buffer[(byte*10+nibble*5+bit+s)/8] & 1<<((byte*10+nibble*5+bit+s)%8))
							data[5-byte] |= 0x80>>(nibble*4+bit);
				}
			}
			return RFID_OK;
		}
	}
	return RFID_PARITY_ERR;
}

uint8_t rfid_force_read(uint8_t* data)
{
	uint8_t temp = 0;
	for(uint8_t i=0; i<4;i++){
		temp = rfid_read(data);
		if(temp == RFID_OK) break;
	}
	return temp;
}

uint8_t rfid_check(uint8_t *data)
{
	uint8_t temp_data[8];
	for(uint8_t i=1;i<6;i++) temp_data[i] = ~data[i];
	uint8_t result = rfid_force_read(temp_data);
	if(result != RFID_OK) return result;
	for(uint8_t i=1;i<6;i++)if(data[i] != temp_data[i]) return RFID_MISMATCH;
	return RFID_OK;
}

void em4305_FirstFieldStop(void)									//пауза FirstFieldStop для em4305
{
	FieldOff();
	_delay_us(55*8+40);
	FieldOn();
	_delay_us(18*8-40);
}

void em4305_SendOne(void)											//отправить единицу для em4305
{
	FieldOn();
	_delay_us(32*8);
}

void em4305_SendZero(void)											//отправить ноль для em4305
{
	FieldOff();
	_delay_us(18*8+40);
	FieldOn();
	_delay_us(18*8-40);
}

void em4305_SendDataBlock(uint8_t *data)							//передать карте Em4305 блок данных 32 бита
{
	uint8_t p=0;													//шлём данные
	for(uint8_t n=0;n<4;n++)
	{
		uint8_t l_p=0;
		p^=data[n];
		for(uint8_t m=0;m<8;m++)
		{
			uint8_t mask=(1<<m);
			if (data[n]&mask)
			{
				em4305_SendOne();
				l_p^=1;
			}
			else em4305_SendZero();
		}
		if (l_p==0) em4305_SendZero();								//шлём чётность по строкам
		else em4305_SendOne();
	}
	for(uint8_t n=0;n<8;n++)										//шлём чётность по столбцам
	{
		unsigned char mask=(1<<n);
		if (p&mask) em4305_SendOne();
		else em4305_SendZero();
	}
	em4305_SendZero();												//шлём 0
	FieldOn();
	_delay_ms(30);
}

void em4305_SendLogin(uint8_t *data)								//передать карте Em4305 логин
{
	em4305_FirstFieldStop();
	em4305_SendZero();//'0'
	em4305_SendZero();//CC0											//шлём 0011
	em4305_SendZero();//CC1
	em4305_SendOne();//CC2
	em4305_SendOne();//P
	
	em4305_SendDataBlock(data);										//шлём данные
}

void em4305_write_word(uint8_t addr, uint8_t *data)					//записать слово в Em4305
{
	//HIGHT - поле выключено
	em4305_FirstFieldStop();
	em4305_SendZero();//'0'
	//шлём 0101
	em4305_SendZero();//CC0
	em4305_SendOne();//CC1
	em4305_SendZero();//CC2
	em4305_SendOne();//P
	//шлём адрес слова (младший бит первый)
	unsigned char p=0;
	for(unsigned char n=0;n<4;n++)
	{
		if (addr&(1<<n))
		{
			em4305_SendOne();
			p^=1;
		}
		else em4305_SendZero();
	}
	//дополняющие нули и чётность
	em4305_SendZero();
	em4305_SendZero();
	if (p==0) em4305_SendZero();
	else em4305_SendOne();
	//шлём данные
	em4305_SendDataBlock(data);
}

uint8_t rfid_em4305_write(uint8_t* data)							//записать EM4305
{
	FieldOn();//включаем электромагнитное поле
	_delay_ms(20);
	//шлём логин, не обязательно
	//rfid_buffer[0] = 0xFF; //password
	//rfid_buffer[1] = 0xFF;
	//rfid_buffer[2] = 0xFF;
	//rfid_buffer[3] = 0xFF;
	//em4305_SendLogin(rfid_buffer);
	//запись конфигурации (манчестер, RF/64, выдача слова 6)
	rfid_buffer[0] = 0x5F;
	rfid_buffer[1] = 0x80;
	rfid_buffer[2] = 0x01;
	rfid_buffer[3] = 0x00;
	em4305_write_word(4,rfid_buffer);
	
	//задаём номер карты
	rfid_encode(data);
	
	//функция em4305_write_word требует прямой порядок бит, а после перекодирования порядок обратный - переставляем
	for(uint8_t n=0;n<8;n++)
	{
		uint8_t v = rfid_buffer[n];
		rfid_buffer[n] = 0;
		for(uint8_t m=0;m<8;m++)
		{
			uint8_t m1 = (1<<m);
			uint8_t m2 = (128>>m);
			if (v&m1) rfid_buffer[n] |= m2;
		}
	}
	//запись ID карты в слова 5 и 6
	em4305_write_word(5,&rfid_buffer[0]);
	em4305_write_word(6,&rfid_buffer[4]);

	FieldOff();
	_delay_ms(30);
	FieldOn();
	_delay_ms(20);
	return rfid_check(data);
}

void t5557_write(uint8_t bit){
	if(bit)_delay_us(30*8);//240
	_delay_us(15*8);//120
	FieldOff();
	_delay_us(32*8);//250
	FieldOn();
}

void t5557_write_block(uint8_t* data, uint8_t address)
{
	FieldOff();		//start gap
	_delay_us(32*8);//250
	FieldOn();
	t5557_write(1); //
	t5557_write(0); // opcode + lock bit
	t5557_write(0); //
	for(uint8_t i=0;i<32;i++)t5557_write(data[i/8] & (0x80>>(i%8)));	//data
	for(uint8_t i=0;i<3;i++) t5557_write(address & (0x04>>i));	//address
	_delay_ms(30);		//wait for writing
}

uint8_t rfid_t5557_write(uint8_t* data)									//записать t5557/t5577
{
	//задаём конфигурацию карты
	rfid_buffer[0] = 0x00;
	rfid_buffer[1] = 0x14;	//bit rate = FCK/64
	rfid_buffer[2] = 0x80;	//modulation = manchester
	rfid_buffer[3] = 0x40;	//max block = 2
	t5557_write_block(rfid_buffer, 0x00);
	rfid_encode(data);
	t5557_write_block(&rfid_buffer[0], 0x01);
	t5557_write_block(&rfid_buffer[4], 0x02);
	
	FieldOff();
	_delay_ms(30);
	FieldOn();
	_delay_ms(20);
	
	return rfid_check(data);
}
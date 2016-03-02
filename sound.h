/*
 * sound.h
 *
 * Created: 15.02.2016 15:56:58
 *  Author: Elektron
 */ 
#pragma once

//***************************************************************************
//
//  ажда€ нота кодируетс€ 1 байтом:
// 0-4 бит - код тона ноты
// 5-7 бит - код длительности ноты
//  
// Ќота с кодом 1 - до 1-й октавы, с кодом 2 - до# 1-й октавы, с кодом 3 - ре 1-й октавы
// и т.д. ¬сего 32 ноты.
// 
// ≈сли 0-4 биты равны 0, то это означает паузу длительностью, задаваемой 
// битами 5-7.
//
//  онец мелодии обозначаетс€ нулем (0x00),
//
//***************************************************************************
 
#define MUTE	0
#define C1		1
#define Cd1		2
#define D1		3
#define Dd1		4
#define E1		5
#define F1		6
#define Fd1		7
#define G1		8
#define Ab1		9
#define A1		10
#define Bb1		11
#define B1		12

#define C2		13
#define Cd2		14
#define D2		15
#define Dd2		16
#define E2		17
#define F2		18
#define Fd2		19
#define G2		20
#define Ab2		21
#define A2		22
#define Bb2		23
#define B2		24

#define C3		25
#define Cd3		26
#define D3		27
#define Dd3		28
#define E3		29
#define F3		30
#define Fd3		31

#define T0		0
#define T1		32
#define T2		64
#define T3		96
#define T4		128
#define T5		160
#define T6		196
#define T7		224

#define SOUND_PORT	PORTB
#define SOUND_DDR	DDRB
#define SOUND_OUT	1

void sound_init(void);

void sound_play(const uint8_t* melody);
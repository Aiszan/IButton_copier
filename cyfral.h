/*
 * cyfral.h
 *
 * Created: 08.02.2016 20:39:34
 *  Author: Elektron
 */
 #pragma once

 #define CL_READ_OK		0
 #define CL_NO_KEY		1

 uint8_t cl_data[2];

 void cl_init(void);
 uint8_t cl_read(uint8_t* data);
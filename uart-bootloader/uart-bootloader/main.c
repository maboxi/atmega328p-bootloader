/*
 * uart-bootloader.c
 *
 * Created: 15.08.2024 17:05:30
 * Author : maxip
 */ 

/*

Links:
	- https://onlinedocs.microchip.com/pr/GUID-CBA6F7C8-69DB-44BF-ACE3-6949CC30E98A-en-US-5/index.html?GUID-A7428434-004A-48B1-B918-3373D07A0985

*/

/*

Fuses:
read with: C:\tools\avrdude-v7.3-windows-x64\avrdude.exe -c usbasp -p m328p -n -U efuse:r:-:b -U hfuse:r:-:b -U lfuse:r:-:b

extended fuse byte:	-----101
	^= Brown-Out Detector trigger level 101
high fuse byte:		11011010
	^=
		- External Reset Disable:	-
		- debugWire enable:			-
		- Serial Programm and
		  Data Downloading Enable:	X
		- Watchtog Timer Always On: -
		- EEPROM memory preserved:	-
		- BOOTSZ1:					X
		- BOOTSZ0:					-
		- BOOTRST					X
		=> BOOTSZ = 01, BOOTRST programmed =>
			- reset vector points to bootloader section
			- 1024 words boot (= 32 pages)
			- Application Flash: 0x0000 - 0x3BFF
			- Bootloader Flash:  0x3C00 - 0x3FFF
	
low fuse byte:		11111111

*/

#define F_CPU 16000000

#include <avr/io.h>
#include <avr/boot.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#define BAUDRATE 19200
#include "MyUSART.h"

__attribute__ ((section (".application"))) int application();

// bootloader entry
int main() {
	uint8_t counter = 0;
	uint8_t temp;
	
	// select bootloader interrupt vector
	cli();
	temp = MCUCR;
	MCUCR = temp | (1<<IVCE);
	MCUCR = temp | (1<<IVSEL);
	sei();
	
	USART_Init();
	
	DDRB |= (1<<DDB5);
	DDRB &= (1<<DDB0);
	PORTB |= (1<<PORTB0); // enable pullup
	
	USART_TransmitString("\r\n-----------------------------------------------\r\n");
	USART_TransmitString("Hello");
	
	if(PINB & (1<<PINB0)) {
		USART_TransmitString(" and goodbye from bootloader!\r\n");
	} else {
		while (counter < 3*2)
		{
			counter++;
			if(PORTB & (1<<PORTB5))
			PORTB = PORTB & (~(1<<PORTB5));
			else
			PORTB = PORTB | (1<<PORTB5);
			_delay_ms(1000);
		}
		
		USART_TransmitString("from bootloader!\r\n");
	}
	
	
	
	// select application interrupt vector
	cli();
	temp = MCUCR;
	MCUCR = temp | (1<<IVCE);
	MCUCR = temp & ~(1<<IVSEL);
	
	// TODO: reset all peripherals to default settings
	application();
}

int application() {
	uint8_t counter = 0;
	
	DDRB |= (1<<DDB5);
	DDRD |= (1<<DDD5) | (1<<DDD6) | (1<<DDD7);
	PORTD &= ~(1<<PORTD5) & ~(1<<PORTD6) & ~(1<<PORTD7);
	
	USART_TransmitString("Hello from application!\r\n");
	
	while (1)
	{
		if(PORTB & (1<<PORTB5)) {
			PORTB = PORTB & (~(1<<PORTB5));
			counter++;
			_delay_ms(1000);
		} else {
			PORTB = PORTB | (1<<PORTB5);
			//USART_TransmitString(".");
			_delay_ms(200);
		}
		
		uint8_t temp = PORTD;
		temp &= ~(1<<PORTD5) & ~(1<<PORTD6) & ~(1<<PORTD7);
		temp |= (counter & 0b111) << 5;
		PORTD = temp;
	}
}
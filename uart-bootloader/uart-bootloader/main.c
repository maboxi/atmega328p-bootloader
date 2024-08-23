/*
 * uart-bootloader.c
 *
 * Created: 15.08.2024 17:05:30
 * Author : maxip
 */ 

/*

Links:
	- https://onlinedocs.microchip.com/pr/GUID-CBA6F7C8-69DB-44BF-ACE3-6949CC30E98A-en-US-5/index.html?GUID-A7428434-004A-48B1-B918-3373D07A0985
	- https://onlinedocs.microchip.com/oxy/GUID-C3F66E96-7CDD-47A0-9AB7-9068BADB46C0-en-US-3/GUID-DF9E479D-6BA8-49E3-A2A5-997BBA49D34D.html
	
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

/*
	Configuration options for the bootloader before compilation:
	
Boot Loader Enable (BLE) Type: How does the bootloader know to continue execution or to directly switch to the application?
	- Bootmode Enable Switch / Button
	- UART Signal
	- SPI Signal
	- ... (to be continued)

Boot Loader Enable Switch Pin: If BLE Type is switch, define pin and ports here
	-> will be configured as input with internal pullup activated -> LOW signal = bootloader active

*/
// BLE Type
#define BLE_BUTTON 1
#define BLE_UART 2
#define BL_ENABLE_TYPE BLE_BUTTON

// BLE Switch
#define BLE_SWITCH_DDRX DDRB
#define BLE_SWITCH_DDRXn DDB0
#define BLE_SWITCH_PORTX PORTB
#define BLE_SWITCH_PORTXn PORTB0
#define BLE_SWITCH_PINX PINB
#define BLE_SWITCH_PINXn PINB0

#define BL_PREFIX "[BL] "

// Red, Green, Blue Test LEDs
#define LED_RED	1
#define LED_GREEN 2
#define LED_BLUE 4

#include <avr/io.h>
#include <avr/boot.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#define BAUDRATE 19200
#include "MyUSART.h"

__attribute__ ((section (".application"))) int application();

void set_rgb_leds(uint8_t flag) {
	uint8_t temp = PORTD;
	temp &= ~(1<<PORTD5) & ~(1<<PORTD6) & ~(1<<PORTD7);
	temp |= (flag & 0b111) << 5;
	PORTD = temp;
}

// bootloader entry
int main() {
	uint8_t temp;
	
	// select bootloader interrupt vector
	cli();
	temp = MCUCR;
	MCUCR = temp | (1<<IVCE);
	MCUCR = temp | (1<<IVSEL);
	sei();
	
	// Boot Mode Enable Switch
	BLE_SWITCH_DDRX &= (1<<BLE_SWITCH_DDRXn);
	BLE_SWITCH_PORTX |= (1<<BLE_SWITCH_PORTXn); // enable pullup
	
	if(!(BLE_SWITCH_PINX & (1<<BLE_SWITCH_PINXn))) {
		DDRB |= (1<<DDB5);
		DDRD |= (1<<DDD5) | (1<<DDD6) | (1<<DDD7);
		
		USART_Init();
		
		USART_TransmitString(BL_PREFIX "Bootloader active!\r\n");
		
		uint8_t bl_run = 1;
		while(bl_run) {
			USART_TransmitString(BL_PREFIX "Send command: ");
			set_rgb_leds(LED_RED); // waiting for input
			char code = USART_Receive();
			USART_Transmit(code);
			USART_NewLine();
			set_rgb_leds(LED_GREEN);
			switch(code) {
				case 'q': {
					USART_TransmitString(BL_PREFIX "Exiting bootloader...\r\n");
					bl_run = 0;
					break;
				}
				case 'f': {
					USART_TransmitString(BL_PREFIX "Reading fuses...\r\n");
					uint8_t fuses_lo = boot_lock_fuse_bits_get(GET_LOW_FUSE_BITS);
					uint8_t fuses_hi = boot_lock_fuse_bits_get(GET_HIGH_FUSE_BITS);
					uint8_t fuses_ex = boot_lock_fuse_bits_get(GET_EXTENDED_FUSE_BITS);
					
					set_rgb_leds(LED_BLUE);
					USART_TransmitString("    Extended Fuse Bits:  -----");
					USART_TransmitBinChar(fuses_ex, 3, 0);
					USART_NewLine();
					USART_TransmitString("    High Fuse Bits:      ");
					USART_TransmitBinChar(fuses_hi, 8, 0);
					USART_NewLine();
					USART_TransmitString("    Low Fuse Bits:       ");
					USART_TransmitBinChar(fuses_lo, 8, 0);
					USART_NewLine();
					set_rgb_leds(LED_GREEN);
					
					break;
				}
				case 'h': {
					USART_TransmitString("    h: Help\r\n");
					USART_TransmitString("    q: Quit bootloader\r\n");
					USART_TransmitString("    f: Read fuses\r\n");
					break;
				}
				default: {
					USART_TransmitString(BL_PREFIX "Unknown code received: ");
					USART_Transmit(code);
					USART_NewLine();
					USART_TransmitString(BL_PREFIX "Send h for help");
					USART_NewLine();
					break;
				}
			}
		}
		
		USART_TransmitString(BL_PREFIX "Bootloader end!\r\n");
		_delay_ms(50);
		USART_AwaitTx();
	}
	
	// TODO: reset all peripherals to default settings
	
	// select application interrupt vector
	cli();
	temp = MCUCR;
	MCUCR = temp | (1<<IVCE);
	MCUCR = temp & ~(1<<IVSEL);
	
	cli();
	application();
}

int application() {
	uint8_t counter = 0;
	
	DDRB |= (1<<DDB5);
	DDRD |= (1<<DDD5) | (1<<DDD6) | (1<<DDD7);
	PORTD &= ~(1<<PORTD5) & ~(1<<PORTD6) & ~(1<<PORTD7);
	
	USART_Init();
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
		
		set_rgb_leds(counter);
	}
}
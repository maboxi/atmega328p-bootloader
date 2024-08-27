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

/*

Memory Information (see atmega datasheet):
	- Flash Size: 16K Words (32K Bytes)
	- Page Size: 64 Words -> #pages = 256
	- PCWORD = PC[5:0]
	- PCPAGE = PC[13:6] -> PCMSB = PC[13]
	- PC for SPM instruction: ZH & ZL

*/

#define F_CPU 16000000
#define BL_INFO_VERSION "0.1"
#define BL_INFO_BLSECTIONSTART (2 * 0x3C00)

#include "bootloader-communication.h"

/*
	Configuration options for the bootloader before compilation:
	
Boot Loader Enable (BLE) Type: How does the bootloader know to continue execution or to directly switch to the application?
	- Bootmode Enable Switch / Button
	- Always (with timeouts)
	- ... (to be extended)

Boot Loader Enable Switch Pin: If BLE Type is switch, define pin and ports here
	-> will be configured as input with internal pullup activated -> LOW signal = bootloader active

*/
// BLE Type
#define BLE_BUTTON 1
#define BLE_ALWAYS 2
#define BL_ENABLE_TYPE BLE_ALWAYS

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

#include <stdint.h>
#include <avr/io.h>
#include <avr/boot.h>
#include <avr/interrupt.h>
#include <alloca.h>
#include <avr/pgmspace.h>
#include <util/delay.h>

#define BAUDRATE 19200
#include "MyUSART.h"

uint8_t* const bl_sectionstartaddress = (uint8_t* const) BL_INFO_BLSECTIONSTART;

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
	
	// disable SPM for the bootloader section
	// boot_lock_bits_set_safe(0b0011); // TODO!
	
	// Boot Mode Enable Switch
	BLE_SWITCH_DDRX &= (1<<BLE_SWITCH_DDRXn);
	BLE_SWITCH_PORTX |= (1<<BLE_SWITCH_PORTXn); // enable pullup
	
	// check if boot mode should be entered

#if BL_ENABLE_TYPE == BLE_BUTTON
	if(!(BLE_SWITCH_PINX & (1<<BLE_SWITCH_PINXn))) {
#else // BL_ENABLE_TYPE == BLE_ALWAYS
	{
#endif // BL_ENABLE_TYPE == BLE_BUTTON
		DDRB |= (1<<DDB5);
		DDRD |= (1<<DDD5) | (1<<DDD6) | (1<<DDD7);
		
		USART_Init();
		
		USART_Transmit(BL_COM_BL_READY);
		
		uint8_t bl_run = 1;
		while(bl_run) {
			set_rgb_leds(LED_RED); // waiting for input
			char code = USART_Receive();
			set_rgb_leds(LED_GREEN);
			switch(code) {
			// Quit bootloader
				case 'q': {
					set_rgb_leds(LED_BLUE);
					USART_Transmit(BL_COM_REPLY_QUITTING);
					bl_run = 0;
					break;
				}
				// Send information about the bootloader: Version, Boot Section Start Address
				case 'i': {
					USART_Transmit(BL_COM_REPLY_OK);
					
					USART_Transmit(sizeof(BL_INFO_VERSION) - 1);
					USART_TransmitString(BL_INFO_VERSION);
					
					USART_Transmit(sizeof(bl_sectionstartaddress));
					for(uint8_t i = 0; i < sizeof(bl_sectionstartaddress); i++) {
						USART_Transmit((uint8_t) ((uint16_t)bl_sectionstartaddress >> (8*i)) );
					}
					
					break;
				}
				// read low, high, extended fuse bytes
				case 'f': {
					set_rgb_leds(LED_BLUE);
					USART_Transmit(BL_COM_REPLY_OK | 3);
				
					uint8_t fuses_lo = boot_lock_fuse_bits_get(GET_LOW_FUSE_BITS);
					uint8_t fuses_hi = boot_lock_fuse_bits_get(GET_HIGH_FUSE_BITS);
					uint8_t fuses_ex = boot_lock_fuse_bits_get(GET_EXTENDED_FUSE_BITS);
				
					USART_Transmit(fuses_lo);
					USART_Transmit(fuses_hi);
					USART_Transmit(fuses_ex);
				
					set_rgb_leds(LED_GREEN);
				
					break;
				}
				// upload program
				case 'u': {
					USART_Transmit(BL_COM_REPLY_NOTIMPLEMENTEDYET);
					break;
				}
				// verify memory
				case 'v': {
					USART_Transmit(BL_COM_REPLY_OK);
					
					set_rgb_leds(0);
					
					uint16_t addrh = (uint16_t) USART_Receive();
					set_rgb_leds(1);
					uint16_t addrl = (uint16_t) USART_Receive();
					set_rgb_leds(2);
					uint8_t num_bytes = USART_Receive();
					set_rgb_leds(3);
					
					uint8_t* addr = (uint8_t*) ((addrh << 8) | addrl); 
					uint8_t* buffer = (uint8_t*) alloca(num_bytes);
					uint8_t buffer_counter = 0;
					
					while(buffer_counter < num_bytes) {
						if(num_bytes == 1) {
							// read byte
							uint8_t byte = pgm_read_byte(addr + buffer_counter);
							buffer[buffer_counter] = byte;
							buffer_counter += 1;
						} else if(num_bytes == 2 || num_bytes == 3) {
							// read word
							uint16_t word = pgm_read_word(addr + buffer_counter);
							uint8_t* byte_buf = (uint8_t*) &word;
							buffer[buffer_counter] = byte_buf[0];
							buffer[buffer_counter + 1] = byte_buf[1];
							buffer_counter += 2;
						} else {
							// read dword
							uint32_t dword = pgm_read_dword(addr + buffer_counter);
							uint8_t* byte_buf = (uint8_t*) &dword;
							buffer[buffer_counter] = byte_buf[0];
							buffer[buffer_counter + 1] = byte_buf[1];
							buffer[buffer_counter + 2] = byte_buf[2];
							buffer[buffer_counter + 3] = byte_buf[3];
							buffer_counter += 4;
						}
					}
					
					for(uint8_t i = 0; i < num_bytes; i++)
						USART_Transmit(buffer[i]);
					
					set_rgb_leds(LED_GREEN);
					
					break;
				}
				// Unknown command
				default: {
					USART_Transmit(BL_COM_REPLY_UNKNOWNCMD);
					USART_Transmit(code);
					break;
				}
			}
			set_rgb_leds(LED_GREEN);
		}
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
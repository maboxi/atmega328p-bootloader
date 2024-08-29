/*
 * uart-bootloader.c
 *
 * Created: 15.08.2024 17:05:30
 * Author : maxip
 */ 

#define F_CPU 16000000
#define BL_INFO_VERSION "0.1"
#define BL_INFO_BLSECTIONSTART (2 * 0x3800)

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
high fuse byte:		11011010 --larger bls--> 0b11011000 = 0xD8
	^=
		- External Reset Disable:	-
		- debugWire enable:			-
		- Serial Programm and
		  Data Downloading Enable:	X
		- Watchtog Timer Always On: -
		- EEPROM memory preserved:	-
		- BOOTSZ1:					X
		- BOOTSZ0:					X
		- BOOTRST					X
		=> BOOTSZ = 11, BOOTRST programmed =>
			- reset vector points to bootloader section
			- 2048 words boot (= 64 pages)
			- Application Flash: 0x0000 - 0x37FF
			- Bootloader Flash:  0x3800 - 0x3FFF
	
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

// hex file decoding
#define HEX_RTYPE_DATARECORD 0
#define HEX_RTYPE_EOF 1
#define HEX_RTYPE_STARTSEGMENTADDRESSRECORD 3

#include <stdint.h>
#include <avr/io.h>
#include <avr/boot.h>
#include <avr/interrupt.h>
#include <alloca.h>
#include <avr/pgmspace.h>
#include <util/delay.h>

#define BAUDRATE 19200
#include "MyUSART.h"

volatile uint16_t page_start_address = 0;
volatile uint16_t next_page_start_address = SPM_PAGESIZE;
volatile uint8_t page_used = 0;

uint8_t* const bl_sectionstartaddress = (uint8_t* const) BL_INFO_BLSECTIONSTART;

__attribute__ ((section (".application"))) int application();


void set_rgb_leds(uint8_t flag) {
	uint8_t temp = PORTD;
	temp &= ~(1<<PORTD5) & ~(1<<PORTD6) & ~(1<<PORTD7);
	temp |= (flag & 0b111) << 5;
	PORTD = temp;
}

static inline void handle_page_write(uint8_t* ram_page_buffer) {
	uint16_t counter = 0;
	if(page_used) {
		// write page
		for(counter = 0; counter < SPM_PAGESIZE; counter += 2) {
			boot_spm_busy_wait();
			boot_page_fill(counter, ram_page_buffer[counter + 1] << 8 | ram_page_buffer[counter]);
		}
		
		boot_spm_busy_wait();
		boot_page_erase(page_start_address);
		boot_spm_busy_wait();
		boot_page_write(page_start_address);
		boot_spm_busy_wait();
	}
}

void handle_hex_data(uint16_t addr, uint8_t bytecount, uint8_t* data_buf, uint8_t* ram_page_buffer) {
	uint8_t sreg;
	
	sreg = SREG;
	cli();
	
	uint16_t address_offset = 0;
	uint16_t counter = 0;
	
	// new data starts in current page
	if(addr >= page_start_address && addr < next_page_start_address) {
		if(!page_used) {
			// enable reading (page write & erase will disable this)
			boot_spm_busy_wait();
			boot_rww_enable();
			
			// fill temporary page buffer with current content of new page
			for(counter = 0; counter < SPM_PAGESIZE; counter++) {
				ram_page_buffer[counter] = pgm_read_byte(page_start_address + counter);
			}
			page_used = 1;
		}
		
		address_offset = addr - page_start_address;
		for(counter = 0; counter < bytecount && addr + counter < next_page_start_address; counter++) {
			// write word to temporary page buffer
			ram_page_buffer[address_offset + counter] = data_buf[counter];
		}
		
		// handle data that spans over multiple pages
		if(addr + bytecount - 1 >= next_page_start_address) {
			uint16_t overlap = addr + bytecount - next_page_start_address;
			handle_hex_data(next_page_start_address, overlap, data_buf + bytecount - overlap, ram_page_buffer);
		}
		
	} else {
		handle_page_write(ram_page_buffer);
		
		page_start_address = addr & ~(SPM_PAGESIZE - 1);
		next_page_start_address = page_start_address + SPM_PAGESIZE;
		page_used = 0;
		
		handle_hex_data(addr, bytecount, data_buf, ram_page_buffer);
	}
	
	SREG = sreg;
}

uint8_t get_hex_val_8(uint8_t* hexval, uint8_t* read_buffer, uint8_t start) {
	if(read_buffer[start] >= 'A' && read_buffer[start] <= 'F') {
		*hexval = 0xA + (read_buffer[start] - 'A');
	} else if(read_buffer[start] >= '0' && read_buffer[start] <= '9') {
		*hexval = read_buffer[start] - '0';
	} else {
		return 1;
	}
	
	*hexval = *hexval << 4;
	
	if(read_buffer[start + 1] >= 'A' && read_buffer[start + 1] <= 'F') {
		*hexval += 0xA + (read_buffer[start + 1] - 'A');
	} else if(read_buffer[start + 1] >= '0' && read_buffer[start +1] <= '9') {
		*hexval += read_buffer[start + 1] - '0';
	} else {
		return 1;
	}
	
	return 0;
}

uint8_t get_hex_val_16(uint16_t* hexval, uint8_t* read_buffer, uint8_t start) {
	if(get_hex_val_8((uint8_t*) hexval + 1, read_buffer, start) || get_hex_val_8((uint8_t*) hexval, read_buffer, start + 2)) {
		return 1;
	}
	return 0;
}

static inline void _handle_cmd_upload() {
	uint8_t ram_page_buffer[SPM_PAGESIZE];
	
	set_rgb_leds(0);
					
	uint8_t upload_running = 1;
	while(upload_running) {
		set_rgb_leds(7);
		uint8_t read_buffer[9];
		USART_ReceiveMultiple((char*)read_buffer, 9);
						
		if(read_buffer[0] != ':') {
			USART_Transmit(BL_COM_REPLY_UPLOADERROR | BL_COM_UPLOADERR_COLON);
			upload_running = 0;
			break;
		}
						
		uint8_t bytecount;
		if(get_hex_val_8(&bytecount, read_buffer, 1)) {
			USART_Transmit(BL_COM_REPLY_UPLOADERROR | BL_COM_UPLOADERR_HEXVAL_8);
			upload_running = 0;
			break;
		}
						
		uint8_t rtype;
		if(get_hex_val_8(&rtype, read_buffer, 7)) {
			USART_Transmit(BL_COM_REPLY_UPLOADERROR | BL_COM_UPLOADERR_HEXVAL_8);
			upload_running = 0;
			break;
		}
						
		USART_Transmit(BL_COM_REPLY_OK | BL_COM_UPLOADOK_HEADEROK);
		set_rgb_leds(6);
						
		uint8_t* data_buf = alloca(bytecount * 2 + 2);
		USART_ReceiveMultiple((char*)data_buf, bytecount * 2 + 2);
						
		set_rgb_leds(5);
						
		switch(rtype) {
			case HEX_RTYPE_EOF: {
								
				uint16_t address_val;
				if(get_hex_val_16(&address_val, read_buffer, 3)) {
					USART_Transmit(BL_COM_REPLY_UPLOADERROR | BL_COM_UPLOADERR_HEXVAL_16);
					upload_running = 0;
					break;
				}
								
				uint8_t checksum_val;
				if(get_hex_val_8(&checksum_val, data_buf, 0)) {
					USART_Transmit(BL_COM_REPLY_UPLOADERROR | BL_COM_UPLOADERR_HEXVAL_16);
					upload_running = 0;
					break;
				}
								
				uint8_t checksum = 0;
				checksum += bytecount;
				checksum += rtype;
				checksum += (uint8_t) (address_val >> 8);
				checksum += (uint8_t) address_val;
				checksum += checksum_val;
								
				if(checksum != 0) {
					USART_Transmit(BL_COM_REPLY_UPLOADERROR | BL_COM_UPLOADERR_CHECKSUM);
					upload_running = 0;
					break;
				}
								
				handle_page_write(ram_page_buffer);
				USART_Transmit(BL_COM_REPLY_OK | BL_COM_UPLOADOK_FINISHED);
				upload_running = 0;
				break;
			}
			case HEX_RTYPE_STARTSEGMENTADDRESSRECORD: {
				/*
				uint16_t address_val;
				if(get_hex_val_16(&address_val, read_buffer, 3)) {
					USART_Transmit(BL_COM_REPLY_UPLOADERROR | BL_COM_UPLOADERR_HEXVAL_16);
					upload_running = 0;
					break;
				}
								
				uint8_t checksum_val;
				if(get_hex_val_8(&checksum_val, data_buf, 0)) {
					USART_Transmit(BL_COM_REPLY_UPLOADERROR | BL_COM_UPLOADERR_HEXVAL_16);
					upload_running = 0;
					break;
				}
								
				uint8_t checksum = 0;
				checksum += bytecount;
				checksum += rtype;
				checksum += (uint8_t) (address_val >> 8);
				checksum += (uint8_t) address_val;
				checksum += checksum_val;
								
				if(checksum != 0) {
					USART_Transmit(BL_COM_REPLY_UPLOADERROR | BL_COM_UPLOADERR_CHECKSUM);
					upload_running = 0;
					break;
				}
				*/
								
				USART_Transmit(BL_COM_REPLY_OK);
				break;
			}
			case HEX_RTYPE_DATARECORD: {
				uint16_t address_val;
				if(get_hex_val_16(&address_val, read_buffer, 3)) {
					USART_Transmit(BL_COM_REPLY_UPLOADERROR | BL_COM_UPLOADERR_HEXVAL_16);
					upload_running = 0;
					break;
				}
																
				uint8_t checksum = 0;
				for(uint8_t i = 0; i < bytecount + 1; i++) { // + 1: checksum
					uint8_t byte;
					if(get_hex_val_8(&byte, data_buf, 2*i)) {
						USART_Transmit(BL_COM_REPLY_UPLOADERROR | BL_COM_UPLOADERR_HEXVAL_8);
						upload_running = 0;
						break;
					}
					data_buf[i] = byte;
					checksum += byte;
				}
								
				// checksum check
				checksum += bytecount;
				checksum += rtype;
				checksum += (uint8_t) (address_val >> 8);
				checksum += (uint8_t) address_val;
								
				if(checksum != 0) {
					USART_Transmit(BL_COM_REPLY_UPLOADERROR | BL_COM_UPLOADERR_CHECKSUM);
					upload_running = 0;
					break;
				}
								
				set_rgb_leds(4);
								
				// TODO: handle data upload pagewise
								
				handle_hex_data(address_val, bytecount, data_buf, ram_page_buffer);
								
				USART_Transmit(BL_COM_REPLY_OK | BL_COM_UPLOADOK_LINEOK);
			}
		}
	}
}

static inline void _handle_cmd_verify() {
	set_rgb_leds(LED_BLUE);
	
	uint16_t addrh = (uint16_t) USART_Receive();
	uint16_t addrl = (uint16_t) USART_Receive();
	uint8_t num_bytes = USART_Receive();
	
	uint8_t* addr = (uint8_t*) ((addrh << 8) | addrl);
	uint8_t* buffer = (uint8_t*) alloca(num_bytes);
	uint8_t buffer_counter = 0;
	
	boot_spm_busy_wait();
	boot_rww_enable();
	
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
	
	set_rgb_leds(LED_GREEN);
	
	for(uint8_t i = 0; i < num_bytes; i++)
		USART_Transmit(buffer[i]);
}

static inline void _handle_cmd_fuses() {
	set_rgb_leds(LED_BLUE);
	
	uint8_t fuses_lo = boot_lock_fuse_bits_get(GET_LOW_FUSE_BITS);
	uint8_t fuses_hi = boot_lock_fuse_bits_get(GET_HIGH_FUSE_BITS);
	uint8_t fuses_ex = boot_lock_fuse_bits_get(GET_EXTENDED_FUSE_BITS);
	uint8_t locks = boot_lock_fuse_bits_get(GET_LOCK_BITS);
	
	USART_Transmit(fuses_lo);
	USART_Transmit(fuses_hi);
	USART_Transmit(fuses_ex);
	USART_Transmit(locks);
	
	set_rgb_leds(LED_GREEN);
}

static inline void _handle_cmd_info() {
	USART_Transmit(sizeof(BL_INFO_VERSION) - 1);
	USART_TransmitString(BL_INFO_VERSION);
	
	USART_Transmit(sizeof(bl_sectionstartaddress));
	for(uint8_t i = 0; i < sizeof(bl_sectionstartaddress); i++) {
		USART_Transmit((uint8_t) ((uint16_t)bl_sectionstartaddress >> (8*i)) );
	}
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
	//boot_lock_bits_set (_BV (BLB11));
	
	// Boot Mode Enable Switch
	BLE_SWITCH_DDRX &= (1<<BLE_SWITCH_DDRXn);
	BLE_SWITCH_PORTX |= (1<<BLE_SWITCH_PORTXn); // enable pullup
	
	// prepare bootloader globals
	page_start_address = 0;
	next_page_start_address = SPM_PAGESIZE;
	page_used = 0;
	
	
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
					_handle_cmd_info();
					
					break;
				}
				// read low, high, extended fuse bytes
				case 'f': {
					USART_Transmit(BL_COM_REPLY_OK);
					_handle_cmd_fuses();	
								
					break;
				}
				// upload program
				case 'u': {
					USART_Transmit(BL_COM_REPLY_OK);
					_handle_cmd_upload();
					
					break;
				}
				// verify memory
				case 'v': {
					USART_Transmit(BL_COM_REPLY_OK);
					_handle_cmd_verify();
					
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
		USART_AwaitTX();
	}
	
	// TODO: reset all peripherals to default settings
	
	// enable rww section
	boot_rww_enable_safe();
	
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
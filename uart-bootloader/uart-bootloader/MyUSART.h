/*
 * MyUSART.h
 *
 * Created: 25.05.2021 16:24:10
 *  Author: maxip
 *
 * USART API for Atmega 328P
 *
 * !! Initialize with USART_Init() !!
 *
 */ 


#ifndef MYUSART_H_
#define MYUSART_H_

#include <avr/interrupt.h>
#include <stdlib.h>
#include <avr/pgmspace.h>

#ifndef BAUDRATE // allow custom baudrate
#define BAUDRATE 9600
#endif // BAUDRATE

#define BAUD_CONST (((F_CPU/(BAUDRATE*16UL)))-1)

#ifndef RX_BUFFERSIZE
#define RX_BUFFERSIZE 128
#endif // RX_BUFFERSIZE

#define RX_FREE_XOFF 4
#define RX_FREE_XON 16
#define XON 0x11
#define XOFF 0x13

#define ASCII_CR 0x0d
#define ASCII_LF 0x0a

volatile char rxBuffer[RX_BUFFERSIZE];
volatile uint8_t rxBufferStart = 0, rxBufferEnd = 0, rxBufferFree = RX_BUFFERSIZE, rxStatus = 1;

void USART_Init(){
	UBRR0H = (BAUD_CONST >> 8);
	UBRR0L = BAUD_CONST;
	UCSR0B |= (1<<RXEN0)|(1<<TXEN0);
	
	UCSR0B |= (1<<RXCIE0);
}

void USART_Transmit(char data){
	while(!(UCSR0A & (1<<UDRE0))) ;
	UDR0 = data;
}

void USART_NewLine() {
	USART_Transmit(ASCII_CR);
	USART_Transmit(ASCII_LF);
}

void USART_TransmitString(const char dataarr[]) {
	int i = 0;
	while(dataarr[i] != '\0')
		USART_Transmit(dataarr[i++]);
}

void USART_TransmitLine(const char dataarr[]) {
	USART_TransmitString(dataarr);
	USART_NewLine();
}


ISR(USART_RX_vect) {
	rxBuffer[rxBufferEnd] = UDR0;
	rxBufferEnd = (rxBufferEnd + 1) % RX_BUFFERSIZE;
	
	rxBufferFree -= 1;
	if(rxStatus && (rxBufferFree <= RX_FREE_XOFF || rxBufferFree == 0)) {
		USART_Transmit(XOFF);
		rxStatus = 0;
	}
}

char USART_Receive(){
	char rx;
	while(rxBufferStart == rxBufferEnd) ;
	
	cli();
	rx = rxBuffer[rxBufferStart];
	rxBufferStart = (rxBufferStart + 1) % RX_BUFFERSIZE;
	
	rxBufferFree += 1;
	if(!rxStatus && (rxBufferFree >= RX_FREE_XON || rxBufferFree >= RX_BUFFERSIZE)) {
		USART_Transmit(XON);
		rxStatus = 1;
	}
	
	sei();
	
	//while(!(UCSR0A & (1<<RXC0))) ;
	//return UDR0;
	return rx;
}

uint8_t USART_IsRXBufferEmpty() {
	return rxBufferFree == RX_BUFFERSIZE ? 1 : 0;
}

void USART_TransmitFlashString(volatile const uint16_t flashaddr, uint8_t newline) {
	char c;
	uint16_t i = 0;
	for(; i < 200; i++) {
		c = pgm_read_byte(flashaddr + i);
		
		if(c == '\0')
			break;
		
		USART_Transmit(c);
	}

	/*
	USART_NewLine();
	USART_Transmit((i / 100) % 10 + 0x30);
	USART_Transmit((i / 10) % 10 + 0x30);
	USART_Transmit((i / 1) % 10 + 0x30);
	*/
	
	if(newline)
		USART_NewLine();
}

char getHexChar(char c){
	if (c < 10)
	return c + 0x30;
	else
	return c - 10 + 0x61;
}

void USART_TransmitHexChar(uint8_t i) {
	//USART_Transmit(' ');
	USART_Transmit('0');
	USART_Transmit('x');
	USART_Transmit(getHexChar(i >> 4));
	USART_Transmit(getHexChar(i & 0x0f));
	//USART_Transmit(' ');
}

void USART_TransmitBinChar(uint8_t i) {
	USART_Transmit('0');
	USART_Transmit('b');
	for(int j = 7; j >= 0; j--)
		USART_Transmit(((i >> j) & 1) + 0x30);
}

void USART_TransmitHexWord(uint16_t i) {
	//USART_Transmit(' ');
	USART_Transmit('0');
	USART_Transmit('x');
	USART_Transmit(getHexChar((i >> 12) & 0x000f));
	USART_Transmit(getHexChar((i >> 8) & 0x000f));
	USART_Transmit(getHexChar((i >> 4) & 0x000f));
	USART_Transmit(getHexChar((i >> 0) & 0x0f));
	//USART_Transmit(' ');
}

void USART_TransmitDecimal8UB(uint8_t i) {
	if(i == 0) {
		USART_Transmit('0');
	} else {
		if(i / 100 > 0) {
			USART_Transmit(i / 100 + 0x30);
			USART_Transmit(i / 10 % 10 + 0x30);
			USART_Transmit(i % 10 + 0x30);
		} else if(i / 10 > 0) {
			USART_Transmit(i / 10 + 0x30);
			USART_Transmit(i % 10 + 0x30);
		} else {
			USART_Transmit(i + 0x30);
		}
	}
}

#endif /* MYUSART_H_ */
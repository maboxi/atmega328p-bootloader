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

#define USART_AwaitTX() {while(!(UCSR0A & (1<<UDRE0))) {}}

volatile char rxBuffer[RX_BUFFERSIZE];
volatile uint8_t rxBufferStart = 0, rxBufferEnd = 0, rxBufferFree = RX_BUFFERSIZE, rxStatus = 1;

void USART_Init(){
	UBRR0H = (BAUD_CONST >> 8);
	UBRR0L = BAUD_CONST;
	UCSR0B |= (1<<RXEN0)|(1<<TXEN0);
	
	UCSR0B |= (1<<RXCIE0);
}

void USART_Transmit(char data){
	USART_AwaitTX();
	UDR0 = data;
}

void USART_TransmitMultiple(char* data, uint8_t len) {
	for(uint8_t i = 0; i < len; i++)
		USART_Transmit(data[i]);
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

inline void USART_ReceiveMultiple(char* buffer, uint8_t bufsize) {
	for(uint8_t i = 0; i < bufsize; i++) {
		buffer[i] = USART_Receive();
	}
}

inline uint8_t USART_IsRXBufferEmpty() {
	return rxBufferFree == RX_BUFFERSIZE ? 1 : 0;
}

void USART_TransmitString(const char dataarr[]) {
	int i = 0;
	while(dataarr[i] != '\0')
		USART_Transmit(dataarr[i++]);
}

#endif /* MYUSART_H_ */
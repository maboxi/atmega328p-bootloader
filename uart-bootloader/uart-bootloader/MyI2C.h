#ifndef __MYI2C_
#define __MYI2C_

// general initialization for both modes
#include "D:\dev\avr\lib\selfmade\atmega328P\MyUSART.h"
#include <avr/io.h>

#define I2C_MODE_MASTER 0
#define I2C_MODE_SLAVE 1


volatile uint8_t I2C_mode = I2C_MODE_MASTER;

void I2C_Init() {
}

/*

* Start of master mode implementation

*/

// 0 = success; 1 = starting error; 2 = address transmission error; 3 = data transmission error
uint8_t I2C_MasterTransmitSyncArray(uint8_t addr, uint8_t *data, uint8_t datacount) {
	// send start
	TWCR = (1<<TWINT) | (1<<TWSTA) | (1<<TWEN);
	
	USART_NewLine();
	USART_TransmitLine("1");
	
	// wait for TWINT
	while(!(TWCR & (1<<TWINT))) ;
	
	USART_TransmitLine("2");
	// check for error in start send
	if((TWSR & 0xF8) != 0x08) // 0x08 = start successful
		return 1;
	
	USART_TransmitLine("3");
	// load address and write / read
	TWDR = (addr << 1) | 0; // addr + 0 for write operation
	TWCR = (1<<TWINT) | (1<<TWEN);
	
	USART_TransmitLine("4");
	// wait for TWINT
	while(!(TWCR & (1<<TWINT))) ;
	
	USART_TransmitLine("5");
	// check if ack received
	if((TWSR & 0xF8) != 0x18) // not ack received -> error
		return 2;
	
	for(uint8_t i = 0; i < datacount; i++) {
		// load data
		TWDR = *(data + i);
		TWCR = (1<<TWINT) | (1<<TWEN);
		
		// wait for TWINT
		while(!(TWCR & (1<<TWINT))) ;
		
		if((TWSR & 0xF8) != 0x28) // not ack received -> error
			return 3;
	}
	
	
	USART_TransmitLine("6");
	// send stop
	TWCR = (1<<TWINT) | (1<<TWSTO) | (1<<TWEN);
	
	return 1;
}

uint8_t I2C_MasterTransmitSyncSingle(uint8_t addr, uint8_t data) {
	uint8_t arr[1] = {data};
	return I2C_MasterTransmitSyncArray(addr, arr, 1);
}

void I2C_InitMaster() {
	I2C_mode = I2C_MODE_MASTER;
	I2C_Init();
	TWBR = 128;
	TWSR &= ~(1<<TWPS1);
	TWSR |= (1<<TWPS0);
}

/*

* Start of slave mode implementation

*/

volatile uint8_t I2C_slaveHasReceivedData = 0, I2C_slaveIsReceivingData = 0, I2C_lastReceiveWasGeneral = 0;
volatile uint8_t I2C_slaveReceivedData[256], I2C_slaveReceivedDataLength = 0;

ISR(TWI_vect) {
	uint8_t status = TWSR & 0xF8;
	USART_TransmitString("TWINT: ");
	USART_TransmitHexChar(status);
	USART_Transmit(' ');
	
	if(I2C_mode == I2C_MODE_SLAVE) {
		if(!I2C_slaveIsReceivingData) {
			if(status == 0x60 || status == 0x70) { // own addr received and ack sent
				// start receiving data
				I2C_slaveReceivedDataLength = 0;
				I2C_slaveIsReceivingData = 1;
				
				I2C_lastReceiveWasGeneral = status == 0x70;
				USART_TransmitLine("1");
				TWCR = (1<<TWINT) | (1<<TWEA) | (1<<TWEN);
			} else {
				USART_TransmitLine("2");
				TWCR = (1<<TWINT) | (1<<TWEA) | (1<<TWEN);
			}
		} else { // slave is receiving data
			if(status == 0x80 || status == 0x90) { // currently addressed and received data, ack sent
				
				I2C_slaveReceivedData[I2C_slaveReceivedDataLength++] = TWDR;
				USART_TransmitLine("3");
				
				TWCR = (1<<TWINT) | (1<<TWEA) | (1<<TWEN);
			} else if(status == 0xA0) { // stop signal received
				I2C_slaveHasReceivedData = 1;
				I2C_slaveIsReceivingData = 0;
				USART_TransmitLine("4");
				
				TWCR = (1<<TWINT) | (1<<TWEN); // switch to not addressed mode but stop acknowledging anything until data has been read
			} else {
				
				USART_TransmitLine("5");
				TWCR = (1<<TWINT) | (1<<TWEA) | (1<<TWEN);
			}
		}
	} else { // default to master-mode
		// TODO: master async send & receive
		USART_TransmitLine("6");
		TWCR = (1<<TWINT) | (1<<TWEN);
	}
}

uint8_t I2C_SlaveHasReceivedData() {
	return I2C_slaveHasReceivedData ? I2C_slaveReceivedDataLength : 0;
}

uint8_t* I2C_SlaveGetReceivedData() {
	I2C_slaveHasReceivedData = 0;
	TWCR = (1<<TWEA) | (1<<TWEN) | (1<<TWIE);
	return (uint8_t*) I2C_slaveReceivedData;
}

uint8_t IC2_SlaveReceiveSync() {
	while(I2C_slaveHasReceivedData) ;
	
	I2C_slaveReceivedDataLength = 0;
	I2C_slaveIsReceivingData = 1;
	
	USART_TransmitString("Sync Rec with addr ");
	USART_TransmitBinChar(TWAR);
	USART_NewLine();
	
	
	TWCR = (1<<TWEA) | (1<<TWEN); // enable twi and send ack
	
	// wait for TWINT
	while(!(TWCR & (1<<TWINT))) ;
	
	if((TWSR & 0xF8) != 0x60) {
		USART_TransmitLine("error receiving start cond");
		return 0;
	}
	
	USART_TransmitLine("Receiving data now...");
	
	uint8_t status, data;
	while(1) {
		// wait for TWINT
		while(!(TWCR & (1<<TWINT))) ;
		
		// check status
		status = TWSR & 0xF8;
		
		if(status == 0xA0) { // STOP received
			USART_TransmitLine("Received stop...");
			break;
		} else if(status == 0x80 || status == 0x90) {
			data = TWDR;
			USART_TransmitString("Received bit ");
			USART_TransmitDecimal8UB(data);
			USART_NewLine();
			I2C_slaveReceivedData[I2C_slaveReceivedDataLength++] = data;
			TWCR = (1<<TWINT) | (1<<TWEN) | (1<<TWEA);
		}
	}
	
	TWCR = (1<<TWEA) | (1<<TWEN) | (1<<TWIE);
	I2C_slaveHasReceivedData = 1;
	I2C_slaveIsReceivingData = 0;
	return I2C_slaveReceivedDataLength;
}

void I2C_InitSlave(uint8_t slaveAddress, uint8_t respondToGeneral) {
	I2C_mode = I2C_MODE_SLAVE;
	I2C_Init();
	
	TWAR = (slaveAddress << 1) | (respondToGeneral ? 1 : 0);
	TWCR = (1<<TWEN) | (1<<TWEA) | (1<<TWIE);
}

#endif // __MYI2C_
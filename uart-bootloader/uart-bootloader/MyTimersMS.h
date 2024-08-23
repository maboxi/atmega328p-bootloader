/*
 * MyTimersMS.h
 *
 * Created: 21.04.2021 19:37:40
 *  Author: maxip
 */ 

#ifndef MYTIMERS_H_
#define MYTIMERS_H_

#include <avr/common.h>
#include <avr/interrupt.h>
#include <stdlib.h>

void initTimersMS();

struct timerMS_t {
	uint8_t running;
	uint32_t duration, counter;
	void (*callback)();
};
typedef uint8_t timerMS;

volatile uint8_t timerCounter = 0;
volatile struct timerMS_t timerArr[5];

timerMS declareTimerMS(uint32_t durationMS, void (*callback)());
void startTimerMS(timerMS t);
void cancelTimerMS(timerMS t);
void setDurationMS(timerMS t, uint32_t newduration);

// define this to be able to use own ISR
#ifndef MYTIMERS_NO_ISR
ISR(TIMER1_OVF_vect) {
	TCNT1 = 0xFFFF - (250-1);
	
	for(uint8_t i = 0; i < timerCounter; i++) {
		if(timerArr[i].running) {
			timerArr[i].counter++;
			if(timerArr[i].counter >= timerArr[i].duration) { // one timercycle done -> call callback and reset
				timerArr[i].counter = 0;
				(timerArr[i].callback)();
			}
		}
	}
	
}

#endif // MYTIMERS_NO_ISR

timerMS declareTimerMS(uint32_t durationMS, void (*callback)()) {
	if(timerCounter >= 5)
		return 0;
		
	timerArr[timerCounter].duration = durationMS;
	timerArr[timerCounter].callback = callback;
	timerArr[timerCounter].running = 0;
	
	return timerCounter++;
}

void startTimerMS(timerMS t) {
	unsigned char sreg_old = SREG;
	cli();
	timerArr[t].counter = 0UL;
	timerArr[t].running = 1;
	SREG = sreg_old;
}

void cancelTimerMS(timerMS t) {
	unsigned char sreg_old = SREG;
	cli();
	timerArr[t].running = 0;
	SREG = sreg_old;
}

// dont forget to enable interrupts
void initTimersMS() {
	
	for(int i = 0; i < 5; i++)
		timerArr[i].running = 0;
	
	// set timer mode to normal
	TCCR1A &= ~((1<<WGM11) | (1<<WGM10));
	TCCR1B &= ~((1<<WGM12) | (1<<WGM13));
	
	// disable compare-register
	TCCR1A &= ~((1<<COM1A1) | (1<<COM1A0) | (1<<COM1B1) | (1<<COM1B0));
	
	// set prescaler to 64 -> 250 ticks = 1 ms
	TCCR1B &= ~(1<<CS12);
	TCCR1B |= (1<<CS11) | (1<<CS10);
	TCNT1 = 0xFFFF - (250-1);
	
	// enable interrupt
	TIMSK1 |= (1<<TOIE1);
}

void setDurationMS(timerMS t, uint32_t newduration) {
	timerArr[t].duration = newduration;
}

#endif /* MYTIMERS_H_ */
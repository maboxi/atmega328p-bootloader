/*
 * led-slowblink.c
 *
 * Created: 28.08.2024 01:11:07
 * Author : maxip
 */ 

// Red, Green, Blue Test LEDs
#define LED_RED	1
#define LED_GREEN 2
#define LED_BLUE 4

#define F_CPU 16000000
#include <avr/io.h>
#include <util/delay.h>

void set_rgb_leds(uint8_t flag) {
	uint8_t temp = PORTD;
	temp &= ~(1<<PORTD5) & ~(1<<PORTD6) & ~(1<<PORTD7);
	temp |= (flag & 0b111) << 5;
	PORTD = temp;
}

int main(void)
{
	DDRD |= (1<<DDD5) | (1<<DDD6) | (1<<DDD7);
	PORTD &= ~(1<<PORTD5) & ~(1<<PORTD6) & ~(1<<PORTD7);

	while (1)
	{	
		set_rgb_leds(LED_RED);
		_delay_ms(5000);
		
		set_rgb_leds(LED_RED | LED_GREEN);
		_delay_ms(1000);
		
		set_rgb_leds(LED_BLUE);
		_delay_ms(5000);
		
		set_rgb_leds(LED_GREEN);
		_delay_ms(3000);
	}
}

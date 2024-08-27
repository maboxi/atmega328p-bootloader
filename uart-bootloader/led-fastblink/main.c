/*
 * led-fastblink.c
 *
 * Created: 28.08.2024 01:08:45
 * Author : maxip
 */

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
	uint8_t counter = 0;

	DDRB |= (1<<DDB5);
	DDRD |= (1<<DDD5) | (1<<DDD6) | (1<<DDD7);
	PORTD &= ~(1<<PORTD5) & ~(1<<PORTD6) & ~(1<<PORTD7);

	while (1)
	{
		if(PORTB & (1<<PORTB5)) {
			PORTB = PORTB & (~(1<<PORTB5));
			counter++;
			_delay_ms(100);
			} else {
			PORTB = PORTB | (1<<PORTB5);
			_delay_ms(20);
		}
		
		set_rgb_leds(counter);
	}
}


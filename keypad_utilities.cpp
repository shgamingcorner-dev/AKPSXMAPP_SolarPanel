/*
 * File:   keypad_utilities.cpp
 *
 * Interrupt-driven keypad: the 74C922 encoder pulls DA high the instant a
 * key is pressed (already hardware-debounced by the chip), so instead of
 * blocking a thread on `while (Keypad_DA == 0);`, an InterruptIn callback
 * fires on that rising edge, reads the 4-bit code, and stashes the
 * translated ASCII key for the caller to pick up whenever it's convenient.
 * The callback stays intentionally tiny (no printf, no blocking calls) --
 * same rule this project already applies to the DHT11 read while inside a
 * CriticalSectionLock.
 */

#undef __ARM_FP

#include "mbed.h"
#include "keypad.h"

//declare the Keypad Data input pins as an object of BusIn
//You can have up to 16 pins in a Bus.
//The order of pins in the constructor is the reverse order of the pins in the byte order.
//If you have BusIn(a,b,c,d,e,f,g,h)
//then the order of bits in the byte would be hgfedcba
//with a being bit 0, b being bit 1, c being bit 2 and so on.
static BusIn Keypad_Data(PB_8, PB_9, PB_10, PB_11);

static InterruptIn Keypad_DA(PB_13); //74C922 DA output

static const unsigned char lookupTable[] = {'1', '2','3','F','4','5','6','E','7','8','9','D','A','0','B','C'};

volatile bool key_pending = false;
volatile char last_key = 0;

static void on_key_ready(void)
{
    unsigned char keycode = Keypad_Data & Keypad_Data.mask();
    last_key = lookupTable[keycode];
    key_pending = true;
}

void keypad_init(void)
{
    Keypad_Data.mode(PullNone);
    Keypad_DA.rise(&on_key_ready);
}

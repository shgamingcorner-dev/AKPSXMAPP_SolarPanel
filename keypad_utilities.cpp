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

// The four keypad data lines are deliberately NOT a BusIn: BusIn::read()
// takes a PlatformMutex internally, and taking a mutex inside an ISR is
// illegal -- mbed traps it at runtime with
//   "Mutex: 0x........, Not allowed in ISR context"
// and halts the board. DigitalIn::read() is a bare gpio_read() that the
// driver documents as "Thread safe / atomic HAL call", so it is safe to
// call from the DA interrupt handler below.
//
// Bit order matches the BusIn convention this replaces -- BusIn(a,b,c,d)
// maps a to bit 0, b to bit 1, and so on.
static DigitalIn Keypad_D0(PB_8);
static DigitalIn Keypad_D1(PB_9);
static DigitalIn Keypad_D2(PB_10);
static DigitalIn Keypad_D3(PB_11);

static InterruptIn Keypad_DA(PB_13); //74C922 DA output

static const unsigned char lookupTable[] = {'1', '2','3','F','4','5','6','E','7','8','9','D','A','0','B','C'};

volatile bool key_pending = false;
volatile char last_key = 0;

static void on_key_ready(void)
{
    unsigned char keycode = (unsigned char)((Keypad_D0.read()     )
                                          | (Keypad_D1.read() << 1)
                                          | (Keypad_D2.read() << 2)
                                          | (Keypad_D3.read() << 3));
    last_key = lookupTable[keycode & 0x0F]; // mask keeps the index inside the 16-entry table
    key_pending = true;
}

void keypad_init(void)
{
    Keypad_D0.mode(PullNone);
    Keypad_D1.mode(PullNone);
    Keypad_D2.mode(PullNone);
    Keypad_D3.mode(PullNone);
    Keypad_DA.rise(&on_key_ready);
}

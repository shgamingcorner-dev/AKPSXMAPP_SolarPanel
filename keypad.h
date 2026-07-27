// file : keypad.h

// Call once at startup -- attaches the interrupt handler for the keypad's
// 74C922 DA (Data Available) line.
extern void keypad_init(void);

// Shared state set by the keypad's ISR and consumed by the caller (main
// thread). key_pending is cleared by the reader once it has consumed
// last_key -- same producer/consumer shape as g_latest_rfid/rfid_mutex in
// main.cpp, except a single volatile bool/char is safe here without a mutex
// since it's only ever written by the ISR and read/cleared by one thread.
extern volatile bool key_pending;
extern volatile char last_key;

/*
 * File:   lcd utilities.cpp
 *
 */
#undef __ARM_FP

#include "mbed.h"
#include "config.h"
#include "lcd.h"	// Include file is located in the project directory

#define DISPLAY_LCD_MASK 0x00000F00 //PORT A1: PA_15 : PA_8, 4-bit mode, using PA_11 : PA_8
#define DISPLAY_LCD_RESET 0x00000000

PortOut lcdPort(PortA, DISPLAY_LCD_MASK);
//DigitalOut LCD_RS(PA_14);   //  Register Select on LC
DigitalOut LCD_RS(PC_7);   //  Register Select on LC
DigitalOut LCD_EN(PA_12);   //  Enable on LCD controller
//DigitalOut LCD_WR(PA_13);   //  Write on LCD controller
DigitalOut LCD_WR(PC_6);   //  Write on LCD controller

void lcd_strobe(void);

//--- Function for writing a command byte to the LCD in 4 bit mode -------------

void lcd_write_cmd(unsigned char cmd)
{
    unsigned char temp2;
    int tempLCDPort = 0;

    LCD_RS = 0;					// Select LCD for command mode
    LCD_WR = 0;					// Select LCD for write mode
    wait_us(40);				// 40us delay for LCD to settle (t_AS = 40ns min, 40us safe)
    temp2 = cmd;
    temp2 = temp2 >> 4;			// Output upper 4 bits
    temp2 = temp2 & 0x0F;
    tempLCDPort = (int) temp2;
    tempLCDPort =  tempLCDPort << 8;
    tempLCDPort = tempLCDPort & 0x00000F00;
    lcdPort = tempLCDPort;

    lcd_strobe();				// Strobe upper nibble
    wait_us(100);				// Wait between nibbles (enable cycle > 500ns)

    temp2 = cmd;				// Re-initialise temp2
    temp2 = temp2 & 0x0F;		// Mask out upper 4 bits - get lower nibble

    tempLCDPort = (int) temp2;
    tempLCDPort =  tempLCDPort << 8;
    tempLCDPort = tempLCDPort & 0x00000F00;
    lcdPort = tempLCDPort;

    lcd_strobe();				// Strobe lower nibble

    // Command-specific delay
    if (cmd == 0x01 || cmd == 0x02) {
        wait_us(2000);			// Clear display/return home: 1.52ms min
    } else {
        wait_us(50);			// Other commands: ~37us typical
    }
}

//---- Function to write a character data to the LCD ---------------------------

void lcd_write_data(char data)
{
  	char temp1;
    int tempLCDPort = 0;

    LCD_RS = 1;					// Select LCD for data mode
    LCD_WR = 0;					// Select LCD for write mode
    wait_us(40);				// 40us delay for LCD to settle down

    temp1 = data;
    temp1 = temp1 >> 4;
    temp1 = temp1 & 0x0F;

    tempLCDPort = (int) temp1;
    tempLCDPort =  tempLCDPort << 8;
    tempLCDPort = tempLCDPort & 0x00000F00;
    lcdPort = tempLCDPort;

    lcd_strobe();				// Strobe upper nibble
    wait_us(100);				// Wait between nibbles

    temp1 = data;
    temp1 = temp1 & 0x0F;		// Get lower nibble
    tempLCDPort = (int) temp1;
    tempLCDPort =  tempLCDPort << 8;
    tempLCDPort = tempLCDPort & 0x00000F00;
    lcdPort = tempLCDPort;

    lcd_strobe();				// Strobe lower nibble
    wait_us(50);				// Data write: ~37us typical
}


//-- Function to generate the strobe signal for command and character----------

void lcd_strobe(void)			// Generate the E pulse
{
    LCD_EN = 1;					// E = 1 (enable high)
    wait_us(LCD_STROBE_US);					// 1-5us pulse width (EN pulse width min 230ns, enable cycle 500ns)
    LCD_EN = 0;					// E = 0 (enable low - data latched on falling edge)
    wait_us(LCD_STROBE_US);					// 1-5us hold
}


//---- Function to initialise LCD module ----------------------------------------
void lcd_init(void)
{
    lcdPort = DISPLAY_LCD_RESET;				// lcd port (portA, PA8 - PA15) is connected to LCD data pin
    LCD_EN = 0;
    LCD_RS = 0;					// Select LCD for command mode
    LCD_WR = 0;					// Select LCD for write mode
   
    // Delay a total of 1 s for LCD module to
	// finish its own internal initialisation

    thread_sleep_for(1000);

    /* The data sheets warn that the LCD module may fail to initialise properly when
       power is first applied. This is particularly likely if the Vdd
       supply does not rise to its correct operating voltage quickly enough.

       It is recommended that after power is applied, a command sequence of
       3 bytes of 30h be sent to the module. This will ensure that the module is in
       8-bit mode and is properly initialised. Following this, the LCD module can be
       switched to 4-bit mode.
    */

    lcd_write_cmd(0x33);
    lcd_write_cmd(0x32);
      
    lcd_write_cmd(0x28);		// 001010xx   Function Set instruction
    							// DL=0 :4-bit interface,N=1 :2 lines,F=0 :5x7 dots
   
    lcd_write_cmd(0x0E);		// 00001110   Display On/Off Control instruction
    							// D=1 :Display on,C=1 :Cursor on,B=0 :Cursor Blink on
   
    lcd_write_cmd(0x06);		// 00000110   Entry Mode Set instruction
    							// I/D=1 :Increment Cursor position
   								// S=0 : No display shift
   
    lcd_write_cmd(0x01);		// 00000001 Clear Display instruction
 
    thread_sleep_for(20);			// 20 ms delay

}

void lcd_Clear(void)
{
    lcd_write_cmd(0x01);		// 00000001 Clear Display instruction
 
    thread_sleep_for(20);			// 20 ms delay

}
/*
* MFRC522.cpp - Library to use ARDUINO RFID MODULE KIT 13.56 MHZ WITH TAGS SPI W AND R BY COOQROBOT.
* NOTE: Please also check the comments in MFRC522.h - they provide useful hints and background information.
* Released into the public domain.
*/
#undef __ARM_FP
#include "mbed.h"
#include "MFRC522.h"
using namespace std::chrono;


/////////////////////////////////////////////////////////////////////////////////////
// Functions for setting up the Arduino
/////////////////////////////////////////////////////////////////////////////////////
/**
 * Constructor.
 */
MFRC522::MFRC522(): MFRC522(PA_4, NC) { // SPI1 SS is defined in NUCLEO F103RB as PA_4, UINT8_MAX means there is no connection from NEUCLO F103RB board to MFRC522's reset and power down input, in Mbed, it is defined as: NC
} // End constructor

/**
 * Constructor.
 * Prepares the output pins.
 */
MFRC522::MFRC522(PinName resetPowerDownPin	///< NUCLEO F103RB pin connected to MFRC522's reset and power down input (Pin 6, NRSTPD, active low). If there is no connection from the CPU to NRSTPD, set this to UINT8_MAX. In this case, only soft reset will be used in PCD_Init().
				): MFRC522(PA_4, resetPowerDownPin) { //
} // End constructor

/**
 * Constructor.
 * Prepares the output pins.
 */
MFRC522::MFRC522(PinName chipSelectPin,		///<  pin connected to MFRC522's SPI slave select input (Pin 24, NSS, active low)
	PinName resetPowerDownPin	///<  pin connected to MFRC522's reset and power down input (Pin 6, NRSTPD, active low). If there is no connection from the CPU to NRSTPD, set this to UINT8_MAX. In this case, only soft reset will be used in PCD_Init().
				) :
	_chipSelectPin(chipSelectPin),
	_resetPowerDownPin(resetPowerDownPin),
	_spi(PB_5, PB_4, PB_3),          // mosi, miso, sclk
	_csPin(chipSelectPin)
{
	_spi.frequency(MFRC522_SPICLOCK); // 4MHz
	_spi.format(8, 0);               // MSBFIRST, SPI_MODE0
	_csPin = 1;                      // CS high (inactive)
}

/////////////////////////////////////////////////////////////////////////////////////
// Basic interface functions for communicating with the MFRC522
/////////////////////////////////////////////////////////////////////////////////////

/**
 * Writes a byte to the specified register in the MFRC522 chip.
 * The interface is described in the datasheet section 8.1.2.
 */
void MFRC522::PCD_WriteRegister(	PCD_Register reg,	///< The register to write to. One of the PCD_Register enums.
									byte value			///< The value to write.
								) {
	_csPin = 0;	// Select slave

	_spi.write(reg);						// MSB == 0 is for writing. LSB is not used in address. Datasheet section 8.1.2.3.
	_spi.write(value);

	_csPin = 1;		// Release slave again
} // End PCD_WriteRegister()

/**
 * Writes a number of bytes to the specified register in the MFRC522 chip.
 * The interface is described in the datasheet section 8.1.2.
 */
void MFRC522::PCD_WriteRegister(	PCD_Register reg,	///< The register to write to. One of the PCD_Register enums.
									byte count,			///< The number of bytes to write to the register
									byte *values		///< The values to write. Byte array.
								) {
	_csPin = 0;	// Select slave

	_spi.write(reg);		// MSB == 0 is for writing. LSB is not used in address. Datasheet section 8.1.2.3.
	for (byte index = 0; index < count; index++) {
		_spi.write(values[index]);
	}
	_csPin = 1;		// Release slave again
} // End PCD_WriteRegister()

/**
 * Reads a byte from the specified register in the MFRC522 chip.
 * The interface is described in the datasheet section 8.1.2.
 */
byte MFRC522::PCD_ReadRegister(	PCD_Register reg	///< The register to read from. One of the PCD_Register enums.
								) {
	byte value;
	_csPin = 0;	// Select slave

	_spi.write(0x80 | reg);					// MSB == 1 is for reading. LSB is not used in address. Datasheet section 8.1.2.3.
	value = _spi.write(0);					// Read the value back. Send 0 to stop reading.

	_csPin = 1;		// Release slave again
	return value;
} // End PCD_ReadRegister()

/**
 * Reads a number of bytes from the specified register in the MFRC522 chip.
 * The interface is described in the datasheet section 8.1.2.
 */
void MFRC522::PCD_ReadRegister(	PCD_Register reg,	///< The register to read from. One of the PCD_Register enums.
								byte count,			///< The number of bytes to read
								byte *values,		///< Byte array to store the values in.
								byte rxAlign		///< Only bit positions rxAlign..7 in values[0] are updated.
								) {
	if (count == 0) {
		return;
	}
	//printf("Reading "); 	printf("%d",count); printf(" bytes from register.");
	byte address = 0x80 | reg;				// MSB == 1 is for reading. LSB is not used in address. Datasheet section 8.1.2.3.
	byte index = 0;							// Index in values array.

	_csPin = 0;	// Select slave

	count--;								// One read is performed outside of the loop
	_spi.write(address);					// Tell MFRC522 which address we want to read
	if (rxAlign) {		// Only update bit positions rxAlign..7 in values[0]
		// Create bit mask for bit positions rxAlign..7
		byte mask = (0xFF << rxAlign) & 0xFF;
		// Read value and tell that we want to read the same address again.
		byte value = _spi.write(address);
		// Apply mask to both current value of values[0] and the new data in value.
		values[0] = (values[0] & ~mask) | (value & mask);
		index++;
	}
	while (index < count) {
		values[index] = _spi.write(address);	// Read value and tell that we want to read the same address again.
		index++;
	}
	values[index] = _spi.write(0);			// Read the final byte. Send 0 to stop reading.
	_csPin = 1;		// Release slave again
} // End PCD_ReadRegister()

/**
 * Sets the bits given in mask in register reg.
 */
void MFRC522::PCD_SetRegisterBitMask(	PCD_Register reg,	///< The register to update. One of the PCD_Register enums.
										byte mask			///< The bits to set.
									) {
	byte tmp;
	tmp = PCD_ReadRegister(reg);
	PCD_WriteRegister(reg, tmp | mask);			// set bit mask
} // End PCD_SetRegisterBitMask()

/**
 * Clears the bits given in mask from register reg.
 */
void MFRC522::PCD_ClearRegisterBitMask(	PCD_Register reg,	///< The register to update. One of the PCD_Register enums.
										byte mask			///< The bits to clear.
									  ) {
	byte tmp;
	tmp = PCD_ReadRegister(reg);
	PCD_WriteRegister(reg, tmp & (~mask));		// clear bit mask
} // End PCD_ClearRegisterBitMask()


/**
 * Use the CRC coprocessor in the MFRC522 to calculate a CRC_A.
 *
 * @return STATUS_OK on success, STATUS_??? otherwise.
 */
MFRC522::StatusCode MFRC522::PCD_CalculateCRC(	byte *data,		///< In: Pointer to the data to transfer to the FIFO for CRC calculation.
												byte length,	///< In: The number of bytes to transfer.
												byte *result	///< Out: Pointer to result buffer. Result is written to result[0..1], low byte first.
									 ) {
	PCD_WriteRegister(CommandReg, PCD_Idle);		// Stop any active command.
	PCD_WriteRegister(DivIrqReg, 0x04);				// Clear the CRCIRq interrupt request bit
	PCD_WriteRegister(FIFOLevelReg, 0x80);			// FlushBuffer = 1, FIFO initialization
	PCD_WriteRegister(FIFODataReg, length, data);	// Write data to the FIFO
	PCD_WriteRegister(CommandReg, PCD_CalcCRC);		// Start the calculation

	// Wait for the CRC calculation to complete. Check for the register to
	// indicate that the CRC calculation is complete in a loop. If the
	// calculation is not indicated as complete in ~90ms, then time out
	// the operation.
	const uint32_t deadline = 89;

	Timer ms_timer;
	ms_timer.start();

	do {
		// DivIrqReg[7..0] bits are: Set2 reserved reserved MfinActIRq reserved CRCIRq reserved reserved
		byte n = PCD_ReadRegister(DivIrqReg);
		if (n & 0x04) {									// CRCIRq bit set - calculation done
			PCD_WriteRegister(CommandReg, PCD_Idle);	// Stop calculating CRC for new content in the FIFO.
			// Transfer the result from the registers to the result buffer
			result[0] = PCD_ReadRegister(CRCResultRegL);
			result[1] = PCD_ReadRegister(CRCResultRegH);
			return STATUS_OK;
		}
		ThisThread::yield();  // Yield to allow other threads to run
	}
	while ( duration_cast<milliseconds>(ms_timer.elapsed_time()).count() < deadline );

	// 89ms passed and nothing happened. Communication with the MFRC522 might be down.
	ms_timer.stop();
	return STATUS_TIMEOUT;
} // End PCD_CalculateCRC()


/////////////////////////////////////////////////////////////////////////////////////
// Functions for manipulating the MFRC522
/////////////////////////////////////////////////////////////////////////////////////

/**
 * Initializes the MFRC522 chip.
 */
void MFRC522::PCD_Init() {
	bool hardReset = false;

	// Set the chipSelectPin as digital output, do not select the slave yet
	_csPin = 1;

	// If a valid pin number has been set, pull device out of power down / reset state.
	if (_resetPowerDownPin != UNUSED_PIN) {
		// First set the resetPowerDownPin as digital input, to check the MFRC522 power down mode.
		DigitalInOut resetPin(_resetPowerDownPin);
		resetPin.input();

		if (resetPin == 0) {	// The MFRC522 chip is in power down mode.
			resetPin.output();		// Now set the resetPowerDownPin as digital output.
			resetPin = 0;		// Make sure we have a clean LOW state.
			wait_us(2);				// 8.8.1 Reset timing requirements says about 100ns. Let us be generous: 2μsl
			resetPin = 1; 	// Exit power down mode. This triggers a hard reset.
			// Section 8.8.2 in the datasheet says the oscillator start-up time is the start up time of the crystal + 37,74μs. Let us be generous: 50ms.
			thread_sleep_for(50);
			hardReset = true;
		}
	}

	if (!hardReset) { // Perform a soft reset if we haven't triggered a hard reset above.
		PCD_Reset();
	}

	// Reset baud rates
	PCD_WriteRegister(TxModeReg, 0x00);
	PCD_WriteRegister(RxModeReg, 0x00);
	// Reset ModWidthReg
	PCD_WriteRegister(ModWidthReg, 0x26);

	// When communicating with a PICC we need a timeout if something goes wrong.
	// f_timer = 13.56 MHz / (2*TPreScaler+1) where TPreScaler = [TPrescaler_Hi:TPrescaler_Lo].
	// TPrescaler_Hi are the four low bits in TModeReg. TPrescaler_Lo is TPrescalerReg.
	PCD_WriteRegister(TModeReg, 0x80);			// TAuto=1; timer starts automatically at the end of the transmission in all communication modes at all speeds
	PCD_WriteRegister(TPrescalerReg, 0xA9);		// TPreScaler = TModeReg[3..0]:TPrescalerReg, ie 0x0A9 = 169 => f_timer=40kHz, ie a timer period of 25μs.
	PCD_WriteRegister(TReloadRegH, 0x03);		// Reload timer with 0x3E8 = 1000, ie 25ms before timeout.
	PCD_WriteRegister(TReloadRegL, 0xE8);

	PCD_WriteRegister(TxASKReg, 0x40);		// Default 0x00. Force a 100 % ASK modulation independent of the ModGsPReg register setting
	PCD_WriteRegister(ModeReg, 0x3D);		// Default 0x3F. Set the preset value for the CRC coprocessor for the CalcCRC command to 0x6363 (ISO 14443-3 part 6.2.4)
	PCD_AntennaOn();						// Enable the antenna driver pins TX1 and TX2 (they were disabled by the reset)
}


/**
 * Initializes the MFRC522 chip.
 * Overload that accepts chipSelectPin and resetPowerDownPin.
 */
void MFRC522::PCD_Init(PinName chipSelectPin,		///<  pin connected to MFRC522's SPI slave select input (Pin 24, NSS, active low)
	PinName resetPowerDownPin	///<  pin connected to MFRC522's reset and power down input (Pin 6, NRSTPD, active low)
						) {
	_chipSelectPin = chipSelectPin;
	_resetPowerDownPin = resetPowerDownPin;
	// Note: SPI pins (MOSI/MISO/SCK) are fixed to PB_5/PB_4/PB_3.
	// Recreating SPI object would require dynamic allocation. Use new CS pin.
	_csPin = DigitalOut(chipSelectPin);
	PCD_Init();
} // End PCD_Init()


/**
 * Performs a soft reset on the MFRC522 chip and waits for it to be ready again.
 */
void MFRC522::PCD_Reset() {
	PCD_WriteRegister(CommandReg, PCD_SoftReset);	// Issue the SoftReset command.
	// The datasheet does not mention how long the SoftRest command takes to complete.
	// But the MFRC522 might have been in soft power-down mode (triggered by bit 4 of CommandReg)
	// in which case the reset sequence takes longer.
	// The typical duration according to the datasheet is 100 microseconds.
	wait_us(2000);
	// Wait for the PowerDown bit in CommandReg to be cleared (max 100ms).
	unsigned int deadline = 100;
	Timer ms_timer;
	ms_timer.start();
	while ( (PCD_ReadRegister(CommandReg) & (1 << 4)) != 0 ) {
		if ( duration_cast<milliseconds>(ms_timer.elapsed_time()).count() >= deadline ) {
			return;
		}
		wait_us(50);
	}
	ms_timer.stop();
}
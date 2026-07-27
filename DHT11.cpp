//DHT11.cpp
 
#include "DHT11.h"

char MSG_ERROR_TIMEOUT[] = "Error 253 Reading from DHT11 timed out.";
char MSG_ERROR_CHECKSUM[] = "Error 254 Checksum mismatch while reading from DHT11.";
char MSF_ERROR_UNKNOW[] = "Error Unknown.";

/**
 * Constructor for the DHT11 class.
 * Initializes the pin to be used for communication and sets it to output mode.
 *
 * @param pin: Pin Name on the STM32 NUCLEO-F103RB board to which the DHT11 sensor is connected.
 */
DHT11::DHT11(PinName pin) : _pin(pin)
{
    t.start();  //start timer
}

/**
 * Sets the delay between consecutive sensor readings.
 * If this method is not called, a default delay of 500 milliseconds is used.
 *
 * @param delay: Delay duration in milliseconds between sensor readings.
 */
void DHT11::setDelay(unsigned long delay)
{
    _delayMS = delay;
}

/**
 * Reads raw data from the DHT11 sensor.
 * This method handles the direct communication with the DHT11 sensor and retrieves the raw data.
 * It's used internally by the readTemperature, readHumidity, and readTemperatureHumidity methods.
 *
 * @param data: An array of bytes where the raw sensor data will be stored.
 *              The array must be at least 5 bytes long, as the DHT11 sensor returns 5 bytes of data.
 * @return: Returns 0 if the data is read successfully and the checksum matches.
 *          Returns DHT11::ERROR_TIMEOUT if the sensor does not respond or communication times out.
 *          Returns DHT11::ERROR_CHECKSUM if the data is read but the checksum does not match.
 */
// Bit-level phases are on the order of 30-70us per the DHT11 spec, so a
// bounded busy-wait counting 1us steps gives ample margin without depending
// on the millisecond Timer (which would need interrupts left enabled).
static bool wait_for_level(DigitalInOut &pin, int level, int timeout_us)
{
    for (int waited = 0; waited < timeout_us; waited++)
    {
        if (pin == level) return true;
        wait_us(1);
    }
    return false;
}

int DHT11::readRawData(byte data[5])
{
    //declare the pin as Digital In/Out pin
    DigitalInOut pin_DHT11(_pin);
    pin_DHT11.output(); //set as output pin
    pin_DHT11 = 1;      //intial state: set pin as HIGH
    thread_sleep_for(_delayMS);

    pin_DHT11 = 0;      //start signal to the DHT11
    //thread_sleep_for(18);
    thread_sleep_for(20); //keep the LOW signal a bit longer than 18ms
    pin_DHT11 = 1;        //set the pin to HIGH
    //wait_us(40);
    wait_us(30);          //wait for 30us, max is 40us per the spec

    pin_DHT11.input();     //set the pin as input and wait for DHT11 pulling down the signal
    pin_DHT11.mode(PullUp); // bias the line HIGH via the MCU's internal pull-up while
                            // nothing else is driving it -- without this the pin was
                            // left floating (PullNone) the whole time we wait for the
                            // sensor's ACK, so a missing/weak external pull-up resistor
                            // meant the line had no defined level to transition from

    // The whole time-sensitive exchange (from the sensor's ack pulse through
    // the 40 data bits) runs with the scheduler locked so networkThread's
    // UART activity can't preempt us mid-bit and desync the timing -- this
    // was silently corrupting reads once the network thread became busy.
    int result = DHT11::ERROR_TIMEOUT;
    {
        CriticalSectionLock lock;

        t.reset();
        int timeout_start = duration_cast<milliseconds> (t.elapsed_time()).count();
        bool acked = false;
        while (pin_DHT11 == 1) //check the pin signal level with timer out: 1000ms
        {
            if ( ( duration_cast<milliseconds> (t.elapsed_time()).count() - timeout_start ) > TIMEOUT_DURATION)
            {
                acked = false;
                break;
            }
            acked = true;
        }

        if (acked && pin_DHT11 == 0)
        {
            wait_us(80);
            if (pin_DHT11 == 1)
            {
                wait_us(80);
                bool bit_timeout = false;

                for (int i = 0; i < 5 && !bit_timeout; i++)
                {
                    byte value = 0;

                    for (int bit = 0; bit < 8 && !bit_timeout; bit++)
                    {
                        if (!wait_for_level(pin_DHT11, 1, 200)) { bit_timeout = true; break; }

                        wait_us(30);

                        if (pin_DHT11 == 1)
                        {
                            value |= (1 << (7 - bit));
                        }

                        if (!wait_for_level(pin_DHT11, 0, 200)) { bit_timeout = true; break; }
                    }

                    data[i] = value;
                }

                if (bit_timeout)
                {
                    result = DHT11::ERROR_TIMEOUT;
                }
                else if (data[4] == ((data[0] + data[1] + data[2] + data[3]) & 0xFF))
                {
                    result = 0; // Success
                }
                else
                {
                    result = DHT11::ERROR_CHECKSUM;
                }
            }
        }
    }

    if (result == DHT11::ERROR_TIMEOUT) printf("DHT11 error: timeout\n");
    else if (result == DHT11::ERROR_CHECKSUM) printf("DHT11 error: checksum mismatch\n");
    return result;
}


/**
 * Reads and returns the temperature from the DHT11 sensor.
 * Utilizes the readRawData method to retrieve raw data from the sensor and then extracts
 * the temperature from the data array.
 *
 * @return: Temperature value in Celsius. Returns DHT11::ERROR_TIMEOUT if reading times out,
 *          or DHT11::ERROR_CHECKSUM if checksum validation fails.
 */
int DHT11::readTemperature()
{
    byte data[5];
    int error = readRawData(data);

    if (error != 0)
    {
        return error;
    }

    return data[2];
}

/**
 * Reads and returns the humidity from the DHT11 sensor.
 * Utilizes the readRawData method to retrieve raw data from the sensor and then extracts
 * the humidity from the data array.
 *
 * @return: Humidity value in percentage. Returns DHT11::ERROR_TIMEOUT if reading times out,
 *          or DHT11::ERROR_CHECKSUM if checksum validation fails.
 */
int DHT11::readHumidity()
{
    byte data[5];
    int error = readRawData(data);

    if (error != 0)
    {
        return error;
    }

    return data[0];
}

/**
 * Reads and returns the temperature and humidity from the DHT11 sensor.
 * Utilizes the readRawData method to retrieve raw data from the sensor and then extracts
 * both temperature and humidity from the data array.
 *
 * @param temperature: Reference to a variable where the temperature value will be stored.
 * @param humidity: Reference to a variable where the humidity value will be stored.
 * @return: An integer representing the status of the read operation.
 *          Returns 0 if the reading is successful, DHT11::ERROR_TIMEOUT if a timeout occurs,
 *          or DHT11::ERROR_CHECKSUM if a checksum error occurs.
 */
int DHT11::readTemperatureHumidity(int &temperature, int &humidity)
{
    byte data[5];
    int error = readRawData(data);

    if (error != 0)
    {
        return error;
    }

    humidity = data[0];
    temperature = data[2];

    return 0; // Indicate success
}

/**
 * Returns a human-readable error message based on the provided error code.
 * This method facilitates easier debugging and user feedback by translating
 * numeric error codes into descriptive strings.
 *
 * @param errorCode The error code for which the description is required.
 * @return A descriptive string explaining the error.
 */
char* DHT11::getErrorString(int errorCode)
{
    switch (errorCode)
    {
        case DHT11::ERROR_TIMEOUT:
                return MSG_ERROR_TIMEOUT;
                break;
        case DHT11::ERROR_CHECKSUM:
                return MSG_ERROR_CHECKSUM;
                break;
        default:
                return MSF_ERROR_UNKNOW;
                break;
    }
}

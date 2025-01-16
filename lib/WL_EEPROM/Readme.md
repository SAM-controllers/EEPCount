Readme.txt: Library explanation

## Brief Operational Explanation ##
This library splits the EEPROM chip's memory into multiple "blocks" which each have a header and a data portion. 


## General Usage ##
### Preparation
To use this library, you will need to know the **capacity** (in bytes), **page size** (in bytes), **address bytes** (1 or 2), and the **i2c address** for your eeprom chip.

You also need to decide on a **block data size** to use. This library divides the memory into blocks. Each block has a header before a number of bytes
#### Choosing a block size:

The max dataBlockBytes value is memSizeBytes - 16 bytes (which is the size of the block header), but that results in no wear leveling capability.

The max practical dataBlockBytes value with wear leveling is (memSizeBytes / 2) - 1 page. (Or -16 bytes if the page size is smaller than 16) The minimum practical size is 16 bytes.

For a 32kb memory, the max size with wear leveling is 960 bytes: (4096 / 2) - (64 byte page) = 960

### Initialization and begin()



### Migrating Code Written for the SparkFun_External_EEPROM Library
1) Memory size, page size, and address bytes are set in the constructor (at compile time) instead of in functions
2) If your code used to call detectMemorySizeBytes() or detectPageSizeBytes(), print out what they return using 
    Sparkfun's library, use those values in the constructor, and remove those calls from your code. This lets the compiler optimize the code size considerably.
3) begin() returns **0 on success** or an error code (1-255) if there's a failure. **The Sparkfun library does the opposite.**
    
    Use something like this to check for successful initialization in your setup for an instance called 'eep':
    ```
    eep.setInfoOutput(&Serial);   // Enable printing warnings (highly recommended)

    uint8_t eepInitErr = eep.begin(eepromAddr);  // Save the result of begin()

    // Print the error code. See EepWarn_t enum in the .cpp file for details
    if(eepInitErr) { Serial.print("EEPROM init failure! Err #"); Serial.println(eepInitErr); }  
--------------------------------------------------------------------------------------------------------


## Troubleshooting

### Troubleshooting Initialization Problems 
When this.begin(addr) returns >= 1 (returning 0 == success)
1) Check hardware issues. Is it powered? Are SDA and SCL connected correctly? Are there pullup resistors?
2) Check configuration like the i2c port, i2c address, and speed. Will it connect with the original Sparkfun Library? (https://github.com/sparkfun/SparkFun_External_EEPROM_Arduino_Library)
3) If it works with the other library but begin() returns > 0, then either the address is not found on the i2c bus, or the there's other data not from this library already on the chip.
The allowInitToOverwrite parameter of begin() is to avoid overwriting data previously stored on a chip that wasn't stored by this library.
Calling .begin(addr, false) will treat the chip as read only if the data block header is missing or invalid. Save any valueable data from the chip with printprintRawMemory(Serial).

### Problems after a successful begin()
1) Enable debug output by calling `eep.setInfoOutput(&Serial);` before calling `eep.begin()`. If there are warnings (like `!WARN#3!`), go to the `EepWarn_t` enum (at the top of WL_EEPROM.cpp) for more info on the warning number
2) Reduce the page size to 8 bytes. If that fixes the issue (wrong data stored, invalid header, or incorrect addresses), then double the page size until it stops working. 
   
   There's very little harm in setting a page size smaller than the actual chip page size.

3) Use [thisLibName].printprintRawMemory(Serial) before and after the problem occours to see what's going wrong

## Brief Operational Explanation ##
This library splits the EEPROM chip's memory into multiple "blocks" which each have a header and a data portion


## Return value reference
read() and write() return the result of the last i2c endTransmission() OR -1 which is special for this library

- -1 .. Refused to write due to invalid header. Save data if needed, erase chip, and try again (From this library, not i2c)
-  0 .. success
-  1 .. length to long for buffer
-  2 .. address send, NACK received
-  3 .. data send, NACK received
-  4 .. other twi error (lost bus arbitration, bus error, ..)
-  5 .. timeout


## Application notes and details 
### Core functionality

Goals of this WearLeveling lib:
 - Add wear leveling capability while being mostly transparent to the user, apart from the different constructor and reduced available memory.
 - Make more parameters compile time to reduce code size and memory use.
 - Be a robust solution suitable for a production environment.
 - Retry in case of failure and fail gracefully if that doesn't work.
 - Try to warn the 
 - Provide easy auditing of the internal workings of the library. There are two ways to do that:
    1) #define EEPROM_PRINT_READWRITE_INFO prints information about address translation, read operations, and write operations when defined.
    2) The printRawMemory() and printCounterStruct() functions work even when EEPROM_PRINT_DEBUG_INFO is disabled and if they don't get called, the compiler optimises them away.

< in progress documentation  below>
 - Transparent erase can erase the whole chip while preserving the wear count (and thus the address offset)

The wear leveling is accomplished by a write counter and address offset translation



### Wear leveling counter explanation 
/*
    Abacus bytes optimize for eeprom lifetime by starting at 0xff (all 1's) and count "up" by setting bits to 0.
    Once all bits are 0, they trigger an overflow to the next least significan storage structure (abacus or int)
    Counter life expectancy is limited by the size of abacus0:
    Number of increments = (8bits) * (n bytes of abacus0) * (L lifetime byte resets)  // General form
        3.2 million      =   8     *      4               *  100,000                  // Calculation for 4 bytes of abacus0

    Abacus1 stores a rollover count of abacus0 to reduce the number of writes to int0 by a factor of 8
    int0,1, and 2 are normal integers storing the number of rollovers of the unit before them
    If the highest abacus rollover happens every 256 counts, then the integers are like the normal highest three bytes of a normal uint32_t
    */

struct WearLevelCounter_t{
    uint32_t abacus0; // Single increments
    uint8_t abacus1;  // Number of abacus0 rollovers
    uint8_t uint0;     // Number of abacus1 rollovers
    uint8_t uint1;     // Number of int0 rollovers
    uint8_t uint2;     // Number of int1 rollovers
} ;

### Page/address byte values from the original sparkfun library
These were taken from the switch case satement that tried to guess the address bytes and page size based on total capacty. They're a good starting place if you don't know values for your chip

    void WL_EEPROM::setMemorySizeBytes(uint32_t memSize) 
     // Nonstandard case formatting to reduce scrolling
     switch (memSizeBytes)    {
     // Capacity (bytes)    Address bytes               Page size (bytes)
     case (16):	            setAddressBytes(1);	        setPageSizeBytes(1);	        break;
     case (128):	        setAddressBytes(1);	        setPageSizeBytes(8);	        break;
     case (256):	        setAddressBytes(1);	        setPageSizeBytes(8);	        break;
     case (512):	        setAddressBytes(1);	        setPageSizeBytes(16);	        break;
     case (1024):	        setAddressBytes(1);	        setPageSizeBytes(16);	        break;
     case (2048):	        setAddressBytes(1);	        setPageSizeBytes(16);	        break;
     case (4096):	        setAddressBytes(2);	        setPageSizeBytes(32);	        break;
     case (8192):	        setAddressBytes(2);	        setPageSizeBytes(32);	        break;
     case (16384):	        setAddressBytes(2);	        setPageSizeBytes(64);	        break;
     case (32768):	        setAddressBytes(2);	        setPageSizeBytes(64);	        break;
     case (65536):	        setAddressBytes(2);	        setPageSizeBytes(128);	        break;
     case (128000):	        setAddressBytes(2);	        setPageSizeBytes(128);	        break;
     case (262144):	        setAddressBytes(2);	        setPageSizeBytes(256);	        break;
     default: break;  // Unknown memory size
     }


```/*
    From sparkfun lib:
  This is a library to read/write to external I2C EEPROMs.
  It uses the same template system found in the Arduino
  EEPROM library so you can use the same get() and put() functions.

  https://github.com/sparkfun/SparkFun_External_EEPROM_Arduino_Library
  Best used with the Qwiic EEPROM: https://www.sparkfun.com/products/18355

  Various external EEPROMs have various interface specs
  (overall size, page size, write times, etc). This library works with
  all types and allows the various settings to be set at runtime. <-- (no longer the case in the WL lib)

  All read and write restrictions associated with pages are taken care of.
  You can access the external memory as if it was contiguous.

  Development environment specifics:
  Arduino IDE 1.8.x

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

*/```
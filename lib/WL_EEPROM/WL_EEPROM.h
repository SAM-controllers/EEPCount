/*
    See Readme.txt for info on this library and its usage.

// === MIGRATING FROM SparkFun_External_EEPROM === //
1) Memory size and page size are set in the constructor (at compile time) instead of in functions
2) If your code used to call detectMemorySizeBytes() or detectPageSizeBytes(), print out what they return using 
    Sparkfun's library, use those values in the constructor, and remove those calls from your code. This lets the compiler optimize the code size considerably.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.
*/

#ifndef _WLEEPROM_H
#define _WLEEPROM_H

// Version defines
#define EEPROM_LIB_VERSION_MAJOR 0
#define EEPROM_LIB_VERSION_MINOR 4
#define EEPROM_LIB_VERSION_PATCH 4
#define BRANCH_DESCRIPTION_STRING "Block Offset"


#include "Arduino.h"
#include "Wire.h"
#include "PlatformDefines.h"

// === Compile Flags === //
// #define EEPROM_USE_CUSTOM_DELAY     // Define your own non-blocking or thread safe delay function. Useful for RTOS or queueing a read/write operation and running other code during the delay
// #define EEPROM_DISABLE_BLOCK_SHIFT  // (NOT_UNTIL_BLOCK_SHIFT) Once block shifting is implemented, always use block 0. Still keeps track of write count, but does not copy/readdress page locations after reaching some number of writes
#define EEPROM_DELAY_BEFORE_WRITE_POLLING  // Wait writeTime_ms milliseconds before polling the chip to see if it's ready after a write
#define EEPROM_PRINT_READWRITE_INFO // (Debugging) Print what address is being requested, translated, and used for any read or write
#define PAD_TO_PAGE_SIZE    // Align the data block byte 0 with the first byte of a page

// Defines and enums
#define WRITE_PIN_DISABLED 255  // Use this pin value to tell the library not to use hardware write protection

enum class BlockStatus_t : uint8_t { 
    Uninitialized = 0xff,
    Available = 0xaa,                       // Has been initialized as a block but has not been used as the current valid block yet
    CurrentValidBlock = 0x99,               // The working area of memory
    // TransitioningFromCurrentValid = 0x88,   // (Removed) The data in this block is current and valid. The process of transitioning to the next block has started but is not complete - Removed, go straight from current valid to UsedUndamaged
    UnvalidatedNextDestination = 0x77,      // The data is being copied from the current valid block to here, but has not been confirmed correct yet
    UsedUndamaged = 0x66,                   // This has been the current valid block before and has reached its max write count, but no errors were reported
    Damaged = 0x55,                         // Error writing to one or more cells in this block
    // ConfigErrorBlock0Fallback = 0x44,       // ToDo: Something is wrong with the block config, so using block 0 forever
};

enum CounterReturn_t : uint8_t {
    counterValid = 0,
    counterInvalid = 1,
    counterBytesBlank = 2,  // All the bytes were 0xff (255) which is likely uninitialized memory, even though technically it's a valid counter state (well past when the chip should have worn out)
    counterReadWriteErr = 3,  // Problem with eeprom chip
};

struct struct_memorySettings
{
    TwoWire *i2cPort;// ToDo: make const
    uint8_t deviceAddress;// ToDo: make const
    uint32_t memorySize_bytes;// ToDo: make const
    uint16_t pageSize_bytes;// ToDo: make const
    uint8_t writeTime_ms;
    uint8_t addressSize_bytes;// ToDo: make const
    uint8_t wpPin;  // ToDo: make const
};

// The raw counter bytes that get saved to eeprom
struct CounterBytes_t{
    uint32_t abacus0; // Single increments
    uint8_t abacus1;  // Number of abacus0 rollovers
    uint8_t uint0;     // Number of abacus1 rollovers
    uint8_t uint1;     // Number of int0 rollovers
    uint8_t uint2;     // Number of int1 rollovers
} ;

// This needs a better name, but includes the raw data plus associated info for ease of use
struct EepCounter_t{
    uint32_t addr;
    uint32_t count;
    CounterBytes_t wlc; // Counter goes last so user code doesn't have to initialize it
};
    
struct BlockHeader_t {
    uint16_t blockSizeBytes;    // How many bytes per chunk NOT including the header
    uint8_t currentBlock;
    BlockStatus_t blockStatus;
    uint8_t reserved[4];        // For now, keep the block header at 16 bytes manually (8 header, 8 wlc)
    CounterBytes_t blockWLC;
};
// const int commentMeOut = sizeof(BlockHeader_t);  // For conveniently checking sizeof

class WL_EEPROM
{
  public:
    WL_EEPROM(const uint32_t memSizeBytes, const uint16_t dataBlockBytes = 256, const uint16_t pageSizeBytes = 16, const uint8_t addressBytes = 2, TwoWire &i2cPort = Wire);
    // === Whole chip functions === //

    void setInfoOutput(HardwareSerial *serialPort); // Select a serial port to print warnings. Call this before begin()
    uint8_t begin(uint8_t deviceAddress = 0b1010000, bool allowInitToOverwrite = true); // Start the i2c bus and validate the starting block header. Will overwrite invalid headers/data if allowInitToOverwrite == true or the chip is blank. 
    void setWriteProtectPin(int16_t pin = -1);
    void erase(uint8_t toWrite = 0xff); // Erase the entire memory. Optional: specify the byte value to write to every address on the chip.
    uint8_t checkWriteErrAtAddr(uint16_t rawByteAddress, uint8_t checkValue = 0xaa); // Validate that a byte is writeable (restores the starting value after the check)
    void disableWriting(void) { wl.protectExistingData = true;} // Treat the chip as read only

    // === Get info about settings and status === //

    bool isConnected(uint8_t i2cAddress = 255);
    bool isBusy(uint8_t i2cAddress = 255);
    uint32_t getUsableBytes();// Return the data block size. User code can read and write addresses from 0x0 to <this value - 1>
    inline uint32_t length(){ return getUsableBytes(); }  // Alias
    inline uint32_t getChipSizeBytes(){ return settings.memorySize_bytes;}  // Returns the raw chip capacity before any abstration or wear leveling address translation
    uint8_t getAddressBytes();
    uint16_t getPageSizeBytes();
    constexpr uint16_t getI2CBufferSize() {return I2C_BUFFER_LENGTH_TX; }; // Return the size of the TX buffer
    bool chipIsBlank();     // Returns true if all the bytes on the chip hold the same value (Typically 0xff)

    // === Counter Functions (WLC = Wear Leveling Counter, WLCO = Wear Leveling Counter Object (really a struct)) === //

    uint32_t getCounterValue(CounterBytes_t &wlc);
    uint32_t getCounterValue(EepCounter_t &wlco)              { return getCounterValue(wlco.wlc); }
    uint8_t loadCounterValue(CounterBytes_t &wlc, uint32_t startAddr) { return loadCounterValue(wlc, startAddr, false);}
    uint8_t loadCounterValue(EepCounter_t &wlco);// Moved to .cpp 
    void saveCounterValue(CounterBytes_t &wlc, uint32_t startAddr)    { saveCounterValue(wlc, startAddr, false);}
    inline void saveCounterValue(EepCounter_t &wlco)          { saveCounterValue(wlco.wlc, wlco.addr, false);}
    uint32_t incrementCounter(CounterBytes_t &wlc, uint32_t startAddr, bool saveToEeprom = true) { return incrementCounter(wlc, startAddr, saveToEeprom, false); }
    uint32_t incrementCounter(EepCounter_t &wlco, bool saveToEeprom = true);
    uint32_t resetCounter(CounterBytes_t &wlc, uint32_t startAddr)    { return resetCounter(wlc, startAddr, false); }
    uint32_t resetCounter(EepCounter_t &wlco);// Moved to .cpp
    uint8_t setCounterValue(CounterBytes_t &wlc, uint32_t newCount, uint32_t addr, bool saveToEeprom = true);
    uint8_t setCounterValue(EepCounter_t &wlco, uint32_t newCount, bool saveToEeprom = true);
    uint8_t loadOrInitCounter(EepCounter_t &wlco);
    bool countIsInvalid(CounterBytes_t &wlc);
    inline bool countIsInvalid(EepCounter_t &wlco)            { return countIsInvalid(wlco.wlc);  }

    // === Debugging === //
    // These functions take a Serial port as a parameter so they can be indepenent of the status of infoSerial and whether address printing is turned on
    void printMemory(HardwareSerial &s, char byteSeparator = ' ');                              // Print the data in the data block
    void printRawMemory(HardwareSerial &s, uint32_t maxAddr = 0, char byteSeparator = ' ');     // Print all the bytes on a chip, including any headers and reserve spaces
    void printCounterStruct(HardwareSerial &s, EepCounter_t &wlco); // Debugging
    void printBlockInfo(HardwareSerial &s);  // Debugging
    void printBlockHeader(HardwareSerial &s);
    void setPrintBytesPerLine(uint16_t bytes) { this->bytesPerLine = bytes; } // Setting for printMemory and print

    // === Read and write functions === //
    uint8_t read(uint32_t eepromLocation);                                  // Read a single byte
    int read(uint32_t eepromLocation, uint8_t *buff, uint16_t bufferSize);  // Read consecutive bytes to a buffer
    int write(uint32_t eepromLocation, uint8_t dataToWrite);                // Write a single byte
    int write(uint32_t eepromLocation, const uint8_t *dataToWrite, uint16_t bufferSize); // Write consecutive bytes to EEPROM
    
    // Functionality to 'get' and 'put' objects to and from EEPROM.
    // These public templates apply address translation, then call the internal version
    template <typename T>       T &get(uint32_t idx, T &t)              {  return _rawget(       this->addrToHwAddr(idx), t);  }  
    template <typename T> const T &put(uint32_t idx, const T &t)        {  return _rawput(       this->addrToHwAddr(idx), t);  }  
    template <typename T> const T &putChanged(uint32_t idx, const T &t) {  return _rawputChanged(this->addrToHwAddr(idx), t);  }


    #ifdef EEPROM_USE_CUSTOM_DELAY
    std::function<void(uint32_t dlTimeMs)>_delay; // Overridable delay function for async/RTOS/Multithread functionality
    // std::function<void(uint32_t dlTimeUs)>_delayMicroseconds; // Overridable delay function for async/RTOS/Multithread functionality
    #else
    #define _delay delay  // Use normal delay if not using a custom version
    #endif


    // Deprecated functions
    // These functions will likely be removed:
    uint8_t detectAddressBytes(); // Determine the number of address bytes, 1 or 2
    void setAddressBytes(uint8_t addressBytes); __attribute__ ((deprecated("Set the address bytes in the constructor"))) ;
    void setPageSizeBytes(uint16_t pageSize); __attribute__ ((deprecated("Set the page size in the constructor"))) ;
    void setWriteTimeMs(uint8_t writeTimeMS); // Set the number of ms required per page write
    uint8_t getWriteTimeMs();
    inline void setMemorySizeBytes(uint32_t){return;} __attribute__ ((deprecated("Set the size of memory in the constructor. This function does nothing but is included for compatability"))) ;
    // Deprecated functions
    // void enablePollForWriteComplete(); // Set this preference with a define. Most EEPROMs allow I2C polling of when a write has completed
    // void disablePollForWriteComplete();  // Set this preference with a define. 
    // uint32_t detectMemorySizeBytes();          // Attempts to detect the size of the EEPROM  // Removed for code size
    // void setMemorySize(uint32_t memSize);      // Depricated
    // uint32_t getMemorySize();                  // Depricated
    // void setMemoryType(uint16_t typeNumber);      // Removed for being unclear. Use setMemorySizeBytes instead
    // uint32_t putString(uint32_t eepromLocation, String &strToWrite);
    // void getString(uint32_t eepromLocation, String &strToRead);

  private:    
    // Documentation through the private area is a work in progress...

    inline uint32_t chipMaxAddr(){ return getChipSizeBytes()-1; }   // Highest valid writable address
    void printWarning(uint8_t);  // Only does something if infoSerial has been set to a serial interface
    void printRawMemory(HardwareSerial &s, uint32_t maxAddr, char byteSeparator, uint32_t startAddr, uint32_t addrPrintOffset);

    uint32_t getOffset();
    uint8_t findCurrentBlock();
    uint8_t validateBlockHeader(BlockHeader_t &h);
    void writeBlockHeader(uint32_t addr, BlockStatus_t status = BlockStatus_t::Available);

    // uint8_t _rawread(uint32_t eepromLocation);   // Single byte reads use a uint8_t[1] buffer to avoid code duplication
    int _rawread(uint32_t eepromLocation, uint8_t *buff, uint16_t bufferSize);
    // int _rawwrite(uint32_t eepromLocation, uint8_t dataToWrite); // Single byte writes use a uint8_t[1] buffer to avoid code duplication
    int _rawwrite(uint32_t eepromLocation, const uint8_t *dataToWrite, uint16_t blockSize);
    void zeroCounterStruct(CounterBytes_t &wlc);
    uint32_t addrToHwAddr(uint32_t addr);

    // Functionality to 'get' and 'put' objects to and from EEPROM.
    template <typename T> T &_rawget(uint32_t idx, T &t)
    {
        uint8_t *ptr = (uint8_t *)&t;
        _rawread(idx, ptr, sizeof(T)); // Address, data, sizeOfData
        return t;
    }

    template <typename T> const T &_rawput(uint32_t idx, const T &t) // Address, data
    {
        const uint8_t *ptr = (const uint8_t *)&t;
        _rawwrite(idx, ptr, sizeof(T)); // Address, data, sizeOfData
        return t;
    }

    template <typename T> const T &_rawputChanged(uint32_t idx, const T &t)  // Address, data
    {
      const uint8_t *newData = (const uint8_t *)&t;
      uint8_t oldData[sizeof(T)];
      _rawread(idx, oldData, sizeof(T));  // Address, data, sizeOfData
      for (uint16_t i = 0; i < sizeof(T); i++) {
        if (oldData[i] != newData[i]) {
          _rawwrite(idx + i, &newData[i], 1);
        }
      }
      return t;
    }

    // Private versions of the counter functions which can take raw or to-be-translated addresses
    uint8_t loadCounterValue(CounterBytes_t &wlc, uint32_t startAddr, bool addressIsRaw);
    void saveCounterValue(CounterBytes_t &wlc, uint32_t startAddr, bool addressIsRaw);
    uint32_t incrementCounter(CounterBytes_t &wlc, uint32_t startAddr, bool saveToEeprom, bool addressIsRaw);
    uint32_t resetCounter(CounterBytes_t &wlc, uint32_t startAddr, bool addressIsRaw);

    // === Internal instances of settings, counters, pointers, etc === //
    HardwareSerial *infoSerial = nullptr;  // Default to none (disable info printing)
    uint16_t bytesPerLine;  // Setting for dump memory
    // Default settings are for onsemi CAT24C51 512Kbit I2C EEPROM used on SparkFun Qwiic EEPROM Breakout
    struct_memorySettings settings = {
        .i2cPort = &Wire,
        .deviceAddress =
            0b1010000, // 0x50; format is 0b1010 + (A2 A1 A0) or 0b1010 + (B0 A1 A0) for larger (>512kbit) EEPROMs
        .memorySize_bytes = 4096, // Default to 4096, to support 24xx32 / 4096 byte EEPROMs and larger
        .pageSize_bytes = 32, // Default to 32 bytes, to support 24xx32 / 4096 byte EEPROMs and larger
        .writeTime_ms = 5, //All EEPROMs seem to have a max write time of 5ms
        .addressSize_bytes = 2, // Default to two address bytes, to support 24xx32 / 4096 byte EEPROMs and larger
        .wpPin = WRITE_PIN_DISABLED, // By default, the write protection pin is not set
    };
    // uint32_t keepMeCommentedOut = sizeof(struct_memorySettings);

    // Properties used for wear leveling (wl) combined into one struct for easy tab completion
    struct AddrTranslation_t{
      uint8_t currentBlockIndex;  // Always 0 until swapping blocks
      uint32_t wlcAddr;         // Always 8 until swapping blocks (the offset of the wlc within the block header)
      uint32_t counterCount;    // Write count
      uint32_t addrOffset;      // Gets set by findCurrentBlock()
      uint16_t blockDataBytes;  // data block size NOT including the header
      const uint8_t blockHeaderSize = sizeof(BlockHeader_t);
      uint32_t writesPerRotate; 
      bool protectExistingData; // If the data block header is missing or mismatched, don't overwrite stored data EXCEPT for erase() since then it's clear the user intends to lose any stored data
      // uint16_t availablePages;  // Removed: Do things in terms of total memory, not pages
    } wl;
    // uint32_t keepMeCommentedOut = sizeof(AddrTranslation_t);
    
    CounterBytes_t _wlc; // "_wlc" stands for "Internal wear leveling counter struct" and contains the in-memory version of the current block's counter
};

#endif //_WEARLEVELING_H

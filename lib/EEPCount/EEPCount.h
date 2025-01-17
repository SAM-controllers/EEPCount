/*
MIT License

Copyright (c) 2025 SAM controllers

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#ifndef EEPCOUNT_H_
#define EEPCOUNT_H_

// Version defines
#define EEPCOUNT_VERSION_MAJOR 0
#define EEPCOUNT_VERSION_MINOR 1
#define EEPCOUNT_VERSION_PATCH 0


#include "Arduino.h"
#include "Wire.h"
#include "PlatformDefines.h"
#include "EEPROM.h"


enum CounterReturn_t : uint8_t {
    counterValid = 0,
    counterInvalid = 1,
    counterBytesBlank = 2,  // All the bytes were 0xff (255) which is likely uninitialized memory, even though technically it's a valid counter state (well past when the chip should have worn out)
    counterReadWriteErr = 3,  // Problem with eeprom chip
};

// These bytes get saved to the eeprom
struct CounterBytes_t {
    uint32_t abacus0; // Single increments
    uint8_t abacus1;  // Number of abacus0 rollovers
    uint8_t uint0;     // Number of abacus1 rollovers
    uint8_t uint1;     // Number of int0 rollovers
    uint8_t uint2;     // Number of int1 rollovers
};

// The raw data plus associated info for ease of use
struct EepCounter_t{
    uint32_t addr;
    uint32_t count;
    CounterBytes_t wlc; // Counter goes last so user code doesn't have to initialize it
};

class EEPCount {
public:
    EEPCount(EEPROMClass &mem);
    // === Whole chip functions === //
    void setInfoOutput(HardwareSerial *serialPort); // Select a serial port to print warnings. Call this before begin()
    uint8_t begin(uint8_t deviceAddress = 0b1010000, bool allowInitToOverwrite = true); // Start the i2c bus and validate the starting block header. Will overwrite invalid headers/data if allowInitToOverwrite == true or the chip is blank. 
    
    // === Counter functions === //
    uint32_t getCounterValue(EepCounter_t &wlco)              { return getCounterValue(wlco.wlc); }
    uint8_t loadCounterValue(EepCounter_t &wlco);
    inline void saveCounterValue(EepCounter_t &wlco);//          { saveCounterValue(wlco.wlc, wlco.addr, false);}
    uint32_t incrementCounter(EepCounter_t &wlco, bool saveToEeprom = true);
    uint32_t resetCounter(EepCounter_t &wlco);
    uint8_t setCounterValue(EepCounter_t &wlco, uint32_t newCount, bool saveToEeprom = true);
    uint8_t loadOrInitCounter(EepCounter_t &wlco);
    inline bool countIsInvalid(EepCounter_t &wlco)            { return countIsInvalid(wlco.wlc);  }


    uint32_t getCounterValue(CounterBytes_t &wlc);
    bool countIsInvalid(CounterBytes_t &wlc);
    uint32_t incrementCounter(CounterBytes_t &wlc, uint32_t startAddr, bool saveToEeprom = true);// { return incrementCounter(wlc, startAddr, saveToEeprom, false); }
    uint8_t loadCounterValue(CounterBytes_t &wlc, uint32_t startAddr);// { return loadCounterValue(wlc, startAddr, false);}
    void saveCounterValue(CounterBytes_t &wlc, uint32_t startAddr);//    { saveCounterValue(wlc, startAddr, false);}
    uint32_t resetCounter(CounterBytes_t &wlc, uint32_t startAddr);//    { return resetCounter(wlc, startAddr, false); }
    uint8_t setCounterValue(CounterBytes_t &wlc, uint32_t newCount, uint32_t addr, bool saveToEeprom = true);

    // === Debugging === //
    // These functions take a Serial port as a parameter so they can be indepenent of the status of infoSerial and whether address printing is turned on
    void printCounterStruct(HardwareSerial &s, EepCounter_t &wlco);

    // Functionality to 'get' and 'put' objects to and from EEPROM.
    template <typename T>       T &get(uint32_t idx, T &t)              {  return memLib->get(  this->addrToHwAddr(idx), t);  }  
    template <typename T> const T &put(uint32_t idx, const T &t)        {  return memLib->put(  this->addrToHwAddr(idx), t);  }  
    template <typename T> const T &putChanged(uint32_t idx, const T &t) {  return memLib->put(  this->addrToHwAddr(idx), t);  }           // ToDo: manually only update changed bytes

private:
    EEPROMClass *memLib = nullptr;  // This is a placeholder for any eeprom lib with "get" and "put"

    void zeroCounterStruct(CounterBytes_t &wlc);
    void printWarning(uint8_t errNum);

    

    // === Internal instances of settings, counters, pointers, etc === //
    HardwareSerial *infoSerial = nullptr;  // Default to none (disable info printing)
    
    CounterBytes_t _wlc; // "_wlc" stands for "Internal wear leveling counter struct" and contains the in-memory version of the current block's counter
};

#endif //_WEARLEVELING_H

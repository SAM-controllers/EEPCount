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

#include "EEPCount.h"
#include "Arduino.h"
#include "Wire.h"

// Warnings for various things that can go wrong
// These live at the top of the .cpp file so that the internal functions can access them but they don't pollute the global namespace
enum EepWarn_t : uint8_t{
    noErr = 0,
};


// Local definitions for code readability
#define EEP_ADDRESS_UNUSED 0 // The required address parameter doesn't matter since we aren't saving to an address
#define EEP_NO_SAVE_TO_EEPROM  false    // Update values in ram only without saving to eeprom chip


/// @brief Declare what the EEPCount library should use for accessing the eeprom. Must include a get(addr, dest) and put(addr, datatoput) function
/// (EEPROMClass is a placeholder)
EEPCount::EEPCount(EEPROMClass &mem){
    memLib = &mem;
}

/// @brief 
/// @param deviceAddress 
/// @param allowInitToOverwrite 
/// @return Error code. 0 = no error, init successful. >= 1: see EepWarn_t
uint8_t EEPCount::begin(uint8_t deviceAddress, bool allowInitToOverwrite)
{
    // Check validity and get current count   

    return noErr;
}

// ###########################
// ## Wear leveling counter ##
// ###########################

/// @brief Calculate the current counter value of a wlc already in memory
/// @param wlc Reference to the counter struct
/// @return The current count
uint32_t EEPCount::getCounterValue(CounterBytes_t &wlc){
    bool errorFound = false;
    uint32_t counterValue = 0;
    
    // Count the 1's in the abacus bytes
    uint32_t tmpCopy = wlc.abacus0;
    uint32_t abacusOnes = 0;
    while(tmpCopy > 0){ // While there are ones to shift
        // // Starting from the lsb, an abacus integer should be all 1's, then all 0's. 
        // // If tmpCopy is > 0 but the lsb is 0, it's not a valid abacus value.
        // if( (tmpCopy & 0x01) == 0 ) {printWarning(wlcAbacusValueInvalid); errorFound = true;}

        abacusOnes++;  // Easiest to count the number of 1's even though 0's are what matters
        tmpCopy /= 2;   // aka shift right one
    }
    counterValue = (32-abacusOnes);    // Each 0 is a count, each 1 is a lack-of-count


    tmpCopy = wlc.abacus1;
    abacusOnes = 0;
    while(tmpCopy > 0){
        // if( (tmpCopy & 0x01) == 0 ) {printWarning(wlcAbacusValueInvalid); errorFound = true;}   // See above explanation

        abacusOnes ++;
        tmpCopy /= 2;   // aka shift right one
    }
    //       +=(Counts in abacus1) * (Number of bits in abacus0)
    counterValue += (8-abacusOnes) * (8 * sizeof(wlc.abacus0)); 

    // Note: this section assumes one rollover of abacus1 = 256 counts:
    counterValue += (uint32_t(wlc.uint0) << 8);// Find the 256's value
    counterValue += (uint32_t(wlc.uint1) << 16);// Find the 65k's value
    counterValue += (uint32_t(wlc.uint2) << 24 );// Find the high byte

    return counterValue;
}

/// @brief This checks the data in a counter struct to see if it is a valid count. 
/// @param wlc The counter struct in memory
/// @return 0: Likely valid, >=1: Definitely not valid
/// @note There is a chance of false negatives, meaning it's possible to have uninitialized memory test valid.
/// An invalid test, however, is definitely invalid and should be initialized.
bool EEPCount::countIsInvalid(CounterBytes_t &wlc){
    bool returnValue = EXIT_SUCCESS;    // Initialize return to valid

    // Check the 1's in the abacus bytes
    uint32_t tmpCopy = wlc.abacus0;
    while(tmpCopy > 0){ // While there are ones to shift
        // Starting from the lsb, an abacus integer should be all 1's, then all 0's. 
        // If tmpCopy is > 0 but the lsb is 0, it's not a valid abacus value.
        if( (tmpCopy & 0x01) == 0 ) returnValue = EXIT_FAILURE;
        tmpCopy /= 2;   // aka shift right one
    }

    tmpCopy = wlc.abacus1;
    while(tmpCopy > 0){
        if( (tmpCopy & 0x01) == 0 ) returnValue = EXIT_FAILURE;
        tmpCopy /= 2;   // aka shift right one
    }

    // Check for blank memory
    // Technically it's a valid counter state when count = 0xffffff00 (4,294,967,040)
    // but it's WAY past when the chip should have worn out. Much more likely that it's
    // uninitialized memory
    if( wlc.abacus0 == __UINT32_MAX__ &&
        wlc.abacus1 == 255 &&
        wlc.uint0 == 255 &&
        wlc.uint1 == 255 &&
        wlc.uint2 == 255)
    {
        returnValue = counterBytesBlank;
    }
    return returnValue;
}

/// @brief Load the data from a counter struct from EEPROM into memory
/// @param wlc 
/// @param startAddr 
/// @return 0 = EXIT_SUCCESS with valid data. 1 = Loaded data is invalid. 2+ = other error
uint8_t EEPCount::loadCounterValue(CounterBytes_t &wlc, uint32_t startAddr){
    get(startAddr, wlc);
    return countIsInvalid(wlc); // Return the result of a validity check
}

/// @brief Load the data from a counter struct from EEPROM into memory, then set the count
/// @param wlco 
/// @return 0 = EXIT_SUCCESS with valid data. 1 = Loaded data is invalid. 2+ = other error (see enum CounterReturn_t)
uint8_t EEPCount::loadCounterValue(EepCounter_t &wlco) { 
    uint8_t loadReturnCode = loadCounterValue(wlco.wlc, wlco.addr); 
    if(loadReturnCode == EXIT_SUCCESS){
        wlco.count = getCounterValue(wlco.wlc);
    }
    return loadReturnCode;
}

void EEPCount::saveCounterValue(CounterBytes_t &wlc, uint32_t startAddr){
    putChanged(startAddr, wlc);
}

/// @brief Checks for an existing valid count on eeprom chip and initializes count to 0 if not found
/// @param wlco 
/// @return Return code: 
/// 0 = Already existing valid counter loaded (EXIT_SUCCESS). 
/// 1 = Loaded data that was not a counter, successfully initialized count to 0. 
/// 2 = All bytes were 0xff (255) which is technically valid, but almost certainly uninitialized. Initialized count to 0. 
/// @todo Address valid/in range check
uint8_t EEPCount::loadOrInitCounter(EepCounter_t &wlco){

    uint8_t loadReturnCode = this->loadCounterValue(wlco);

    if(loadReturnCode == counterValid){  // Loaded counter struct was valid and not blank
        wlco.count = getCounterValue(wlco.wlc);
        return counterValid;
    }

    // Otherwise, initialize the counter value to 0
    zeroCounterStruct(wlco.wlc);
    wlco.count = 0;
    saveCounterValue(wlco);

    return loadReturnCode;
}

/// @brief Add one to the counter count
/// @param wlco Wear leveling count object
/// @param saveToEeprom Normally true. If false, update in ram only, useful for when lots of increments happen or in time critical code.
/// @note If saveToEeprom is false, power loss or calling readCounterValue() will cause the count to reset to the last saved value on next load.
/// @return new counter count
uint32_t EEPCount::incrementCounter(EepCounter_t &wlco, bool saveToEeprom){
    wlco.count++;
    return incrementCounter(wlco.wlc, wlco.addr, saveToEeprom); 
}

/// @brief Add one to the counter count
/// @param startAddr Address of the counter struct
/// @param saveToEeprom Normally true. If false, update in ram only, useful for when lots of increments happen or in time critical code.
/// @note If saveToEeprom is false, power loss or calling readCounterValue() will cause the count to reset to the last saved value.
/// @return new counter count
uint32_t EEPCount::incrementCounter(CounterBytes_t &countStruct, uint32_t startAddr, bool saveToEeprom){
    // ToDo: Check status and load the current count if it hasn't already been loaded
    
    bool carryFlag = false;
    
    // === Singles count (Abacus0) === //
    if(countStruct.abacus0 > 0){
        countStruct.abacus0 /= 2;  // Set the leftmost 1 bit to 0 (normal increment)
    }
    else {
        countStruct.abacus0 = 0xffffffff;  // Reset count and set carry
        carryFlag = true;
    }

    // === Abacus 1 === //
    if(carryFlag){
        carryFlag = false;
        if(countStruct.abacus1 > 0) {   // Normal increment
            countStruct.abacus1 /= 2;
        }
        else {                  // Handle rollover
            countStruct.abacus1 = 0xff;
            carryFlag = true;
        }
    }

    // === Uint0 === //
    if(carryFlag){
        carryFlag = false;
        if(countStruct.uint0 < 255) {  // Normal increment
            countStruct.uint0 += 1;
        }
        else {                  // Handle rollover
            countStruct.uint0 = 0;
            carryFlag = true;
        }
    }

    // === Uint1 === //
    if(carryFlag){
        carryFlag = false;
        if(countStruct.uint1 < 255) {  // Normal increment
            countStruct.uint1 += 1;
        }
        else {                  // Handle rollover
            countStruct.uint1 = 0;
            carryFlag = true;
        }
    }

    // === Uint2 === //
    // Note: it should not be physically possible to roll over the uint2 place before the abacus0 cells wear out
    // They would have had to count over 6 billion increments for this to roll over
    if(carryFlag){
        countStruct.uint2 += 1;
    }

    // Write the updated information to eeprom
    if(saveToEeprom) putChanged(startAddr, countStruct);

    return true;
}

// Reset a counter to 0 or initialize a previously unused part of memory
uint32_t EEPCount::resetCounter(CounterBytes_t &wlc, uint32_t startAddr){
    // if(infoSerial != nullptr){
    //     infoSerial->print("<Resetting counter>");
    // }
    this->zeroCounterStruct(wlc);
    putChanged(startAddr, wlc);
    return 0;
}

uint32_t EEPCount::resetCounter(EepCounter_t &wlco){
    resetCounter(wlco.wlc, wlco.addr);
    wlco.count = 0;
    return 0;
}

/// @brief Set a counter struct to a given count
/// @param wlc The counter data struct to set
/// @param newCount What to update to
/// @param addr Where to save the updated count data (if saveToEeprom == true, unused if false)
/// @param saveToEeprom false = Only update in ram, do not write to chip. true = save new value to chip.
/// @return 0 = success
uint8_t EEPCount::setCounterValue(CounterBytes_t &wlc, uint32_t newCount, uint32_t addr, bool saveToEeprom){
    zeroCounterStruct(wlc);

    // Note: this section assumes one rollover of abacus1 = 256 counts:
    wlc.uint0 = (newCount >> 8) & 0xff ;// Find the 256's value
    wlc.uint1 = (newCount >> 16) & 0xff ;// Find the 65k's value
    wlc.uint2 = (newCount >> 24 ) & 0xff ;// Find the high byte

    // Quick and dirty way to set the abacus bytes
    newCount &= 0x000000ff; // Get the lowest 8 bits (which go in the abacus bytes)
    while(newCount){
        incrementCounter(wlc, EEP_ADDRESS_UNUSED, EEP_NO_SAVE_TO_EEPROM);    // only update in ram, don't save to chip each time
        newCount--;
    }
    if(saveToEeprom) saveCounterValue(wlc, addr);   // Save once at the end
    return counterValid;
}

/// @brief Set a counter to a given count
/// @param wlc The counter data struct to set
/// @param newCount What to update to
/// @param saveToEeprom false = Only update in ram, do not write to chip. true = save new value to chip.
/// @return 0 = success
uint8_t EEPCount::setCounterValue(EepCounter_t &wlco, uint32_t newCount, bool saveToEeprom){
    wlco.count = newCount;
    return setCounterValue(wlco.wlc, newCount, wlco.addr, saveToEeprom);
}

/// @brief Initialize a struct in memory (NOT ON THE EEPROM CHIP) to a count of 0
void EEPCount::zeroCounterStruct(CounterBytes_t &wlc){
    wlc = {.abacus0 = 0xffffffff, .abacus1 = 0xff, .uint0 = 0, .uint1 = 0, .uint2 = 0};
}

/// @brief Print the data in memory for a counter object. If you want to be sure it's what's on the chip, call saveCounterValue() and loadCounterValue() before printing
/// @param s Output serial port
/// @param wlco The object to print
void EEPCount::printCounterStruct(HardwareSerial &s, EepCounter_t &wlco){
    // s.printf("Counter struct: Value = %u\t a0|a1-u0|u1|u2: %x|%x-%u|%u|%u\n", counterCount, _wlc.abacus0, _wlc.abacus1, _wlc.uint0, _wlc.uint1, _wlc.uint2);
    s.print("\r\nCounter Count: 0x");
    s.print(wlco.count, HEX);
    s.print(" (");
    s.print(wlco.count);
    s.print(")");
    s.print(", a0: ");  s.print(wlco.wlc.abacus0, HEX);
    s.print(", a1: ");  s.print(wlco.wlc.abacus1, HEX);
    s.print(", i0: ");  s.print(wlco.wlc.uint0, HEX);
    s.print(", i1: ");  s.print(wlco.wlc.uint1, HEX);
    s.print(", i2: ");  s.print(wlco.wlc.uint2, HEX);
    s.println();
}

/// @brief Internal mechanism to automatically warn the user of problems
/// @param errNum Which warning from EepWarn_t to present
/// @note It is safe to call this function at any time. No need to check that infoSerial != nullptr.
/// @note The user needs to call setInfoOutput() for this to print anything.
void EEPCount::printWarning(uint8_t errNum){
    if(infoSerial != nullptr){
        infoSerial->print("!WARN#");
        infoSerial->print(errNum);
        infoSerial->print("! ");
    }
}

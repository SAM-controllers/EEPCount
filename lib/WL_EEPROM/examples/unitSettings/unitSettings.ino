#include "Arduino.h"

#include "WL_EEPROM.h"

// Define the EEPROM properties of your chip:
// const uint8_t eepromAddr = 0x57;    // Address for CC v5 
const uint8_t eepromAddr = 0x50; // A common eeprom address
const uint16_t eepromBytes = 4096;  // 32k bits
const uint16_t eepromDataBlockBytes = 256;
const uint8_t eepromPageSize = 32;

WL_EEPROM mem24(eepromBytes, eepromDataBlockBytes, eepromPageSize); // Declare the 24cx32 eeprom object

#ifdef Serial1
#define Serial Serial1  // Redirect serial output
#endif

// === Settings Struct Example === //
// It may make sense to have two structs: 
// - One for things that don't change often (Serial number, config) 
// - and one for things that do (hoursOn, usage counters, etc)
// That can improve the performance of putChanged() by reducing the amount of changes to check each time

const uint32_t structAddr = 32;
struct myStruct_t {
    uint8_t magicNumber;    // Used to determine that this struct has been written to this address before
    uint32_t serialNum;
    uint32_t hoursOn;
    uint8_t config[8];
    uint8_t libVersion[3];
};


// The starting values to save to an unininitialized memory and compare on startup
const myStruct_t unitInfoDefault{.magicNumber = 0xAB, .serialNum = 0x123456, .hoursOn = 0, .config{8,3,7,2},
    .libVersion{EEPROM_LIB_VERSION_MAJOR, EEPROM_LIB_VERSION_MINOR, EEPROM_LIB_VERSION_PATCH}};

// This holds struct to check for previously written data, then hold the runtime values during operation
myStruct_t unitInfo;

void setup() {
    Serial.begin(115200);
    Serial.println("This example is still a work in progress");

    // You should usually enable info output unless you're out of flash. 
    // It has almost 0 overhead when things are *not* going wrong.
    // Look up any error codes at the top of WL_EEPROM.cpp
    mem24.setInfoOutput(&Serial);   

    // Begin (returns 0 for success)
    uint8_t initReturn = mem24.begin(eepromAddr); 
    
    // Recommended to check that begin() succeeded
    if (initReturn != EXIT_SUCCESS) {
        Serial.println(F("EEPROM init failure!"));
        Serial.print("Error code: ");
        Serial.println(initReturn);
        // Look up error codes and warnings in the enum at the top of WL_EEPROM.cpp
    }

    




    
    myStruct_t unitInfo;
    mem24.get(structAddr, unitInfo);

    // Initialize an uninitialized memory
    if(unitInfo.magicNumber != unitInfoDefault.magicNumber) {
        mem24.put(structAddr, unitInfoDefault);
        unitInfo = unitInfoDefault;
    }
    else{
        // Check version number, then load the saved struct to working memory
    }


}

void loop() {
    delay(100);

    // putChanged is especially useful when some bytes don't change
    // mem24.putChanged(structAddr, unitInfo);
}
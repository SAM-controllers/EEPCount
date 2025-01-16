#include "Arduino.h"

#include "WL_EEPROM.h"

// const uint8_t eepromAddr = 0x50; // A common eeprom address
const uint8_t eepromAddr = 0x57;    // Address for CC v5 
const uint16_t eepromBytes = 4096;  // 32k bits
const uint16_t eepromDataBlockBytes = 256;
const uint8_t eepromPageSize = 32;

WL_EEPROM mem24(eepromBytes, eepromDataBlockBytes, eepromPageSize); // Declare the 24cx32 eeprom object

#ifdef Serial1
#define Serial Serial1  // Redirect serial output
#endif

void setup() {
    Serial.begin(115200);
    Serial.println("Begin Code");

    // You should usually enable info output unless you're out of flash. 
    // It has almost 0 overhead when things are *not* going wrong.
    // Look up any error codes at the top of WL_EEPROM.cpp
    mem24.setInfoOutput(&Serial);   

    // Begin 
    if (mem24.begin(eepromAddr) != EXIT_SUCCESS) Serial.println(F("EEPROM init failure!"));

    // === Disabling overwriting during begin() === //
    // By default it will overwrite the first 16 bytes with the block header if there isn't one there already
    // You can disable automatic overwriting using .begin(<addr>, false). If the header is invalid and there's data
    // anywhere on the chip, it will print the contents to InfoOutput and go into read only mode to avoid accidental deletion.
    // Calling erase() works even in read only mode because it's clear the programmer intends to delete any data.
    // Use the following line to disable 
    // if(mem24.begin(eepromAddr, false) != EXIT_SUCCESS) Serial.println(F("EEPROM init failure!")); 

    // This will write "0xEE" to every byte on the chip *including the header and unused area*
    // mem24.erase(0xEE);

    // Print the data in the data block
    mem24.printMemory(Serial);

    // Print the raw contents of the chip (including header) without any address translation
    // It takes its own serial port argument so that it's independent of InfoOutput. That maximises the optimization the compiler can do.
    // The second argument is when to stop printing. Prints the whole chip by default
    // The third argument is the byte separator. Defaults to space (' '), can be any char. A value of 0 means no separator
    mem24.printRawMemory(Serial, eepromDataBlockBytes + eepromPageSize, 0);

    // Serial.println("done erasing data");
    // mem24.printRawMemory(Serial);

        // "putChanged" is recommended for anything with multiple bytes (an int, an array, a struct, etc.) 

}

void loop() {
    // while (true);

    for (uint32_t i = 0; i < 100; i++) {
        uint16_t addr = random(mem24.getUsableBytes());
        uint8_t testValue = uint8_t(random());
        mem24.put(addr, testValue);
        uint8_t readValue = 0;
        mem24.get(addr, readValue);
        if (testValue != readValue) Serial.println("Write error!");
        else Serial.print('.');
        delay(10);
    }

    mem24.printRawMemory(Serial, eepromDataBlockBytes + eepromPageSize, 0);
}
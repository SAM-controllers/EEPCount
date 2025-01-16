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

void setup() {
    Serial.begin(115200);
    Serial.println("Basic EEPROM");

    // You should usually enable info output unless you're out of flash. 
    // It has almost 0 overhead when things are *not* going wrong.
    // Look up any error codes at the top of WL_EEPROM.cpp
    mem24.setInfoOutput(&Serial);   

    // Begin (returns 0 for success)
    // mem24.begin(eepromAddr);    // Minimum required
    if (mem24.begin(eepromAddr) != EXIT_SUCCESS) Serial.println(F("EEPROM init failure!"));  // Recommended to check that begin() succeeded

    // Uncomment to overwrite every byte on the chip *including the header and unused area*
    // mem24.erase();

    // Print the data in the data block
    Serial.print("Starting memory:");
    mem24.printMemory(Serial);

    // Define some values and addresses to save
    uint32_t myInt = 0xABCDEF55;
    uint8_t myByte = 123;
    char message[] = "I'm an array!";
    uint32_t myAddress1 = 0;
    uint32_t myAddress2 = 4;
    uint32_t myAddress3 = 8;

    // === Save values === //
    // Single bytes can be read/written with write/read
    mem24.write(myAddress1, myByte);  
    // "put(<addr>, <data>)" writes data to the eeprom. It can be any data type (int, array, struct, etc.)
    uint32_t result = mem24.put(myAddress2, myInt);
    // "putChanged(<addr>, <data>)" can reduce wear by only writing the bytes that are different,
    // but it's slightly slower and needs enough ram to store a copy of the data to compare
    mem24.putChanged(myAddress3, message);


    // === Retreve values === //
    // Clear variables to be sure the values come from eeprom
    myByte = 0;
    myInt = 0;
    message[0] = 0;

    // Read from chip
    myByte = mem24.read(myAddress1);
    mem24.get(myAddress2, myInt);
    mem24.get(myAddress3, message);

    // Print the output
    Serial.print("myByte = ");
    Serial.println(myByte);
    Serial.print("myInt = ");
    Serial.println(myInt, HEX);
    Serial.print("message = ");
    Serial.println(message);

}

void loop() {
    delay(100);
}
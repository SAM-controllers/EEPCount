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
    Serial.println("Normal usage");

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
    // Get the data block size from code
    uint32_t dataBlockSize = mem24.getUsableBytes();    // You can get() and put() addresses from 0 up to (this number - 1)
    
    // Get the raw chip capacity, not just the usable capacity in the data block
    uint32_t chipBytes = mem24.getChipSizeBytes();

    // Check memory write at an address
    // This checks at a raw address (not mapped to the current data block) and puts the value back to what it was before testing
    // It will attempt to read even if the address is higher than defined chip size
    if(mem24.checkWriteErrAtAddr(chipBytes - 1) != EXIT_SUCCESS){    // EXIT_SUCCESS is defined as 0 and makes code more readable
        Serial.print("Write failure at byte addr: ");
        Serial.println(chipBytes - 1);
    }

    // Set or disable write protect pin
    // Write protect is off by default, so no need to call .setWriteProtectPin(-1) at all if not using it
    mem24.setWriteProtectPin(8);    // Set write protection on pin 8;
    mem24.setWriteProtectPin(-1);    // Turn off write protection
    

    // Uncomment to overwrite every byte on the chip *including the header and unused area*
    // A blank chip will get initialized with a block header the next time begin() is called
    // mem24.erase(0xEF);   // Overwrite with 0xEF (defaults to 0xFF)

    // chipIsBlank returns true if all bytes are the same value. (Can be 0xff, 0x00, or any single value)
    if(mem24.chipIsBlank()){
        Serial.println("This chip has no data");
    }
    else {
        // Print the data in the data block
        Serial.print("Starting memory:");
        mem24.setPrintBytesPerLine(64);     // Default is 32
        mem24.printMemory(Serial, ',');     // Print to Serial with a comma between each byte (default is space, 0 (not the char '0') means no separation)
    }


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
#include <Arduino.h>

// Define "JUMP_TO_EXAMPLE" and uncomment the example file to run
#define JUMP_TO_EXAMPLE

#ifdef JUMP_TO_EXAMPLE // Run the normal code default
// #include "../examples/basic/basic.ino"
// #include "../examples/normalUsage/normalUsage.ino"
// #include "../examples/advancedDebug/advancedDebug.ino"
#include "../examples/unitSettings/unitSettings.ino"
#endif

#ifndef JUMP_TO_EXAMPLE // Run the normal code default


#if defined(DXCORE)   // Shorthand for cctl v5 board
const uint8_t eepromAddr = 0x57;
#define DEBUG_SERIAL Serial1
#else // #ifdef ARDUINO_AVR_NANO
const uint8_t eepromAddr = 0x50;
#define DEBUG_SERIAL Serial
#endif // DEBUG

#include "debugHelperFuncs.h"
#include "WL_EEPROM.h"


const uint16_t eepromBytes = 4096;
const uint16_t eepromDataBlockBytes = 256;
const uint8_t eepromPageSize = 32;
WL_EEPROM mem24(eepromBytes, eepromDataBlockBytes, eepromPageSize);

struct BigTestStruct_t {
    uint8_t frontBytes[32];
    uint8_t testBytes1[32];
};

BigTestStruct_t ts{{0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0x11,}, {0xa, 0xb, 0xc, 0xd,0xa, 0xb, 0xc, 0xd,0xa, 0xb, 0xc, 0xd,0xa, 0xb, 0xc, 0xd,0xa, 0xb, 0xc, 0xd,0xa, 0xb, 0xc, 0xd,0xa, 0xb, 0xc, 0xd,0xa, 0xb, 0xc, 0xd,}};

// ######################
// ## Helper Functions ##
// ######################

void writeTestData0F(void) {
    // Write repeating data
    DEBUG_SERIAL.println("\nWriting data");
    uint16_t addr = 0;
    uint8_t i = 0;
    while (addr < eepromDataBlockBytes) {
        mem24.put(addr, i);
        i = (i >= 0xf) ? 0 : i + 1;   // Increment from 0-f
        addr++;
    }
}

void testStructSaving(BigTestStruct_t &bts, uint32_t addr = 0) {
    mem24.printRawMemory(DEBUG_SERIAL, 0x200);

    DEBUG_SERIAL.print(I2C_BUFFER_LENGTH_TX);
    DEBUG_SERIAL.print(F(" B tx buff, struct size: "));
    DEBUG_SERIAL.println(sizeof(bts));
    mem24.put(addr, bts);

    mem24.printRawMemory(DEBUG_SERIAL, 0x200);

    BigTestStruct_t btsRead;
    mem24.get(addr, btsRead);
    DEBUG_SERIAL.print("\r\nTestbytes: ");
    for (auto &i : btsRead.testBytes1) { DEBUG_SERIAL.print(i); DEBUG_SERIAL.print(", "); }
}

// ######################
// ## Main  Functions  ##
// ######################

void setup() {
    DEBUG_SERIAL.begin(115200);
    DEBUG_SERIAL.println("Begin Code");
    mem24.setInfoOutput(&DEBUG_SERIAL);

    bool allowOverwritingInvalidHeader = true;
    if (mem24.begin(eepromAddr, allowOverwritingInvalidHeader) != EXIT_SUCCESS) DEBUG_SERIAL.println(F("EEPROM init failure!"));
    // if(mem24.begin(eepromAddr, false) == 0) DEBUG_SERIAL.println(F("EEPROM init failure!")); 

    // mem24.erase(0xff, true);
    writeTestData0F();
    // testStructSaving(ts);



    // DEBUG_SERIAL.println("\n\n");

    mem24.printRawMemory(DEBUG_SERIAL, 0x200, 0);

    // DEBUG_SERIAL.println("Erasing data");
    // mem24.erase();

    // DEBUG_SERIAL.println("done erasing data");
    // mem24.printRawMemory(DEBUG_SERIAL);
}

void loop() {
    while (true);

    uint16_t addr = random(mem24.getUsableBytes());
    // if(mem24.checkWriteErrAtAddr(addr)) DEBUG_SERIAL.println("Write error!");   // Test with raw address
    // else DEBUG_SERIAL.print('.');

    uint8_t testValue = uint8_t(random());
    mem24.put(addr, testValue);
    uint8_t readValue = 0;
    mem24.get(addr, readValue);
    if (testValue != readValue) DEBUG_SERIAL.println("Write error!");   // Test with raw address
    else DEBUG_SERIAL.print('.');
    delay(200);
}

#endif // !JUMP_TO_EXAMPLE
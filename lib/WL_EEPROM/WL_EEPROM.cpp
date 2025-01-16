/*
  This is a library to read/write to external I2C EEPROMs.
  It uses the same template system found in the Arduino
  EEPROM library so you can use the same get() and put() functions.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.
*/

#include "WL_EEPROM.h"
#include "Arduino.h"
#include "Wire.h"

// Warnings for various things that can go wrong
// These live at the top of the .cpp file so that the internal functions can access them but they don't pollute the global namespace
enum EepWarn_t : uint8_t{
    noErr = 0,
    addrAboveChipCapacity = 1,  // The requested address is higher than the highest address on the chip
    addrAboveAvailableCapacity = 2, // The requested address is outside the specified data block size
    blockHeaderErr_InvalidStatusByte = 3,   // Block header status was not a known value
    foundBlockMarkedDamaged = 4,        // (not yet implemented)
    blockDataByteCountMismatch = 5,    // There is an otherwise valid block header with a different block size. Use erase() or Allow overwriting with begin(<addr>, true)
    readInvalidBlockSize = 6,   // The block size specified is bigger than will actually fit on the chip
    invalidBlockZeroHeader_beginAborted = 7, // The chip did not have a valid status byte at block 0 during begin() and did not overwrite existing data. Allow overwriting with begin(<addr>, true)
    block0HeaderInitWriteFail = 8,  // The stored and read info wasn't right. Perhaps write protected?
    writeBufferTruncatedToChipSize = 9, // Attempted to write to an address above the eeprom capacity. Can be caused by writing a chunk of data that *starts* in valid memory and goes past the end
    i2cWriteError = 10, // The i2c driver returned an error code while writing to the chip
    i2cChipNotFound = 11, // Call to isConnected() failed during begin(). Check address and wiring
    wlcAbacusValueInvalid = 12, // During getCounterValue 

    // Internal error debugging (These shouldn't happen from user code)
    writeTestEndStateFail = 100,
    blockHeaderTooBigForFixedOffset = 101,
    blockHeaderNotSixteenBytes = 102,   // Sanity check during development
    blockHeaderInvalidAsGenerated = 103, // Header generation/zeroing code is wrong
};

// Local definitions for code readability
#define EEP_ADDRESS_UNUSED 0 // The required address parameter doesn't matter since we aren't saving to an address
#define EEP_NO_SAVE_TO_EEPROM  false    // Update values in ram only without saving to eeprom chip

/// @brief 
/// @param memSizeBytes The total byte capacity of the chip
/// @param dataBlockBytes The usable data capacity in bytes, typically a multiple of the page size. The program should not read or write an address over this number-1. A larger data block can hold more data, but smaller blocks have less wear resistance.
/// @param pageSizeBytes The page size from the data sheet. Minimum: 8. If it's too low, redundent page clearing shortens the memory life a little. If it's too large, waiting on buffers to clear may slow down the code
/// @param i2cPort Which TwoWire interface to use
/// @note The max dataBlockBytes value is memSizeBytes - 16 bytes (which is the size of the block header), but that results in no wear leveling capability.
/// The max practical dataBlockBytes value with wear leveling is (memSizeBytes / 2) - 1 page. (Or -16 bytes if the page size is smaller than 16) The minimum practical size is 16 bytes.
/// For a 32kb memory, the max size with wear leveling is 960 bytes: (4096 / 2) - (64 byte page) = 960
WL_EEPROM::WL_EEPROM(const uint32_t memSizeBytes, const uint16_t dataBlockBytes, const uint16_t pageSizeBytes, const uint8_t addressBytes, TwoWire &i2cPort){
    settings.memorySize_bytes = memSizeBytes;
    settings.pageSize_bytes = pageSizeBytes;
    settings.addressSize_bytes = addressBytes;
    wl.blockDataBytes = dataBlockBytes;
    wl.protectExistingData = false;
    settings.i2cPort = &i2cPort;
    bytesPerLine = 32;

    wl.writesPerRotate = 100000;
    
    #ifdef EEPROM_USE_CUSTOM_DELAY
    _delay = delay; // Default to the normal delay function until overridden
    // _delayMicroseconds = delayMicroseconds; // Default to the normal delay function until overridden
    #endif
}

/// @brief 
/// @param deviceAddress 
/// @param allowInitToOverwrite 
/// @return Error code. 0 = no error, init successful. >= 1: see EepWarn_t
uint8_t WL_EEPROM::begin(uint8_t deviceAddress, bool allowInitToOverwrite)
{
    settings.deviceAddress = deviceAddress;
    if (isConnected() == false){ printWarning(i2cChipNotFound); return i2cChipNotFound; }

    // Read the first data block header (always at address 0)
    BlockHeader_t header0;
    this->_rawget(0, header0);

    // bool writeBlankHeader = false; 
    if(header0.blockStatus == BlockStatus_t::Uninitialized){
        if(chipIsBlank()){
            if(infoSerial) infoSerial->println(F("chip is blank, auto writing header"));
            writeBlockHeader(0, BlockStatus_t::CurrentValidBlock);
            // ToDo: locate and write the other headers for higher blocks on initialization

            this->_rawget(0, header0);// Read the newly initialized block header into current memory for validation
        } 
    }

    // Abort if there are errors found and overwriting is not allowed
    uint8_t headerErrsFound = validateBlockHeader(header0);
    if(headerErrsFound){

        // Note: if user code does not call begin(<addr>, false), this check should be optimized away by the compiler
        if( (allowInitToOverwrite == false) && (chipIsBlank() == false) ){
            // Check if the whole chip is blank. Allow init in that case
            printWarning(invalidBlockZeroHeader_beginAborted);
            if(infoSerial){
                // Store the long warning in flash once
                const __FlashStringHelper* warnString = F("\r\nHeader Err! Save needed data, then erase");
                infoSerial->println(warnString);
                printRawMemory(*infoSerial);
                infoSerial->println(warnString);
            } 

            wl.protectExistingData = true;  // Treat the chip with unknown data as read only
            // settings.i2cPort = nullptr; // (Removed for being drastic) Throw away this library's access to the i2c bus to be ABSOLUTELY sure nothing gets overwritten if write() or put() are called from user code
            return false;
        }

        // Initialize (overwrite) if there isn't a valid block0 header
        writeBlockHeader(0, BlockStatus_t::CurrentValidBlock);
    
    }

    // Load the counter struct
    // wl.counterCount = this->readCounterValue(wlcAddr);   // ToDo: Rewrite to get counter from header block
    this->findCurrentBlock();

    if(wl.blockHeaderSize != 16) printWarning(blockHeaderNotSixteenBytes); // Several functions assume that the header block is 16 bytes during development

    return noErr;
}

/// @brief Determine if a data block is valid based on its header
/// @param h A reference to the block header data to check
/// @return 0 = Valid, >=1 is an error
uint8_t WL_EEPROM::validateBlockHeader(BlockHeader_t &h){
    uint8_t errsFound = 0;

    // Determine based on status byte first since that's easiest
    switch(h.blockStatus){
        case(BlockStatus_t::Available ):
        case(BlockStatus_t::CurrentValidBlock ):
        // case(BlockStatus_t::TransitioningFromCurrentValid ):
        case(BlockStatus_t::UnvalidatedNextDestination ):
        case(BlockStatus_t::UsedUndamaged ):
            // No error found yet
            break;
        case(BlockStatus_t::Damaged ):
            printWarning(foundBlockMarkedDamaged);  // Print a warning, but the state is valid
            break;
        case(BlockStatus_t::Uninitialized ):    // Print a warning. This should be handled by begin() for block 0
            printWarning(foundBlockMarkedDamaged);
            errsFound++;
            break;
        default:
            printWarning(blockHeaderErr_InvalidStatusByte);
            errsFound++;
            break;
    }

    // Check the size
    // This will not pass uninitialized chips when mem size < 0xFFFF (65535)
    if(h.blockSizeBytes > settings.memorySize_bytes){
        // printWarning(readInvalidBlockSize);  // Can (falsely) trigger on blank chips
        errsFound++;
    }

    if( (h.blockSizeBytes != wl.blockDataBytes) && (h.blockSizeBytes != 0xFFFF) ){
        errsFound++;
    }

    // if(errsFound) printWarning(invalidBlockHeaderFound); // Removed. Add specific warnings to each test above
    return errsFound;
}

/// @brief Set a pin for (or disable) hardware write enable control
/// @param pin A pin number from 0-254. Pin -1 or >=255 disable write protection
/// @todo Unhandled case of a write protect pin being previously set, then cleared, leaving the chip in a read only state
void WL_EEPROM::setWriteProtectPin(int16_t pin){
    if( (pin >= 0) && (pin < WRITE_PIN_DISABLED))
    {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, HIGH);
        settings.wpPin = pin;
    }
    else pin = WRITE_PIN_DISABLED; // Original lib used 255 for not enabled
}

/// @brief Overwrite the data bytes on the chip with the given value
/// @param valueToWrite What value to write to each byte (255 causes the least wear)
void WL_EEPROM::erase(uint8_t valueToWrite) //, bool noHeader, bool resetWriteCount)
// Parameters to add:
// @param noHeader True = erase every physical byte on the chip, False = just erase the data bytes in the data blocks, not the header or padding space 
// @param resetWriteCount True = reset block header to "available" and the block write count to 0, false = leave block status and write count as is
{
    wl.protectExistingData = false; // If the user calls erase, they clearly intend to overwrite the data, so remove write protection
    uint16_t writeChunkSize = min(uint32_t(settings.pageSize_bytes), uint32_t(I2C_BUFFER_LENGTH_TX));   // Write chuck length is limited by page size and buffer size
    uint8_t tempBuffer[writeChunkSize];
    for (uint32_t x = 0; x < writeChunkSize; x++)  // Prepare a data struct for efficient writes
        tempBuffer[x] = valueToWrite;

    // if(resetWriteCount == false) this->readCounterValue(wlcAddr);   // Read into MCU mem to rewrite after erase

    // Perform the erase chunk by chunk
    for (uint32_t addr = 0; addr < chipMaxAddr(); addr += writeChunkSize){
        _rawwrite(addr, tempBuffer, writeChunkSize);
        this->incrementCounter(_wlc, 0, false, true); // Only update the counter in ram, don't save to eeprom each time for performance reasons
    }

    // if(resetWriteCount == true) zeroCounterStruct(_wlc);    // Clear the count before rewriting if told to
    // else this->incrementCounter(_wlc, wlcAddr);
    // this->put(wlcAddr, _wlc);
}

// Returns true if device is detected
bool WL_EEPROM::isConnected(uint8_t i2cAddress)
{
    if (i2cAddress == 255)
        i2cAddress = settings.deviceAddress; // We can't set the default to settings.deviceAddress so we use 255 instead

    settings.i2cPort->beginTransmission((uint8_t)i2cAddress);
    if (settings.i2cPort->endTransmission() == 0)
        return (true);
    return (false);
}

// Returns true if device is not answering (currently writing)
// Caller can pass in an I2C address. This is helpful for larger EEPROMs that have two addresses (see block bit 2).
// Caution: using "while(eeprom.isBusy())" will hang if the eeprom looses connection or the i2c bus goes down
bool WL_EEPROM::isBusy(uint8_t i2cAddress)
{
    if (i2cAddress == 255)
        i2cAddress = settings.deviceAddress; // We can't set the default to settings.deviceAddress so we use 255 instead

    if (isConnected(i2cAddress))
        return (false);
    return (true);
}

// Get the number of bytes available to read or write from user code
uint32_t WL_EEPROM::getUsableBytes(){    return wl.blockDataBytes;}    
void WL_EEPROM::setPageSizeBytes(uint16_t pageSize){    settings.pageSize_bytes = pageSize;}
uint16_t WL_EEPROM::getPageSizeBytes(){    return settings.pageSize_bytes;}
void WL_EEPROM::setWriteTimeMs(uint8_t writeTimeMS){    settings.writeTime_ms = writeTimeMS;}
uint8_t WL_EEPROM::getWriteTimeMs(){    return settings.writeTime_ms;}
void WL_EEPROM::setAddressBytes(uint8_t addressBytes){    settings.addressSize_bytes = addressBytes;}
uint8_t WL_EEPROM::getAddressBytes(){    return settings.addressSize_bytes;}

/// @brief Check that writing to a byte to a given address on the chip does not fail
/// @param rawByteAddress 
/// @param checkValue 
/// @return 0 == success, >= 1 means failure
uint8_t WL_EEPROM::checkWriteErrAtAddr(uint16_t rawByteAddress, uint8_t checkValue){
    // This should attempt to read even if the address is higher than defined chip size
    uint8_t origValue;
    uint8_t tmpValue;
    uint8_t errFound = false;
    this->_rawget(rawByteAddress, origValue);

    // Write to 0xff since that's the first step of "erasing" a byte anyway
    uint8_t bytesToWrite[] = {0xff, checkValue, 0x00, origValue};

    for(uint8_t &i : bytesToWrite){
        this->_rawput(rawByteAddress, i);
        this->_rawget(rawByteAddress, tmpValue);
        if(tmpValue != i) errFound = true;
    }

    if(!errFound){
    this->_rawget(rawByteAddress, tmpValue);
    if(tmpValue != origValue) printWarning(writeTestEndStateFail);  // Didn't set the byte back to its starting value
    }

    return errFound;
}


// Determines the number of address bytes to complete a successful write
// Returns 1 or 2
// Sets the internal setting
uint8_t WL_EEPROM::detectAddressBytes()
{
    uint8_t testLocation = 1;

    // Create copy of internal settings before test
    uint32_t originalMemorySize = settings.memorySize_bytes;
    uint16_t originalPageSize = settings.pageSize_bytes;

    // Read and store before we start (potentially) writing
    // This will fail on two byte address EEPROMs when the memory size is below 4096 bytes
    uint8_t locationValueOriginal = read(testLocation);

    setMemorySizeBytes(128); // Assume the smallest memory size during test
    setPageSizeBytes(1);     // Assume a page size

    uint8_t addressBytes = 1;
    for (; addressBytes < 3; addressBytes++)
    {
        setAddressBytes(addressBytes); // Start test at one byte

        // Avoid the default state of 0xFF = 255 and 0. Assumes user has randomSeed()ed something.
        // Do not use the original value
        // Do not use the value found in the next location either
        uint8_t magicValue = 0;
        do
        {
            magicValue = random(1, 255); // (Inclusive, exclusive)
        } while (magicValue == locationValueOriginal);

        // Serial.print(" writing: 0x");
        // Serial.print(magicValue, HEX);

        // Write this new value
        write(testLocation, magicValue); // Will use a 1 or 2 byte address depending on setMemorySizeBytes()

        // Read data back
        uint8_t locationValue = read(testLocation);
        // Serial.print(" read: 0x");
        // Serial.print(locationValue, HEX);
        // Serial.print(" - ");

        if (locationValue == magicValue)
        {
            // Successful write. We've determined the number of address bytes
            break;
        }
    }

    // Serial.println("\r\nWrite success. One byte addressing.");
    //  Return spot to its original value
    write(testLocation, locationValueOriginal);

    // Return original settings
    settings.memorySize_bytes = originalMemorySize;
    settings.pageSize_bytes = originalPageSize;

    // Error check
    if (addressBytes >= 3)
        addressBytes = 1; // If we failed, 1 guarantees we won't corrupt data with two byte address writes

    settings.addressSize_bytes = addressBytes;
    return (settings.addressSize_bytes);
}

// Read a byte from a given location
uint8_t WL_EEPROM::read(uint32_t eepromLocation)
{
    eepromLocation = this->addrToHwAddr(eepromLocation);    // Address translation
    uint8_t tempByte;
    _rawread(eepromLocation, &tempByte, 1);
    return tempByte;
}


// Bulk read from EEPROM
// Handles breaking up read amt into 32 byte chunks (can be overriden with setI2CBufferSize)
// Handles a read that straddles the 512kbit barrier
int WL_EEPROM::read(uint32_t eepromLocation, uint8_t *buff, uint16_t bufferSize)
{
    eepromLocation = this->addrToHwAddr(eepromLocation);    // Address translation
    return _rawread(eepromLocation, buff, bufferSize);
}

int WL_EEPROM::_rawread(uint32_t eepromLocation, uint8_t *buff, uint16_t bufferSize)
{
    #ifdef EEPROM_PRINT_READWRITE_INFO
    if(infoSerial != nullptr){
        infoSerial->print("|r(");
        infoSerial->print(eepromLocation, HEX);
        infoSerial->print(",");
        infoSerial->print(bufferSize);
        infoSerial->print(" B)");
    }
    #endif

    int i2cReturnCode = 0;

    uint16_t received = 0;
    while (received < bufferSize)
    {
        // Limit the amount to write to a page size
        uint16_t amtToRead = bufferSize - received;
        if (amtToRead > I2C_BUFFER_LENGTH_RX) // Arduino I2C buffer size limit
            amtToRead = I2C_BUFFER_LENGTH_RX;

        // Check if we are dealing with large (>512kbit) EEPROMs
        uint8_t i2cAddress = settings.deviceAddress;
        if (settings.memorySize_bytes > 0xFFFF)
        {
            // Figure out if we are going to cross the barrier with this read
            if (eepromLocation + received < 0xFFFF)
            {
                if (0xFFFF - (eepromLocation + received) < amtToRead) // 0xFFFF - 0xFFFA < 32
                    amtToRead =
                        0xFFFF - (eepromLocation + received); // Limit the read amt to go right up to edge of barrier
            }

            // Figure out if we are accessing the lower half or the upper half
            if (eepromLocation + received > 0xFFFF)
                i2cAddress |= 0b100; // Set the block bit to 1
        }
        // Check if we are dealing with 24LC04/08/16 (512, 1024, and 2048 bytes)
        // These use a single address byte but change the I2C address
        else if (settings.memorySize_bytes >= 512 && settings.memorySize_bytes <= 2048)
        {
            // Set I2C Address bits (A2/A1/A0) accordingly
            i2cAddress |= ((eepromLocation + received) >> 8);
        }

        #ifdef EEPROM_DELAY_BEFORE_WRITE_POLLING
        _delay(settings.writeTime_ms);
        #endif // EEPROM_DELAY_BEFORE_WRITE_POLLING

        // See if EEPROM is available or still writing a previous request
        while (isBusy(settings.deviceAddress) == true) // Poll device's original address, not the modified one
            _delay(1); // original: _delayMicroseconds(100); // This shortens the amount of time waiting between writes but hammers the I2C bus

        settings.i2cPort->beginTransmission(i2cAddress);
        if (settings.addressSize_bytes > 1)
            settings.i2cPort->write((uint8_t)((eepromLocation + received) >> 8)); // MSB
        settings.i2cPort->write((uint8_t)((eepromLocation + received) & 0xFF));   // LSB

        i2cReturnCode = settings.i2cPort->endTransmission();

        settings.i2cPort->requestFrom((uint8_t)i2cAddress, (size_t)amtToRead);

        for (uint16_t x = 0; x < amtToRead; x++)
            buff[received + x] = settings.i2cPort->read();

        received += amtToRead;
    }

    return (i2cReturnCode);
}

// Write a byte to a given location
int WL_EEPROM::write(uint32_t eepromLocation, uint8_t dataToWrite)
{
    #ifdef EEPROM_PRINT_READWRITE_INFO
    if(infoSerial != nullptr){ infoSerial->print("_W1_");}
    #endif
    
    eepromLocation = this->addrToHwAddr(eepromLocation);    // Address translation
    return _rawwrite(eepromLocation, &dataToWrite, 1);
}


// Write a buffer to the eeprom
// Limits writes to the I2C buffer size (default is 32 bytes)
// Silently truncates data if larger than buffer or I2C_BUFFER_LENGTH_TX or memorySize_bytes
// Returns the i2cReturnCode of the I2C endTransmission
int WL_EEPROM::write(uint32_t eepromLocation, const uint8_t *dataToWrite, uint16_t bufferSize)
{
    #ifdef EEPROM_PRINT_READWRITE_INFO
    if(infoSerial != nullptr){
        infoSerial->print("\n_Wx_");
    }
    #endif

    eepromLocation = this->addrToHwAddr(eepromLocation);    // Address translation
    
    return _rawwrite(eepromLocation, dataToWrite, bufferSize);
}

int WL_EEPROM::_rawwrite(uint32_t eepromLocation, const uint8_t *dataToWrite, uint16_t bufferSize)
{
    if(wl.protectExistingData == true) return -1;  // Don't perform writes after an aborted begin()
    int i2cReturnCode = noErr;

    #ifdef EEPROM_PRINT_READWRITE_INFO
    if(infoSerial != nullptr){
        infoSerial->print("|w(");
        infoSerial->print(eepromLocation, HEX);
        if(bufferSize > 1){
            infoSerial->print(",");
            infoSerial->print(bufferSize);
            infoSerial->print(" B");
        }
        infoSerial->print(')');

        // // Print the actual bytes
        // infoSerial->print('<');
        // for (size_t i = 0; i < bufferSize; i++) {
        //     infoSerial->print(*(dataToWrite+i), HEX);
        //     infoSerial->print(',');
        // }
        // infoSerial->print("\b>");   // Backspace the last comma
    }
    #endif

    // Write beyond memory size error check
    if (eepromLocation + bufferSize > settings.memorySize_bytes){
        bufferSize = settings.memorySize_bytes - eepromLocation;
        printWarning(writeBufferTruncatedToChipSize);
    }

    // Serial.print("bufferSize: ");
    // Serial.println(bufferSize);

    int16_t maxWriteSize = settings.pageSize_bytes;
    if (maxWriteSize > I2C_BUFFER_LENGTH_TX - settings.addressSize_bytes)
        maxWriteSize =
            I2C_BUFFER_LENGTH_TX -
            settings.addressSize_bytes; // Arduino has 32 byte limit. We loose 1 or 2 bytes to the EEPROM address

    // Serial.print("maxWriteSize: ");
    // Serial.println(maxWriteSize);

    // Break the buffer into page sized chunks
    uint16_t recorded = 0;
    while (recorded < bufferSize)
    {
        // Limit the amount to write to either the page size or the Arduino limit of 30
        int amtToWrite = bufferSize - recorded;

        // Serial.print("amtToWrite: ");
        // Serial.println(amtToWrite);

        if (amtToWrite > maxWriteSize)
            amtToWrite = maxWriteSize;

        // Serial.print("amtToWrite: ");
        // Serial.println(amtToWrite);

        if (amtToWrite > 1)
        {
            // Check for crossing of a page line. Writes cannot cross a page line.
            uint16_t pageNumber1 = (eepromLocation + recorded) / settings.pageSize_bytes;
            uint16_t pageNumber2 = (eepromLocation + recorded + amtToWrite - 1) / settings.pageSize_bytes;
            if (pageNumber2 > pageNumber1)
                amtToWrite = ((pageNumber1 + 1) * settings.pageSize_bytes) -
                             (eepromLocation + recorded); // Limit the write amt to go right up to edge of page barrier
        }

        uint8_t i2cAddress = settings.deviceAddress;
        // Check if we are dealing with large (>512kbit) EEPROMs
        // These use two address bytes and a B0 'block' bit
        if (settings.memorySize_bytes > 0xFFFF)
        {
            // Figure out if we are accessing the lower half or the upper half
            if (eepromLocation + recorded > 0xFFFF)
                i2cAddress |= 0b100; // Set the block bit to 1
        }
        // Check if we are dealing with 24LC04/08/16 (512, 1024, and 2048 bytes)
        // These use a single address byte but change the I2C address
        else if (settings.memorySize_bytes >= 512 && settings.memorySize_bytes <= 2048)
        {
            // Set I2C Address bits (A2/A1/A0) accordingly
            i2cAddress |= ((eepromLocation + recorded) >> 8);
        }

        #ifdef EEPROM_DELAY_BEFORE_WRITE_POLLING
        _delay(settings.writeTime_ms);
        #endif // EEPROM_DELAY_BEFORE_WRITE_POLLING

        // See if EEPROM is available or still writing a previous request
        while (isBusy(settings.deviceAddress) == true) // Poll device's original address, not the modified one
            _delay(1); // original: _delayMicroseconds(100); // This shortens the amount of time waiting between writes but hammers the I2C bus

        // Check if we are using Write Protection then disable WP for write access
        if(settings.wpPin != WRITE_PIN_DISABLED ) digitalWrite(settings.wpPin, LOW);

        settings.i2cPort->beginTransmission(i2cAddress);
        if (settings.addressSize_bytes > 1) // Device larger than 16,384 bits have two byte addresses
            settings.i2cPort->write((uint8_t)((eepromLocation + recorded) >> 8)); // MSB
        settings.i2cPort->write((uint8_t)((eepromLocation + recorded) & 0xFF));   // LSB

        for (int16_t x = 0; x < amtToWrite; x++)    // Write the bytes to EEPROM one at a time
            settings.i2cPort->write(dataToWrite[recorded + x]);

        i2cReturnCode = settings.i2cPort->endTransmission(); // Send stop condition
        if(i2cReturnCode != noErr) {
            printWarning(i2cWriteError);
            // if(infoSerial){
            //     infoSerial->printf("(code %i)", i2cReturnCode);
            // }

        }

        recorded += amtToWrite;

        // Serial.print("recorded: ");
        // Serial.println(recorded);

        // this->incrementCounter(wlcAddr, false); // ToDo: Uncomment after tests

        #ifdef EEPROM_DELAY_BEFORE_WRITE_POLLING
        _delay(settings.writeTime_ms);  // Delay long enough to write a page
        #endif // EEPROM_DELAY_BEFORE_WRITE_POLLING

        // Enable Write Protection if we are using WP
        if(settings.wpPin != WRITE_PIN_DISABLED) digitalWrite(settings.wpPin, HIGH);
    }

    // if(i2cReturnCode == noErr) this->incrementCounter(wlcAddr, false); // Save the new count value to chip once at the end // Does this need a "noUpdateWriteCount" flag? // ToDo: Uncomment after tests

    return (i2cReturnCode);
}

// ###########################
// ## Wear leveling counter ##
// ###########################

/// @brief Calculate the current counter value of a wlc already in memory
/// @param wlc Reference to the counter struct
/// @return The current count
uint32_t WL_EEPROM::getCounterValue(WearLevelCounter_t &wlc){
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
bool WL_EEPROM::countIsInvalid(WearLevelCounter_t &wlc){
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
/// @param addressIsRaw 
/// @return 0 = EXIT_SUCCESS with valid data. 1 = Loaded data is invalid. 2+ = other error
uint8_t WL_EEPROM::loadCounterValue(WearLevelCounter_t &wlc, uint32_t startAddr, bool addressIsRaw){
    if(addressIsRaw == false) { startAddr = this->addrToHwAddr(startAddr); }
    this->_rawget(startAddr, wlc);
    return countIsInvalid(wlc); // Return the result of a validity check
}

/// @brief Load the data from a counter struct from EEPROM into memory, then set the count in wlco.currentCount
/// @param wlco 
/// @return 0 = EXIT_SUCCESS with valid data. 1 = Loaded data is invalid. 2+ = other error
uint8_t WL_EEPROM::loadCounterValue(WearLevelCounterObject_t &wlco) { 
    uint8_t loadReturnCode = loadCounterValue(wlco.wlc, wlco.addr, false); 
    if(loadReturnCode == EXIT_SUCCESS){
        wlco.count = getCounterValue(wlco.wlc);
    }
    return loadReturnCode;
}

void WL_EEPROM::saveCounterValue(WearLevelCounter_t &wlc, uint32_t startAddr, bool addressIsRaw){
    if(addressIsRaw == false) { startAddr = this->addrToHwAddr(startAddr); }
    this->_rawputChanged(startAddr, wlc);
}

/// @brief Checks for an existing valid count on eeprom chip and initializes count to 0 if not found
/// @param wlco 
/// @return Return code: 
/// 0 = Already existing valid counter loaded (EXIT_SUCCESS). 
/// 1 = Loaded data that was not a counter, successfully initialized count to 0. 
/// 2 = All bytes were 0xff (255) which is technically valid, but almost certainly uninitialized. Initialized count to 0. 
/// @todo Address valid/in range, raw address option
uint8_t WL_EEPROM::loadOrInitCounter(WearLevelCounterObject_t &wlco){
    // ToDo: Optional bool addressIsRaw

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
/// @note If saveToEeprom is false, power loss or calling readCounterValue() will cause the count to reset to the last saved value.
/// @return new counter count
uint32_t WL_EEPROM::incrementCounter(WearLevelCounterObject_t &wlco, bool saveToEeprom){
    wlco.count++;
    return incrementCounter(wlco.wlc, wlco.addr, saveToEeprom, false); 
}

/// @brief Add one to the counter count
/// @param startAddr Address of the counter struct
/// @param saveToEeprom Normally true. If false, update in ram only, useful for when lots of increments happen or in time critical code.
/// @note If saveToEeprom is false, power loss or calling readCounterValue() will cause the count to reset to the last saved value.
/// @return new counter count
uint32_t WL_EEPROM::incrementCounter(WearLevelCounter_t &countStruct, uint32_t startAddr, bool saveToEeprom, bool addressIsRaw){
    if(addressIsRaw == false) { startAddr = this->addrToHwAddr(startAddr); }
    // ToDo: Check status and load the current count if it hasn't already been loaded
    
    bool carryFlag = false;
    
    // === Singles count (Abacus0) === //
    if(countStruct.abacus0 > 0){
        countStruct.abacus0 /= 2;  // Set the leftmost 1 bit to 0 (normal decrement)
    }
    else {
        countStruct.abacus0 = 0xffffffff;  // Reset count and set carry
        carryFlag = true;
    }

    // === Abacus 1 === //
    if(carryFlag){
        carryFlag = false;
        if(countStruct.abacus1 > 0) {   // Normal decrement
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

    // === Fours === //
    // Note: it should not be physically possible to roll over the uint2 place before the abacus0 cells wear out
    // They would have had to count over 6 billion increments for this to roll over
    if(carryFlag){
        countStruct.uint2 += 1;
    }

    // Write the updated information to eeprom
    if(saveToEeprom) this->_rawputChanged(startAddr, countStruct);

    return true;
}

// Reset a counter to 0 or initialize a previously unused part of memory
uint32_t WL_EEPROM::resetCounter(WearLevelCounter_t &wlc, uint32_t startAddr, bool addressIsRaw){
    if(addressIsRaw == false) { startAddr = this->addrToHwAddr(startAddr); }
    // if(infoSerial != nullptr){
    //     infoSerial->print("<Resetting counter>");
    // }
    this->zeroCounterStruct(wlc);
    this->_rawputChanged(startAddr, wlc);
    return 0;
}

uint32_t WL_EEPROM::resetCounter(WearLevelCounterObject_t &wlco){
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
uint8_t WL_EEPROM::setCounterValue(WearLevelCounter_t &wlc, uint32_t newCount, uint32_t addr, bool saveToEeprom){
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
uint8_t WL_EEPROM::setCounterValue(WearLevelCounterObject_t &wlco, uint32_t newCount, bool saveToEeprom){
    wlco.count = newCount;
    return setCounterValue(wlco.wlc, newCount, wlco.addr, saveToEeprom);
}

// Initialize a struct in memory (NOT ON THE EEPROM CHIP) to a count of 0
void WL_EEPROM::zeroCounterStruct(WearLevelCounter_t &wlc){
    wlc = {.abacus0 = 0xffffffff, .abacus1 = 0xff, .uint0 = 0, .uint1 = 0, .uint2 = 0};
}

/// @brief Print the data in memory for a counter object. If you want to be sure it's what's on the chip, call saveCounterValue() and loadCounterValue() before printing
/// @param s Output serial port
/// @param wlco The object to print
void WL_EEPROM::printCounterStruct(HardwareSerial &s, WearLevelCounterObject_t &wlco){
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


// #####################################
// ## Wear leveling Address Functions ##
// #####################################

/// @brief Internal mechanism to automatically warn the user of problems
/// @param errNum Which warning from EepWarn_t to present
/// @note It is safe to call this function at any time. No need to check that infoSerial != nullptr.
/// @note The user needs to call setInfoOutput() for this to print anything.
void WL_EEPROM::printWarning(uint8_t errNum){
    if(infoSerial != nullptr){
        infoSerial->print("!WARN#");
        infoSerial->print(errNum);
        infoSerial->print("! ");
    }
}

void WL_EEPROM::printBlockHeader(HardwareSerial &s){
    BlockHeader_t tmp;
    this->get(0, tmp);  // Hard coded to block 0
    s.print("\r\nBlkHeader: ");
    uint8_t copyBuf[sizeof(tmp)];
    memcpy(&tmp, copyBuf, sizeof(tmp));
    for(auto &i : copyBuf){
        s.print(i, HEX);
    }
    s.println();
}

// Print the contents of current the data block
void WL_EEPROM::printMemory(HardwareSerial &s, char byteSeparator){
    printRawMemory(s, wl.addrOffset+wl.blockDataBytes, byteSeparator, wl.addrOffset, wl.addrOffset);
}

/// @brief Print the actual contents of the memory chip at it is on the physical chip, ignoring any address translation
/// @param s The serial port to use
/// @param maxAddr Stop after this address. Default (0) is print all memory
/// @param byteSeparator A character to print between each byte of memory. 0 = no separator, ' ' = space separator, ',' = comma separator
/// @note This function is available from user code even when infoSerial is not defined, which is why it takes a serial reference rather than using infoSerial
void WL_EEPROM::printRawMemory(HardwareSerial &s, uint32_t maxAddr, char byteSeparator){
    printRawMemory(s, maxAddr, byteSeparator, 0, 0);
}

void newlineAndAddress(HardwareSerial &s, uint32_t addr){
    s.println(); 
    s.print("0x");
    s.print(addr, HEX);
    s.print('\t');
}

/// @brief Main memory printing function with all parameters available
/// @param s The serial port to use
/// @param maxAddr Stop after this address. Default (0) is print all memory
/// @param byteSeparator A character to print between each byte of memory. 0 = no separator, ' ' = space separator, ',' = comma separator
/// @param startAddr 
/// @param addrPrintOffset 
void WL_EEPROM::printRawMemory(HardwareSerial &s, uint32_t maxAddr, char byteSeparator, uint32_t startAddr, uint32_t addrPrintOffset){

    HardwareSerial *origSerial = infoSerial;
    infoSerial = nullptr;   // Suppress read and write output during dump
    uint32_t printAddress = startAddr - addrPrintOffset;
    uint8_t tmpBuf[settings.pageSize_bytes]{0};
    uint32_t addr = startAddr;
    // const uint16_t bytesPerLine = 64;   // Independent of page size
    const uint16_t nByteExtraSpace = 16;
    s.print("\nDump at t = ");
    s.print(millis());
    // s.print(", write count = ");
    // s.println(wl.counterCount);
    // s.print("\t\t");
    // for (uint16_t i = 0; i < bytesPerLine; i++) {
    //     /* code */
    // }
    
    s.print("\r\n0x00\t");
    if(maxAddr==0) maxAddr = settings.memorySize_bytes;    // Don't change maxAddr unless 0

    uint16_t lineByteCount = 0;

    while(addr < maxAddr){
        this->_rawread(addr, tmpBuf, settings.pageSize_bytes);
        for(auto &i : tmpBuf){
            if(i<0x10) s.print('0');    // Fill 10's space if byte value doesn't
            s.print(i, HEX);
            if(byteSeparator) s.print(byteSeparator);       // Char between each byte (if byteSeparator != 0)
            lineByteCount++;
            printAddress++;
            if(lineByteCount % nByteExtraSpace == 0) s.print(' ');  
            if(lineByteCount >= bytesPerLine){  // Newline and next address
                newlineAndAddress(s, printAddress);
                lineByteCount = 0;
            } 
        }
        addr+= settings.pageSize_bytes;

        #ifdef EEPROM_USE_CUSTOM_DELAY
        _delay(1);  // Add a tiny delay to serve the same purpose as "yield()"
        #endif
    }
    s.print("end\r\n");
    infoSerial = origSerial;    // Restore info output
}

// For debugging, gets compiled away unless called
void WL_EEPROM::printBlockInfo(HardwareSerial &s){
    s.print("Block info: writeCount= (bogus until implemented)"); s.print(wl.counterCount);
    s.print(", currentBlock="); s.print(wl.currentBlockIndex);
    s.print(", blockDataSize="); s.print(wl.blockDataBytes);
    s.print(", Total size w/hdr="); s.print(wl.blockDataBytes + wl.blockHeaderSize);
    s.print(", addr offset="); s.print(getOffset());
    s.println();
    // s.print(""); s.print(wl.);
}

// Convert a logical address to the hardware address of that data in the EEPROM with wear leveling offset
/// @todo only works when EEPROM_DISABLE_BLOCK_SHIFT is defined as currently written
uint32_t WL_EEPROM::addrToHwAddr(uint32_t addr){
    // return addr;    // Uncomment to disable address translation and make all addresses raw addresses
    
    // #ifdef EEPROM_DISABLE_BLOCK_SHIFT   // Commented out until ready for block switching
    return addr + wl.addrOffset;  // Page 0 used for write counter, with rotation disabled the offset is always 1 page
    // #else // If block shift is enabled  // Commented out until ready for block switching
    #ifdef EEPROM_PRINT_READWRITE_INFO
    uint32_t newAddr = addr + wl.addrOffset;
    if(infoSerial != nullptr){
        infoSerial->print("[");
        infoSerial->print(addr, HEX);
        infoSerial->print("->");
        infoSerial->print(newAddr, HEX);
        infoSerial->print("]");
    }
    #endif // EEPROM_PRINT_READWRITE_INFO
    // return newAddr; // Commented out until ready for block switching
    // #endif // EEPROM_DISABLE_BLOCK_SHIFT    // Commented out until ready for block switching
}

/// @brief Set addrOffset and currentBlockIndex
/// @note This function always sets the current block to block 0 until block switching is enabled
uint8_t WL_EEPROM::findCurrentBlock(){
    // #ifdef EEPROM_DISABLE_BLOCK_SHIFT    // Commented out until ready for block switching
    uint32_t addrOffsetToSet = wl.blockHeaderSize;

    #ifdef PAD_TO_PAGE_SIZE
    addrOffsetToSet = max(uint16_t(wl.blockHeaderSize), uint16_t(settings.pageSize_bytes));
    #endif // PAD_TO_PAGE_SIZE

    wl.addrOffset = addrOffsetToSet;    // Valid only while currentBlockIndex is always 0 (no switching)
    wl.currentBlockIndex = 0;
    return 0;
    // #else // If block shift is enabled
    // *Block finding logic goes here*
    // wl.addrOffset = (wl.currentBlockIndex * (blockSize + wl.blockHeaderSize)) + wl.blockHeaderSize;    // Eventual implementation when currentBlockIndex != 0
    // #endif // EEPROM_DISABLE_BLOCK_SHIFT
}

/// @brief Initialize a block header and write to chip
/// @param addr Raw address of header byte 0
/// @param status What status to initialize with. Typtically BlockStatus_t::Available
void WL_EEPROM::writeBlockHeader(uint32_t addr, BlockStatus_t status){
    BlockHeader_t header0;
        header0.blockSizeBytes = wl.blockDataBytes;
        header0.currentBlock  = 0;
        header0.blockStatus = status;
        for(uint8_t &i : header0.reserved) i=0xff;
        zeroCounterStruct(header0.blockWLC);
        if(validateBlockHeader(header0)) printWarning(blockHeaderInvalidAsGenerated);
        _rawput(addr, header0);
        while(this->isBusy()) {delay(10);}
        _rawget(addr, header0);
        if(validateBlockHeader(header0)) printWarning(block0HeaderInitWriteFail);
}

// Returns the address of data byte 0 in the current block
uint32_t WL_EEPROM::getOffset(){
    return wl.addrOffset;
}

/// @brief Enable info/error output from this library to a serial port
/// @param serialPort Where to print output. Set to 'nullptr' to disable output
/// @note It is recommended to call this before begin() to get any initialization errors
/// @remark Unless this function is called, the printing code gets optimized away by the compiler, so it's easy to turn on and off for production/testing
void WL_EEPROM::setInfoOutput(HardwareSerial *serialPort){
    infoSerial = serialPort;
}

/// @brief Returns true if all bytes of a chip have the same value
/// @return False = chip has data, True = all bytes same value
bool WL_EEPROM::chipIsBlank(){
    // if(infoSerial) infoSerial->println(F("Starting search"));
    uint8_t byte0;
    _rawget(0, byte0);

    uint16_t readChunkSize = min(uint16_t(settings.pageSize_bytes), uint16_t(I2C_BUFFER_LENGTH_RX));   // Read chuck length is limited by page size and buffer size
    uint8_t tempBuffer[readChunkSize];

    // Multi byte version
    for (uint32_t addr = 0; addr <= chipMaxAddr(); addr += readChunkSize){
        readChunkSize = min(uint32_t(readChunkSize), uint32_t(chipMaxAddr()-addr));   // Don't read past the end of the chip
        _rawread(addr, tempBuffer, readChunkSize);
        for(uint8_t &val : tempBuffer) {if(val != byte0) return 0;}
    }

    // if(infoSerial) infoSerial->println(F("Chip is blank"));
    return true;
}
/*
    #ifdef EEPROM_DISABLE_BLOCK_SHIFT
    #else // If block shift is enabled
    #endif // EEPROM_DISABLE_BLOCK_SHIFT
    */
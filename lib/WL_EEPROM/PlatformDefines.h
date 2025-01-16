// This file contains definitions that depend on the archetecture or platform being compiled for

#ifndef PLATFORMDEFINES_H_
#define PLATFORMDEFINES_H_

// === I2C Buffer lengths === //
#if defined(ARDUINO_ARCH_APOLLO3)

#define I2C_BUFFER_LENGTH_RX                                                                                           \
    256 // Hardcoding until issue is resolved: https://github.com/sparkfun/Arduino_Apollo3/issues/351
#define I2C_BUFFER_LENGTH_TX 256

#elif defined(__AVR_ATmega328P__) || defined(__AVR_ATmega168__)

#define I2C_BUFFER_LENGTH_RX BUFFER_LENGTH // I2C_BUFFER_LENGTH is defined in Wire.H
#define I2C_BUFFER_LENGTH_TX BUFFER_LENGTH

#elif defined(__SAMD21G18A__)

#define I2C_BUFFER_LENGTH_RX SERIAL_BUFFER_SIZE // SAMD21 uses RingBuffer.h
#define I2C_BUFFER_LENGTH_TX SERIAL_BUFFER_SIZE

#elif (defined(__MK20DX128__) || defined(__MK20DX256__) || defined(__MKL26Z64__) || defined(__MK64FX512__) ||          \
       defined(__MK66FX1M0__) || defined(__IMXRT1062__)) // 3.0/3.1-3.2/LC/3.5/3.6/4.0

#define I2C_BUFFER_LENGTH_RX BUFFER_LENGTH // Teensy
#define I2C_BUFFER_LENGTH_TX BUFFER_LENGTH

#elif defined(ESP32)

#define I2C_BUFFER_LENGTH_RX I2C_BUFFER_LENGTH
#define I2C_BUFFER_LENGTH_TX I2C_BUFFER_LENGTH

#elif defined(ESP8266)

#define I2C_BUFFER_LENGTH_RX BUFFER_LENGTH // BUFFER_LENGTH is defined in Wire.h for ESP8266
#define I2C_BUFFER_LENGTH_TX BUFFER_LENGTH

#elif defined(STM32)

#define I2C_BUFFER_LENGTH_RX BUFFER_LENGTH // BUFFER_LENGTH is defined in Wire.h for STM32
#define I2C_BUFFER_LENGTH_TX BUFFER_LENGTH

#elif defined(NRF52_SERIES)

#define I2C_BUFFER_LENGTH_RX SERIAL_BUFFER_SIZE // Adafruit Bluefruit nRF52 Boards uses RingBuffer.h
#define I2C_BUFFER_LENGTH_TX SERIAL_BUFFER_SIZE

#elif defined(ARDUINO_ARCH_RP2040)

#ifdef WIRE_BUFFER_SIZE
#define I2C_BUFFER_LENGTH_RX WIRE_BUFFER_SIZE // 128 - defined in Wire.h (provided by pico-arduino-compat)
#define I2C_BUFFER_LENGTH_TX WIRE_BUFFER_SIZE
#elif defined(ARDUINO_RASPBERRY_PI_PICO)

#define I2C_BUFFER_LENGTH_RX                                                                                           \
    256 // Not properly defined but set at 256:
        // https://github.com/arduino/ArduinoCore-mbed/blob/master/libraries/Wire/Wire.h
#define I2C_BUFFER_LENGTH_TX 256
#else
#pragma GCC warning                                                                                                    \
    "This RP2040 platform doesn't have a wire buffer size defined. Defaulting to 32 bytes. Please contribute to this library!"

// Default to safe 32 bytes
#define I2C_BUFFER_LENGTH_RX 32
#define I2C_BUFFER_LENGTH_TX 32
#endif

#elif defined(MEGATINYCORE) || defined(DXCORE)
// https://github.com/SpenceKonde/DxCore/blob/master/megaavr/libraries/Wire/src/Wire.h
// Wire defines BUFFER_LENGTH according to ram: 16 for <256B, 32 for 256-4095B, or 130 for >= 4096B

#define I2C_BUFFER_LENGTH_RX BUFFER_LENGTH 
#define I2C_BUFFER_LENGTH_TX BUFFER_LENGTH

#else

#pragma GCC warning                                                                                                    \
    "This platform doesn't have a wire buffer size defined. Defaulting to 32 bytes. Please contribute to this library!"

// Default to safe 32 bytes
#define I2C_BUFFER_LENGTH_RX 32
#define I2C_BUFFER_LENGTH_TX 32

#endif

#endif // !PLATFORMDEFINES_H_
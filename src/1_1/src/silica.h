#pragma once
#include <stdint.h>

// Application layer packet type.
// The first element indicates the total length of the packet.
typedef const uint8_t *packet_t;

// Debug logging, disabled by default.
// Build with -DDEBUG_LOG (the "debug" environment in platformio.ini) to print
// messages over the serial port and to keep the last unsupported command in
// the EEPROM error log (readable as blocks 0xE0 and 0xE1).
// Logging takes milliseconds and wears the EEPROM, so production builds
// must not use it.
#ifdef DEBUG_LOG
// Functions for serial output
// Similar to Arduino interface
void Serial_write(uint8_t);
void Serial_print(const char *);
void Serial_println(const char *);
void Serial_print_hex(uint8_t); // two uppercase hex digits

void save_error(packet_t);
void print_packet(packet_t);

#define LOG(str) Serial_println(str)
#define LOG_PACKET(packet)    \
    do                        \
    {                         \
        save_error(packet);   \
        print_packet(packet); \
    } while (0)
#else
#define LOG(str) ((void)0)
#define LOG_PACKET(packet) ((void)0)
#endif

// application layer functions
void initialize();
packet_t process(packet_t);

#ifndef SERIAL_H
#define SERIAL_H

#include <stdint.h>
#include <stdbool.h>

// Base address for Serial Port 1 (COM1)
#define COM1 0x3F8

// Low-level port helpers
static inline uint8_t serial_inb(uint16_t port) {
    uint8_t ret;
    asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void serial_outb(uint16_t port, uint8_t val) {
    asm volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

// Configures the UART chip properties
void init_serial() {
    serial_outb(COM1 + 1, 0x00);    // Disable all hardware interrupts
    serial_outb(COM1 + 3, 0x80);    // Enable DLAB (set baud rate divisor)
    serial_outb(COM1 + 0, 0x03);    // Set divisor to 3 (38400 baud)
    serial_outb(COM1 + 1, 0x00);    // High byte of divisor
    serial_outb(COM1 + 3, 0x03);    // 8 bits, no parity, one stop bit
    serial_outb(COM1 + 2, 0xC7);    // Enable FIFO, clear them, with 14-byte threshold
    serial_outb(COM1 + 4, 0x0B);    // IRQs enabled, RTS/DSR set
}

// Checks if the serial port transmitter queue is empty and ready to accept data
bool is_serial_empty() {
    // Read the Line Status Register (LSR). Bit 5 is set when ready.
    return (serial_inb(COM1 + 5) & 0x20) != 0;
}

// Sends a single raw character out through the physical COM interface
void serial_put_char(char c) {
    while (!is_serial_empty()); // Spin-wait until the register clears
    serial_outb(COM1, c);
}

// Loops over a string pointer to route text directly to the serial stream
void serial_print(const char *str) {
    while (*str != '\0') {
        // Automatically translate standalone newlines to include a carriage return
        if (*str == '\n') {
            serial_put_char('\r');
        }
        serial_put_char(*str);
        str++;
    }
}

#endif

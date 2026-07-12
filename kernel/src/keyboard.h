#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>
#include <stdbool.h>

#define KEY_UP     0xE0 | 0x48
#define KEY_DOWN   0xE0 | 0x50
#define KEY_LEFT   0xE0 | 0x4B
#define KEY_RIGHT  0xE0 | 0x4D


// Low-level assembly function to read a byte from a hardware port
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

// Check if the keyboard buffer has a new key waiting to be read
static inline bool keyboard_has_key() {
    // Read the Status Register (Port 0x64). Bit 0 is set to 1 if data is ready.
    return (inb(0x64) & 1) != 0;
}

// Reads the raw scancode and translates it to an ASCII character
char keyboard_get_char() {
    if (!keyboard_has_key())
        return 0;

    uint8_t sc = inb(0x60);

    // Extended scancode prefix
    if (sc == 0xE0) {
        uint8_t ext = inb(0x60);

        switch (ext) {
            case 0x48: return KEY_UP;
            case 0x50: return KEY_DOWN;
            case 0x4B: return KEY_LEFT;
            case 0x4D: return KEY_RIGHT;
        }

        return 0;
    }

    // Ignore key releases
    if (sc & 0x80)
        return 0;

    // ASCII lookup
    static const char scancode_to_ascii[] = {
        0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
        '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
        0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0,
        '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' '
    };

    if (sc < sizeof(scancode_to_ascii))
        return scancode_to_ascii[sc];

    return 0;
}

#endif

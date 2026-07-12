#ifndef GRAPHICS_H
#define GRAPHICS_H

#include <stdint.h>
#include "font.h"

// Draws a single 8x8 character onto your fb_ptr coordinates
void draw_char(uint32_t *fb, int x, int y, char c, uint32_t color, uint32_t pitch) {
    uint32_t stride = pitch / 4;
    uint8_t ascii = (uint8_t)c;

    // Static fallback pattern row definition
    static const uint8_t fallback_glyph[8] = {0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55};

    const uint8_t *glyph_rows;

    // Check if the target character falls within our printable font range (32 to 126)
      // Check if the target character falls within our printable font range (32 to 126)
        // Check if the target character falls within our printable font range (32 to 126)
    if (ascii >= 32 && ascii <= 126) {
        // Changed from 32 to 31 to pull the character glyphs back into exact alignment
        glyph_rows = font_bitmap[ascii - 32]; 
    } else {
        // Use checkerboard fallback for non-printable keys
        glyph_rows = fallback_glyph;
    }



    // Render out the 8x8 matrix rows onto the framebuffer
    for (int row = 0; row < 8; row++) {
        uint8_t row_byte = glyph_rows[row];
        for (int col = 0; col < 8; col++) {
            if ((row_byte & (1 << (7 - col))) != 0) {
                fb[(y + row) * stride + (x + col)] = color;
            }
        }
    }
}


// Loops over a string array pointer to draw full text sentences
void draw_string(uint32_t *fb, int x, int y, const char *str, uint32_t color, uint32_t pitch) {
    int current_x = x;
    while (*str != '\0') {
        draw_char(fb, current_x, y, *str, color, pitch);
        current_x += 8; // Advance 8 horizontal pixels forward for the next letter
        str++;
    }
}

// Draws a solid filled block of color at target coordinates
void draw_rect(uint32_t *fb, int x, int y, int w, int h, uint32_t color, uint32_t pitch) {
    uint32_t stride = pitch / 4;
    for (int cy = y; cy < y + h; cy++) {
        for (int cx = x; cx < x + w; cx++) {
            fb[cy * stride + cx] = color;
        }
    }
}

// Draws a hollow outline box with a thickness of 2 pixels for active window tracking
void draw_border(uint32_t *fb, int x, int y, int w, int h, uint32_t color, uint32_t pitch) {
    uint32_t stride = pitch / 4;
    // Top and Bottom lines
    for (int cx = x; cx < x + w; cx++) {
        fb[y * stride + cx] = color;
        fb[(y + 1) * stride + cx] = color;
        fb[(y + h - 1) * stride + cx] = color;
        fb[(y + h - 2) * stride + cx] = color;
    }
    // Left and Right lines
    for (int cy = y; cy < y + h; cy++) {
        fb[cy * stride + x] = color;
        fb[cy * stride + (x + 1)] = color;
        fb[cy * stride + (x + w - 1)] = color;
        fb[cy * stride + (x + w - 2)] = color;
    }
}

void clear_entire_screen(uint32_t *fb, uint32_t width, uint32_t height, uint32_t pitch, uint32_t color) {
    // Pitch / 4 converts total row bytes into 32-bit pixel columns
    uint32_t stride = pitch / 4; 

    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            fb[y * stride + x] = color;
        }
    }
}



#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "limine.h"
#include "serial.h"
#include "keyboard.h"
#include "graphics.h"
#include "rfs.h"

/*
 * Copyright (C) 2026 KashOS Project
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */
// ======================================================
// PORT I/O & TIME SYSTEM
// ======================================================

static inline void outb(uint16_t port, uint8_t value) {
    asm volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t kinb(uint16_t port) {
    uint8_t ret;
    asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline uint64_t rdtsc(void) {
    uint32_t lo, hi;
    asm volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

void sleep_ms(uint32_t ms) {
    const uint64_t cycles_per_ms = 3000000ULL; // Tuned for standard 3GHz emulations
    uint64_t start = rdtsc();
    uint64_t wait_cycles = (uint64_t)ms * cycles_per_ms;

    while (rdtsc() - start < wait_cycles) {
        asm volatile("pause");
    }
}

// ======================================================
// AUDIO HARNESS (STUBBED FOR ZERO DEGRADATION)
// ======================================================

void play_sound(uint32_t nFrequence) { (void)nFrequence; }
void stop_sound(void) {}
void beep(uint32_t frequency, uint32_t duration_ms) { (void)frequency; sleep_ms(duration_ms); }

// ======================================================
// STRINGS & STRUCTS MANAGEMENT
// ======================================================

bool string_compare(const char *a, const char *b) {
    while (*a && (*a == *b)) { a++; b++; }
    return *(unsigned char*)a == *(unsigned char*)b;
}

bool string_starts_with(const char *str, const char *prefix) {
    while (*prefix) {
        if (*str != *prefix) return false;
        str++; prefix++;
    }
    return true;
}

void string_copy(char *dest, const char *src, int max_len) {
    int i = 0;
    while (src[i] != '\0' && i < max_len - 1) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

int k_atoi(const char *s) {
    int sign = 1, result = 0;
    if (*s == '-') { sign = -1; s++; }
    while (*s >= '0' && *s <= '9') {
        result = result * 10 + (*s - '0');
        s++;
    }
    return result * sign;
}

void k_itoa(int value, char *out) {
    char temp[32];
    int pos = 0;
    bool neg = false;
    if (value == 0) { out[0] = '0'; out[1] = '\0'; return; }
    if (value < 0) { neg = true; value = -value; }
    while (value > 0 && pos < 31) { temp[pos++] = '0' + (value % 10); value /= 10; }
    int i = 0;
    if (neg) out[i++] = '-';
    while (pos > 0) out[i++] = temp[--pos];
    out[i] = '\0';
}

typedef struct {
    char name[32];
    int value;
} MiniVar;

static MiniVar vars[128];
static int var_count = 0;

MiniVar* get_var(const char *name) {
    for (int i = 0; i < var_count; i++) {
        if (string_compare(vars[i].name, name)) return &vars[i];
    }
    if (var_count < 128) {
        MiniVar *v = &vars[var_count++];
        string_copy(v->name, name, 32);
        v->value = 0;
        return v;
    }
    return NULL;
}

// ======================================================
// INTERPRETER STORAGE & EXECUTION PARSING
// ======================================================

typedef enum { EXEC_RUNNING, EXEC_SKIPPING_IF, EXEC_SKIPPING_ELSE } ExecState;
static ExecState state_stack[8];
static int stack_ptr = 0;

void parse_command(const char *input, char *cmd, char args[8][64], int *argc) {
    int i = 0, j = 0;
    *argc = 0;
    while (input[i] != ' ' && input[i] != '\0') cmd[j++] = input[i++];
    cmd[j] = '\0';
    while (input[i] == ' ') i++;
    while (input[i] != '\0') {
        j = 0;
        while (input[i] != ' ' && input[i] != '\0') args[*argc][j++] = input[i++];
        args[*argc][j] = '\0';
        (*argc)++;
        while (input[i] == ' ') i++;
    }
}

// ======================================================
// RAM FILESYSTEM
// ======================================================

static RamFile file1;
static RamFile file2;
static RamFile extra_files[16];
static int extra_count = 0;

void init_ram_filesystem(void) {
    rfs_create_file(&file1, "hello.txt", "Welcome to KashOS! This file is running completely from RAM.\n");
    rfs_create_file(&file2, "about.txt", "KashOS v3.1.0\nBuilt using Limine, pure C, and an explicit custom font engine.\n");
}

// ======================================================
// FRAMEBUFFER CONTEXT
// ======================================================

static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0
};

uint32_t *fb_ptr = NULL;
uint32_t fb_pitch = 0;
uint32_t fb_width = 0;
uint32_t fb_height = 0;

static const uint32_t BG_COLOR   = 0x1A1A1A;
static const uint32_t TXT_COLOR  = 0xFFFFFF;
static const uint32_t BLUE_RECT  = 0x0000FF;

int typing_box_x = 20;
int typing_box_y = 110;
const int prompt_offset = 8 * 8;

void clear_screen(void) {
    for (uint32_t y = 0; y < fb_height; y++)
        for (uint32_t x = 0; x < fb_width; x++)
            fb_ptr[y * (fb_pitch / 4) + x] = BG_COLOR;
    typing_box_y = 110;
}

// ======================================================
// NATIVE APP HANDLERS FOR FORWARD DECLARATION INTERFACE
// ======================================================

void handle_command(const char *input, struct limine_framebuffer *fb);

void read_line_sync(char *buf, int max_len) {
    int len = 0;
    buf[0] = '\0';
    int cur_x = typing_box_x;

    while (1) {
        char key = keyboard_get_char();
        if (!key) { asm volatile("pause"); continue; }
        if (key == '\n') {
            typing_box_y += 14;
            typing_box_x = 20 + prompt_offset;
            break;
        }
        if (key == '\b') {
            if (len > 0) {
                cur_x -= 8; len--; buf[len] = '\0';
                for (int y = 0; y < 8; y++)
                    for (int x = 0; x < 8; x++)
                        fb_ptr[(typing_box_y + y) * (fb_pitch / 4) + (cur_x + x)] = BG_COLOR;
            }
        } else if (len < max_len - 1) {
            buf[len++] = key; buf[len] = '\0';
            draw_char(fb_ptr, cur_x, typing_box_y, key, TXT_COLOR, fb_pitch);
            cur_x += 8;
        }
    }
}

// ======================================================
// REMADE CORE INTERPRETER ENGINE (KASHLANG FULL)
// ======================================================

void run_line(const char *line, struct limine_framebuffer *fb) {
    char cmd[64]; char args[8][64]; int argc = 0;
    int sx = 20;

    // 1. Process structural boundaries block tokens
    if (string_starts_with(line, "if ")) {
        if (stack_ptr >= 8) return;
        if (stack_ptr > 0 && (state_stack[stack_ptr - 1] == EXEC_SKIPPING_IF || state_stack[stack_ptr - 1] == EXEC_SKIPPING_ELSE)) {
            state_stack[stack_ptr++] = EXEC_SKIPPING_IF;
            return;
        }
        parse_command(line + 3, cmd, args, &argc);
        MiniVar *v = get_var(cmd);
        int target = k_atoi(args[1]);
        bool cond = false;

        if (string_compare(args[0], "==") && v && v->value == target) cond = true;
        if (string_compare(args[0], "!=") && v && v->value != target) cond = true;
        if (string_compare(args[0], ">")  && v && v->value > target)  cond = true;
        if (string_compare(args[0], "<")  && v && v->value < target)  cond = true;

        state_stack[stack_ptr++] = cond ? EXEC_RUNNING : EXEC_SKIPPING_IF;
        return;
    }
    if (string_compare(line, "else")) {
        if (stack_ptr == 0) return;
        ExecState cur = state_stack[stack_ptr - 1];
        if (cur == EXEC_RUNNING) state_stack[stack_ptr - 1] = EXEC_SKIPPING_ELSE;
        else if (cur == EXEC_SKIPPING_IF) state_stack[stack_ptr - 1] = EXEC_RUNNING;
        return;
    }
    if (string_compare(line, "endif")) {
        if (stack_ptr > 0) stack_ptr--;
        return;
    }

    // 2. Structural execution skipping bypass
    if (stack_ptr > 0) {
        ExecState active = state_stack[stack_ptr - 1];
        if (active == EXEC_SKIPPING_IF || active == EXEC_SKIPPING_ELSE) return;
    }

    parse_command(line, cmd, args, &argc);

    if (string_compare(cmd, "print")) {
        char buf[256] = {0}; int pos = 0;
        const char *p = line; const char *start = NULL; const char *end = NULL;
        while (*p) {
            if (*p == '"') { if (!start) start = p + 1; else { end = p; break; } }
            p++;
        }
        if (start && end && end > start) {
            const char *q = start;
            while (q < end && pos < 255) buf[pos++] = *q++;
            buf[pos] = '\0';
        } else {
            for (int i = 0; i < argc; i++) {
                for (int j = 0; args[i][j] != '\0'; j++) buf[pos++] = args[i][j];
                if (i < argc - 1) buf[pos++] = ' ';
            }
            buf[pos] = '\0';
        }
        draw_string(fb_ptr, sx, typing_box_y, buf, TXT_COLOR, fb_pitch);
        typing_box_y += 14;
    }
    else if (string_compare(cmd, "set")) {
        if (argc < 2) return;
        MiniVar *v = get_var(args[0]);
        if (v) v->value = k_atoi(args[1]);
    }
    else if (string_compare(cmd, "input")) {
        if (argc < 1) return;
        char input_buf[64];
        typing_box_x = sx;
        draw_string(fb_ptr, sx, typing_box_y, "? ", 0x55FF55, fb_pitch);
        typing_box_x += 16;
        read_line_sync(input_buf, 64);
        MiniVar *v = get_var(args[0]);
        if (v) v->value = k_atoi(input_buf);
    }
    else if (string_compare(cmd, "run")) {
        char run_buf[128] = {0};
        const char *p = line + 4; // jump "run "
        int p_idx = 0;
        while (*p && p_idx < 127) run_buf[p_idx++] = *p++;
        run_buf[p_idx] = '\0';
        handle_command(run_buf, fb);
    }
    else if (string_compare(cmd, "add")) {
        if (argc < 2) return; MiniVar *v = get_var(args[0]); if (v) v->value += k_atoi(args[1]);
    }
    else if (string_compare(cmd, "sub")) {
        if (argc < 2) return; MiniVar *v = get_var(args[0]); if (v) v->value -= k_atoi(args[1]);
    }
    else if (string_compare(cmd, "mul")) {
        if (argc < 2) return; MiniVar *v = get_var(args[0]); if (v) v->value *= k_atoi(args[1]);
    }
    else if (string_compare(cmd, "div")) {
        if (argc < 2) return; int d = k_atoi(args[1]); if (d != 0) { MiniVar *v = get_var(args[0]); if (v) v->value /= d; }
    }
    else if (string_compare(cmd, "echo")) {
        if (argc == 0) return;
        MiniVar *v = get_var(args[0]);
        if (v) {
            char buf[64]; k_itoa(v->value, buf);
            draw_string(fb_ptr, sx, typing_box_y, buf, TXT_COLOR, fb_pitch);
            typing_box_y += 14;
        }
    }
    else if (string_compare(cmd, "rect")) {
        if (argc < 4) return;
        int x = k_atoi(args[0]), y = k_atoi(args[1]), w = k_atoi(args[2]), h = k_atoi(args[3]);
        if (x < 0) x = 0; if (y < 0) y = 0; if (w < 0) w = 0; if (h < 0) h = 0;
        for (int yy = y; yy < y + h && yy < (int)fb_height; yy++)
            for (int xx = x; xx < x + w && xx < (int)fb_width; xx++)
                fb_ptr[yy * (fb_pitch / 4) + xx] = BLUE_RECT;
    }
    else if (string_compare(cmd, "clear")) { clear_screen(); }
    else if (string_compare(cmd, "sleep")) { if (argc >= 1) sleep_ms(k_atoi(args[0])); }
}

// ======================================================
// KASIM EDITOR MODULE
// ======================================================

static bool kasim_active = false;
static RamFile *kasim_file = NULL;
static char kasim_buf[4096];
static int kasim_len = 0;

void kasim_redraw(void) {
    for (uint32_t y = 0; y < fb_height; y++)
        for (uint32_t x = 0; x < fb_width; x++)
            fb_ptr[y * (fb_pitch / 4) + x] = 0x000000;
    int px = 10, py = 10;
    for (int i = 0; i < kasim_len; i++) {
        char c = kasim_buf[i];
        if (c == '\n') { py += 14; px = 10; }
        else { draw_char(fb_ptr, px, py, c, TXT_COLOR, fb_pitch); px += 8; }
    }
    draw_string(fb_ptr, 10, fb_height - 24, "KASIM v1.2 - '.' on a line to save+exit", 0x00FF00, fb_pitch);
}

void kasim_save(void) {
    kasim_buf[kasim_len] = '\0';
    if (kasim_file) rfs_set_file(kasim_file, kasim_buf);
}

void kasim_open(RamFile *f) {
    kasim_active = true; kasim_file = f; kasim_len = 0;
    if (f && f->content) {
        for (int i = 0; f->content[i] != '\0' && kasim_len < (int)sizeof(kasim_buf) - 1; i++)
            kasim_buf[kasim_len++] = f->content[i];
    }
    kasim_redraw();
}

void kasim_key(char key) {
    if (key == '\n') {
        int start = kasim_len - 1;
        while (start >= 0 && kasim_buf[start] != '\n') start--;
        int line_start = start + 1;
        int line_len = kasim_len - line_start;
        if (line_len == 1 && kasim_buf[line_start] == '.') {
            kasim_save(); kasim_active = false; return;
        }
        if (kasim_len < (int)sizeof(kasim_buf) - 1) { kasim_buf[kasim_len++] = '\n'; kasim_redraw(); }
        return;
    }
    if (key == '\b') { if (kasim_len > 0) { kasim_len--; kasim_redraw(); } return; }
    if (key >= 32 && key <= 126) {
        if (kasim_len < (int)sizeof(kasim_buf) - 1) { kasim_buf[kasim_len++] = key; kasim_redraw(); }
    }
}

// ======================================================
// ARCADE MODULE: UNDERTALE SIMULATOR
// ======================================================

typedef struct { int x; int y; } Vector2D;

void run_undertale_game(void) {
    int soul_x = fb_width / 2, soul_y = fb_height / 2;
    int soul_size = 8, soul_speed = 4;
    int box_l = fb_width/2 - 100, box_r = fb_width/2 + 100;
    int box_t = fb_height/2 - 80, box_b = fb_height/2 + 80;

    // Bullet generation arrays
    int bullet_x = box_r - 10, bullet_y = fb_height / 2;
    int bullet_size = 6, bullet_speed = 3;

    bool active = true;
    while (active) {
        char key = keyboard_get_char();
        if (key == 'q' || key == 'Q') break;
        if (key == 'w' || key == 'W') soul_y -= soul_speed;
        if (key == 's' || key == 'S') soul_y += soul_speed;
        if (key == 'a' || key == 'A') soul_x -= soul_speed;
        if (key == 'd' || key == 'D') soul_x += soul_speed;

        if (soul_x < box_l + 4) soul_x = box_l + 4;
        if (soul_x + soul_size > box_r - 4) soul_x = box_r - 4 - soul_size;
        if (soul_y < box_t + 4) soul_y = box_t + 4;
        if (soul_y + soul_size > box_b - 4) soul_y = box_b - 4 - soul_size;

        // Simulate projectile mechanics
        bullet_x -= bullet_speed;
        if (bullet_x < box_l + 4) {
            bullet_x = box_r - 14;
            bullet_y = box_t + 10 + (int)(rdtsc() % (box_b - box_t - 20));
        }

        // Simple bounding box checks (Heart vs Bone)
        if (soul_x < bullet_x + bullet_size && soul_x + soul_size > bullet_x &&
            soul_y < bullet_y + bullet_size && soul_y + soul_size > bullet_y) {
            // Collision trigger
            break; 
        }

        // Render pass frames
        for (uint32_t y = box_t - 20; y <= (uint32_t)box_b + 50; y++)
            for (uint32_t x = box_l - 20; x <= (uint32_t)box_r + 20; x++)
                fb_ptr[y * (fb_pitch / 4) + x] = 0x000000;

        // Render Frame Bounds
        for (int x = box_l; x <= box_r; x++) {
            fb_ptr[box_t * (fb_pitch / 4) + x] = 0xFFFFFF;
            fb_ptr[box_b * (fb_pitch / 4) + x] = 0xFFFFFF;
        }
        for (int y = box_t; y <= box_b; y++) {
            fb_ptr[y * (fb_pitch / 4) + box_l] = 0xFFFFFF;
            fb_ptr[y * (fb_pitch / 4) + box_r] = 0xFFFFFF;
        }

        // Render Active Assets
        for (int y = 0; y < soul_size; y++)
            for (int x = 0; x < soul_size; x++)
                fb_ptr[(soul_y + y) * (fb_pitch / 4) + (soul_x + x)] = 0xFF0000;

        for (int y = 0; y < bullet_size; y++)
            for (int x = 0; x < bullet_size; x++)
                fb_ptr[(bullet_y + y) * (fb_pitch / 4) + (bullet_x + x)] = 0xFFFFFF;

        draw_string(fb_ptr, box_l, box_b + 15, "* Stay determined! (Q to quit)", 0xFFFFFF, fb_pitch);
        sleep_ms(16);
    }
    clear_screen();
}

// ======================================================
// ARCADE MODULE: CLASSIC SNAKE & PONG GAMES
// ======================================================

void run_snake_game(void) {
    for (uint32_t y = 0; y < fb_height; y++)
        for (uint32_t x = 0; x < fb_width; x++) fb_ptr[y * (fb_pitch / 4) + x] = 0x000000;

    Vector2D snake[256]; int snake_len = 3;
    snake[0] = (Vector2D){20, 15}; snake[1] = (Vector2D){20, 16}; snake[2] = (Vector2D){20, 17};
    int dir = 0; Vector2D apple = (Vector2D){10, 10}; bool game_over = false; int score = 0;

    draw_string(fb_ptr, 20, 20, "KashOS Snake - Use WASD to turn. Press 'Q' to quit.", 0x00FF00, fb_pitch);

    while (!game_over) {
        char key = keyboard_get_char();
        if (key != '\0') {
            if ((key == 'w' || key == 'W') && dir != 1) dir = 0;
            if ((key == 's' || key == 'S') && dir != 0) dir = 1;
            if ((key == 'a' || key == 'A') && dir != 3) dir = 2;
            if ((key == 'd' || key == 'D') && dir != 2) dir = 3;
            if (key == 'q' || key == 'Q') break;
        }
        Vector2D old_tail = snake[snake_len - 1];
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 16; x++)
                fb_ptr[((old_tail.y * 16 + 50) + y) * (fb_pitch / 4) + (old_tail.x * 16 + 20 + x)] = 0x000000;

        for (int i = snake_len - 1; i > 0; i--) snake[i] = snake[i - 1];
        if (dir == 0) snake[0].y--; if (dir == 1) snake[0].y++; if (dir == 2) snake[0].x--; if (dir == 3) snake[0].x++;

        if (snake[0].x < 0 || snake[0].x >= 45 || snake[0].y < 0 || snake[0].y >= 30) break;
        for (int i = 1; i < snake_len; i++) if (snake[0].x == snake[i].x && snake[0].y == snake[i].y) game_over = true;
        if (game_over) break;

        if (snake[0].x == apple.x && snake[0].y == apple.y) {
            score++; if (snake_len < 255) snake_len++;
            uint64_t ticks = rdtsc();
            apple.x = (ticks % 40) + 2; apple.y = ((ticks >> 4) % 25) + 2;
        }
        for (int y = 0; y < 12; y++)
            for (int x = 0; x < 12; x++)
                fb_ptr[((apple.y * 16 + 52) + y) * (fb_pitch / 4) + (apple.x * 16 + 22 + x)] = 0xFF0000;
        for (int i = 0; i < snake_len; i++) {
            uint32_t color = (i == 0) ? 0x00FF00 : 0x00AA00;
            for (int y = 0; y < 14; y++)
                for (int x = 0; x < 14; x++)
                    fb_ptr[((snake[i].y * 16 + 51) + y) * (fb_pitch / 4) + (snake[i].x * 16 + 21 + x)] = color;
        }
        sleep_ms(130);
    }
    draw_string(fb_ptr, 150, fb_height / 2, "GAME OVER! Press any key to return to shell.", 0xFF0000, fb_pitch);
    while (keyboard_get_char() == '\0') asm volatile("pause");
    clear_screen();
}

void run_pong_game() {
    for (uint32_t y = 0; y < fb_height; y++)
        for (uint32_t x = 0; x < fb_width; x++) fb_ptr[y * (fb_pitch / 4) + x] = 0x000000;

    const int pad_w = 12, pad_h = 80;
    int p1_x = 30, p1_y = fb_height / 2 - pad_h / 2;
    int p2_x = fb_width - 30 - pad_w, p2_y = fb_height / 2 - pad_h / 2;
    int ball_x = fb_width / 2, ball_y = fb_height / 2, ball_size = 10, ball_dx = 5, ball_dy = 3;
    int p1_score = 0, p2_score = 0; bool playing = true;

    while (playing) {
        char key = keyboard_get_char();
        if (key != '\0') {
            if ((key == 'w' || key == 'W') && p1_y > 10) p1_y -= 15;
            if ((key == 's' || key == 'S') && p1_y < (int)fb_height - pad_h - 10) p1_y += 15;
            if ((key == 'i' || key == 'I') && p2_y > 10) p2_y -= 15;
            if ((key == 'k' || key == 'K') && p2_y < (int)fb_height - pad_h - 10) p2_y += 15;
            if (key == 'q' || key == 'Q') break;
        }
        for (uint32_t y = 0; y < fb_height; y++)
            for (uint32_t x = 0; x < fb_width; x++) fb_ptr[y * (fb_pitch / 4) + x] = 0x000000;

        ball_x += ball_dx; ball_y += ball_dy;
        if (ball_y <= 0 || ball_y >= (int)fb_height - ball_size) ball_dy = -ball_dy;

        if (ball_x <= p1_x + pad_w && ball_x >= p1_x && ball_y + ball_size >= p1_y && ball_y <= p1_y + pad_h) { ball_dx = -ball_dx; ball_x = p1_x + pad_w; }
        if (ball_x + ball_size >= p2_x && ball_x <= p2_x + pad_w && ball_y + ball_size >= p2_y && ball_y <= p2_y + pad_h) { ball_dx = -ball_dx; ball_x = p2_x - ball_size; }

        if (ball_x < 0) { p2_score++; ball_x = fb_width / 2; ball_y = fb_height / 2; ball_dx = 5; }
        else if (ball_x > (int)fb_width) { p1_score++; ball_x = fb_width / 2; ball_y = fb_height / 2; ball_dx = -5; }

        for (uint32_t y = 0; y < fb_height; y += 30)
            for (int h = 0; h < 15; h++) if (y + h < fb_height) fb_ptr[(y + h) * (fb_pitch / 4) + (fb_width / 2)] = 0x555555;

        for (int y = p1_y; y < p1_y + pad_h; y++) for (int x = p1_x; x < p1_x + pad_w; x++) fb_ptr[y * (fb_pitch / 4) + x] = 0xFFFFFF;
        for (int y = p2_y; y < p2_y + pad_h; y++) for (int x = p2_x; x < p2_x + pad_w; x++) fb_ptr[y * (fb_pitch / 4) + x] = 0xFFFFFF;
        for (int y = ball_y; y < ball_y + ball_size; y++) for (int x = ball_x; x < ball_x + ball_size; x++) fb_ptr[y * (fb_pitch / 4) + x] = 0x00FFFF;

        char s1[16], s2[16]; k_itoa(p1_score, s1); k_itoa(p2_score, s2);
        draw_string(fb_ptr, fb_width / 4, 30, s1, 0xFFFFFF, fb_pitch);
        draw_string(fb_ptr, (fb_width / 4) * 3, 30, s2, 0xFFFFFF, fb_pitch);
        if (p1_score >= 5 || p2_score >= 5) playing = false;
        sleep_ms(16);
    }
    clear_screen();
}

// ======================================================
// CENTRAL SYSTEM KERNEL COMMAND INTERFACE
// ======================================================

void handle_command(const char *input, struct limine_framebuffer *fb) {
    if (input[0] == '\0') return;
    char cmd[64]; char args[8][64]; int argc = 0;
    parse_command(input, cmd, args, &argc);
    int sx = 20;

    draw_string(fb_ptr, sx, typing_box_y, "kashos> ", 0xAAAAFF, fb_pitch);
    draw_string(fb_ptr, sx + prompt_offset, typing_box_y, input, TXT_COLOR, fb_pitch);
    typing_box_y += 14;

    if (string_compare(cmd, "help")) {
        draw_string(fb_ptr, sx, typing_box_y, "Commands: help, version, clear, shutdown, echo, ls, cat, touch, kashlang, kasim, snake, pong, undertale, reboot", 0xFFFF00, fb_pitch);
    }
    else if (string_compare(cmd, "reboot")) {
        uint8_t good = 0x02;
        while (good & 0x02) { good = kinb(0x64); }
        outb(0x64, 0xFE);
        while (1) { asm volatile("hlt"); }
    }
    else if (string_compare(cmd, "undertale")) {
        run_undertale_game();
    }
    else if (string_compare(cmd, "snake")) {
        run_snake_game();
    }
    else if (string_compare(cmd, "pong")) {
        run_pong_game();
    }
    else if (string_compare(cmd, "version")) {
        draw_string(fb_ptr, sx, typing_box_y, "KashOS Shell v3.1.0 (KashLang v2 + Kasim v1.2)", 0x55FF55, fb_pitch);
    }
    else if (string_compare(cmd, "clear")) {
        clear_screen(); return;
    }
    else if (string_compare(cmd, "shutdown")) {
        while (1) asm("hlt");
    }
    else if (string_compare(cmd, "ls")) {
        RamFile *cur = rfs_root; int off = sx;
        if (!cur) { draw_string(fb_ptr, sx, typing_box_y, "(No files found in RFS memory)", 0xAAAAAA, fb_pitch); }
        else {
            while (cur) {
                draw_string(fb_ptr, off, typing_box_y, cur->name, 0x55FFFF, fb_pitch);
                off += (rfs_strlen(cur->name) * 8) + 16; cur = cur->next;
            }
        }
    }
    else if (string_compare(cmd, "cat")) {
        if (argc == 0) { draw_string(fb_ptr, sx, typing_box_y, "Usage: cat <filename>", 0xFF0000, fb_pitch); }
        else {
            RamFile *f = rfs_find_file(args[0]);
            if (f) draw_string(fb_ptr, sx, typing_box_y, f->content, TXT_COLOR, fb_pitch);
            else draw_string(fb_ptr, sx, typing_box_y, "Error: File not found.", 0xFF0000, fb_pitch);
        }
    }
    else if (string_compare(cmd, "echo")) {
        char buf[256] = {0}; int pos = 0;
        for (int i = 0; i < argc; i++) {
            for (int j = 0; args[i][j] != '\0'; j++) buf[pos++] = args[i][j];
            if (i < argc - 1) buf[pos++] = ' ';
        }
        draw_string(fb_ptr, sx, typing_box_y, buf, TXT_COLOR, fb_pitch);
    }
    else if (string_compare(cmd, "touch")) {
        if (argc == 0) { draw_string(fb_ptr, sx, typing_box_y, "Usage: touch <filename>", 0xFF0000, fb_pitch); }
        else if (extra_count >= 16) { draw_string(fb_ptr, sx, typing_box_y, "Error: RFS touch limit reached.", 0xFF0000, fb_pitch); }
        else {
            rfs_create_file(&extra_files[extra_count], args[0], ""); extra_count++;
            draw_string(fb_ptr, sx, typing_box_y, "File created in RAM.", 0x00FF00, fb_pitch);
        }
    }
    else if (string_compare(cmd, "kasim")) {
        if (argc == 0) { draw_string(fb_ptr, sx, typing_box_y, "Usage: kasim <filename>", 0xFF0000, fb_pitch); }
        else {
            RamFile *f = rfs_find_file(args[0]);
            if (!f) draw_string(fb_ptr, sx, typing_box_y, "Error: File not found.", 0xFF0000, fb_pitch);
            else kasim_open(f);
        }
    }
    else if (string_compare(cmd, "kashlang")) {
        if (argc == 0) { draw_string(fb_ptr, sx, typing_box_y, "Usage: kashlang <filename>", 0xFF0000, fb_pitch); }
        else if (string_compare(args[0], "-version")) { draw_string(fb_ptr, sx, typing_box_y, "KashLang v2 (With If-Else Stack Control Flow)", 0x55FF55, fb_pitch); }
        else {
            RamFile *f = rfs_find_file(args[0]);
            if (!f) { draw_string(fb_ptr, sx, typing_box_y, "Error: File not found.", 0xFF0000, fb_pitch); }
            else {
                const char *p = f->content; char line[128]; int pos = 0;
                stack_ptr = 0; // Reset script state conditions flag
                while (*p) {
                    if (*p == '\n') { line[pos] = '\0'; run_line(line, fb); pos = 0; }
                    else if (pos < 127) { line[pos++] = *p; }
                    p++;
                }
                if (pos > 0) { line[pos] = '\0'; run_line(line, fb); }
            }
        }
    }
    else { draw_string(fb_ptr, sx, typing_box_y, "Unknown command. Type 'help'", 0xFF0000, fb_pitch); }
    typing_box_y += 14;
}

// ======================================================
// KMAIN INITIALIZATION ENTER LOOP
// ======================================================

void kmain(void) {
    init_serial();
    serial_print("KashOS Logger Active\n");
    init_ram_filesystem();

    char buffer[128]; int len = 0; buffer[0] = '\0';

    if (!framebuffer_request.response || framebuffer_request.response->framebuffer_count < 1)
        while (1) asm("hlt");

    struct limine_framebuffer *fb = framebuffer_request.response->framebuffers[0];
    fb_ptr = (uint32_t*)fb->address;
    fb_pitch = fb->pitch;
    fb_width = fb->width;
    fb_height = fb->height;

    clear_entire_screen(fb_ptr, fb_width, fb_height, fb_pitch, 0x000000);
    draw_rect(fb_ptr, fb_width/2 - 80, fb_height/2 - 80, 160, 160, 0x0000FF, fb_pitch);
    draw_string(fb_ptr, fb_width/2 - 24, fb_height/2 + 10, "KashOS", 0xFFFFFF, fb_pitch);

    sleep_ms(2000);
    clear_screen();

    draw_string(fb_ptr, 20, 20, "==================================================", 0x00FF00, fb_pitch);
    draw_string(fb_ptr, 20, 35, "KashOS 64-bit Freestanding Environment", 0x00FF00, fb_pitch);
    draw_string(fb_ptr, 20, 50, "==================================================", 0x00FF00, fb_pitch);
    draw_string(fb_ptr, 20, 75, "Ultimate Language: print, input, conditional stack, shell run", 0x55FFFF, fb_pitch);
    draw_string(fb_ptr, 20, 90, "Arcade Suite Active: snake, pong, undertale", 0x55FFFF, fb_pitch);

    typing_box_x = 20 + prompt_offset;
    draw_string(fb_ptr, 20, typing_box_y, "kashos> ", 0xAAAAFF, fb_pitch);

    while (1) {
        char key = keyboard_get_char();
        if (!key) { asm volatile("pause"); continue; }

        if (kasim_active) {
            kasim_key(key);
            if (!kasim_active) {
                clear_screen();
                draw_string(fb_ptr, 20, 20, "==================================================", 0x00FF00, fb_pitch);
                draw_string(fb_ptr, 20, 35, "KashOS Subsystem Console", 0x00FF00, fb_pitch);
                draw_string(fb_ptr, 20, 50, "==================================================", 0x00FF00, fb_pitch);
                typing_box_y = 70; typing_box_x = 20 + prompt_offset;
                draw_string(fb_ptr, 20, typing_box_y, "kashos> ", 0xAAAAFF, fb_pitch);
            }
            asm volatile("pause"); continue;
        }

        if (key == '\n') {
            handle_command(buffer, fb);
            draw_string(fb_ptr, 20, typing_box_y, "kashos> ", 0xAAAAFF, fb_pitch);
            typing_box_x = 20 + prompt_offset;
            len = 0; buffer[0] = '\0';
        }
        else if (key == '\b') {
            if (typing_box_x > (20 + prompt_offset) && len > 0) {
                typing_box_x -= 8; len--; buffer[len] = '\0';
                for (int y = 0; y < 8; y++)
                    for (int x = 0; x < 8; x++)
                        fb_ptr[(typing_box_y + y) * (fb_pitch / 4) + (typing_box_x + x)] = BG_COLOR;
            }
        }
        else if (len < 127) {
            buffer[len++] = key; buffer[len] = '\0';
            draw_char(fb_ptr, typing_box_x, typing_box_y, key, TXT_COLOR, fb_pitch);
            typing_box_x += 8;
            if (typing_box_x > (int)fb_width - 20) { typing_box_x = 20 + prompt_offset; typing_box_y += 14; }
        }
        asm volatile("pause");
    }
}
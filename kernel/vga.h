/* VGA text mode output. Defined in kernel.c. */

#ifndef VGA_H
#define VGA_H

#include <stdint.h>

#define COLOR_BLACK            0x00
#define COLOR_BLUE             0x01
#define COLOR_GREEN            0x02
#define COLOR_CYAN             0x03
#define COLOR_RED              0x04
#define COLOR_MAGENTA          0x05
#define COLOR_BROWN            0x06
#define COLOR_LIGHT_GRAY       0x07
#define COLOR_DARK_GRAY        0x08
#define COLOR_LIGHT_BLUE       0x09
#define COLOR_LIGHT_GREEN      0x0A
#define COLOR_LIGHT_CYAN       0x0B
#define COLOR_LIGHT_RED        0x0C
#define COLOR_LIGHT_MAGENTA    0x0D
#define COLOR_YELLOW           0x0E
#define COLOR_WHITE            0x0F

void clear_screen(void);
void set_cursor_position(uint8_t x, uint8_t y);
void print_character(char c);
void print_string(const char* str);
void print_colored_string(const char* str, uint8_t color);
void backspace_character(void);

#endif

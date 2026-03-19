/* Linear framebuffer graphics.
 *
 * Mode setting goes through the Bochs VBE dispatch interface (ports 0x1CE
 * and 0x1CF) rather than the VESA BIOS. The BIOS route means dropping to
 * real mode or carrying a v86 monitor; these ports work from protected
 * mode with two out instructions, and qemu's std VGA speaks them. The cost
 * is that it's qemu/bochs-specific - real hardware wants the BIOS call, and
 * that's a bootloader job for the day this meets real silicon.
 *
 * The framebuffer itself is the std VGA card's PCI BAR0. */

#ifndef FB_H
#define FB_H

#include <stdint.h>

#define FB_WIDTH  1024
#define FB_HEIGHT 768

/* 0x00RRGGBB. The card is 32bpp little-endian, so this maps straight to a
 * uint32_t store. */
#define RGB(r, g, b) (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))

#define FB_BLACK   RGB(0x00, 0x00, 0x00)
#define FB_WHITE   RGB(0xE8, 0xE8, 0xE8)
#define FB_GREY    RGB(0x80, 0x80, 0x80)
#define FB_RED     RGB(0xC0, 0x30, 0x30)
#define FB_GREEN   RGB(0x30, 0xC0, 0x50)
#define FB_BLUE    RGB(0x40, 0x70, 0xD0)
#define FB_YELLOW  RGB(0xD0, 0xC0, 0x40)
#define FB_CYAN    RGB(0x40, 0xC0, 0xC0)

int  fb_initialize(void);
int  fb_present(void);

uint32_t fb_width(void);
uint32_t fb_height(void);

void fb_clear(uint32_t colour);
void fb_pixel(uint32_t x, uint32_t y, uint32_t colour);
void fb_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t colour);
void fb_frame(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t colour);

void fb_char(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg);
void fb_text(uint32_t x, uint32_t y, const char* s, uint32_t fg, uint32_t bg);

/* Text console on top of the framebuffer: same job as the VGA text mode
 * console, one layer up. */
void fb_console_putchar(char c);
void fb_console_enable(int on);
int  fb_console_active(void);

/* Draws a pattern with known colours at known coordinates, so a host-side
 * screenshot can check exact pixels rather than "something appeared". */
void fb_demo(void);

#endif

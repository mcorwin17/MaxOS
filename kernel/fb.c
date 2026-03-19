#include <stdint.h>

#include "fb.h"
#include "pci.h"
#include "io.h"
#include "vmm.h"
#include "pmm.h"
#include "serial.h"
#include "font8x16.h"

/* Bochs VBE dispatch interface. */
#define VBE_INDEX  0x1CE
#define VBE_DATA   0x1CF

#define VBE_ID          0
#define VBE_XRES        1
#define VBE_YRES        2
#define VBE_BPP         3
#define VBE_ENABLE      4
#define VBE_VIRT_WIDTH  6

#define VBE_DISABLED    0x00
#define VBE_ENABLED     0x01
#define VBE_LFB         0x40

static volatile uint32_t* framebuffer;
static uint32_t fb_pitch_px;        /* pixels per scanline, not bytes */
static int      present;

/* Text console state, in character cells. */
static uint32_t cursor_col, cursor_row;
static uint32_t cols, rows;
static int      console_on;

static void vbe_write(uint16_t index, uint16_t value) {
    outw(VBE_INDEX, index);
    outw(VBE_DATA, value);
}

static uint16_t vbe_read(uint16_t index) {
    outw(VBE_INDEX, index);
    return inw(VBE_DATA);
}

int fb_present(void)     { return present; }
uint32_t fb_width(void)  { return FB_WIDTH; }
uint32_t fb_height(void) { return FB_HEIGHT; }

int fb_initialize(void) {
    present = 0;

    /* The ID register reads back 0xB0Cx on a card that speaks this
     * interface, and 0xFFFF (or garbage) on one that doesn't. */
    uint16_t id = vbe_read(VBE_ID);
    if ((id & 0xFFF0) != 0xB0C0) {
        kprintf("fb: no bochs VBE interface (id 0x%04x)\n", id);
        return -1;
    }

    struct pci_device vga;
    if (!pci_find(0x1234, 0x1111, &vga)) {
        kprintf("fb: no std VGA device\n");
        return -1;
    }

    /* BAR0 is the linear framebuffer; the low bits are flags. */
    uint32_t phys = vga.bar[0] & ~0xFu;
    if (!phys) { kprintf("fb: VGA has no framebuffer BAR\n"); return -1; }

    /* Mode must be disabled while the geometry registers change. */
    vbe_write(VBE_ENABLE, VBE_DISABLED);
    vbe_write(VBE_XRES, FB_WIDTH);
    vbe_write(VBE_YRES, FB_HEIGHT);
    vbe_write(VBE_BPP, 32);
    vbe_write(VBE_ENABLE, VBE_ENABLED | VBE_LFB);

    /* The card may hand back a wider scanline than we asked for; drawing
     * against the requested width instead of the real pitch produces a
     * picture that shears diagonally. */
    uint16_t virt_width = vbe_read(VBE_VIRT_WIDTH);
    fb_pitch_px = virt_width ? virt_width : FB_WIDTH;

    uint32_t bytes = fb_pitch_px * FB_HEIGHT * 4;

    /* Above RAM, so the identity map doesn't cover it. Map it, and share
     * every 4 MB slot it spans into process directories - the console
     * writes here from kernel code that runs on whatever CR3 is loaded. */
    for (uint32_t off = 0; off < bytes; off += PAGE_SIZE) {
        vmm_map(phys + off, phys + off, PAGE_WRITE);
    }
    for (uint32_t off = 0; off < bytes; off += 0x400000) {
        vmm_share_pde(phys + off);
    }
    vmm_share_pde(phys + bytes - 1);

    framebuffer = (volatile uint32_t*)(uintptr_t)phys;

    cols = FB_WIDTH / FONT_WIDTH;
    rows = FB_HEIGHT / FONT_HEIGHT;
    cursor_col = cursor_row = 0;
    console_on = 0;

    present = 1;
    kprintf("fb: %ux%u 32bpp at 0x%08x, pitch %u px, %ux%u text cells\n",
            FB_WIDTH, FB_HEIGHT, phys, fb_pitch_px, cols, rows);
    return 0;
}

void fb_pixel(uint32_t x, uint32_t y, uint32_t colour) {
    if (!present || x >= FB_WIDTH || y >= FB_HEIGHT) return;
    framebuffer[y * fb_pitch_px + x] = colour;
}

void fb_clear(uint32_t colour) {
    if (!present) return;

    for (uint32_t y = 0; y < FB_HEIGHT; ++y) {
        volatile uint32_t* row = framebuffer + y * fb_pitch_px;
        for (uint32_t x = 0; x < FB_WIDTH; ++x) row[x] = colour;
    }
}

void fb_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t colour) {
    if (!present) return;

    for (uint32_t dy = 0; dy < h; ++dy) {
        uint32_t py = y + dy;
        if (py >= FB_HEIGHT) break;

        volatile uint32_t* row = framebuffer + py * fb_pitch_px;
        for (uint32_t dx = 0; dx < w; ++dx) {
            uint32_t px = x + dx;
            if (px >= FB_WIDTH) break;
            row[px] = colour;
        }
    }
}

void fb_frame(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t colour) {
    fb_rect(x, y, w, 1, colour);
    fb_rect(x, y + h - 1, w, 1, colour);
    fb_rect(x, y, 1, h, colour);
    fb_rect(x + w - 1, y, 1, h, colour);
}

void fb_char(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg) {
    if (!present) return;

    unsigned char ch = (unsigned char)c;
    if (ch < FONT_FIRST || ch > FONT_LAST) ch = ' ';

    const unsigned char* glyph = &font8x16[(ch - FONT_FIRST) * FONT_HEIGHT];

    for (uint32_t row = 0; row < FONT_HEIGHT; ++row) {
        uint32_t py = y + row;
        if (py >= FB_HEIGHT) return;

        volatile uint32_t* line = framebuffer + py * fb_pitch_px;
        unsigned char bits = glyph[row];

        for (uint32_t col = 0; col < FONT_WIDTH; ++col) {
            uint32_t px = x + col;
            if (px >= FB_WIDTH) break;

            /* MSB is the leftmost column, which is how the generator
             * packed it. */
            line[px] = (bits & (0x80 >> col)) ? fg : bg;
        }
    }
}

void fb_text(uint32_t x, uint32_t y, const char* s, uint32_t fg, uint32_t bg) {
    for (uint32_t i = 0; s[i]; ++i) {
        fb_char(x + i * FONT_WIDTH, y, s[i], fg, bg);
    }
}

static void fb_scroll(void) {
    /* Move every row up one cell and clear the last. A blit, not a redraw -
     * the console has no memory of what it printed. */
    uint32_t line_px = FONT_HEIGHT * fb_pitch_px;
    uint32_t total   = (rows - 1) * line_px;

    for (uint32_t i = 0; i < total; ++i) framebuffer[i] = framebuffer[i + line_px];

    fb_rect(0, (rows - 1) * FONT_HEIGHT, FB_WIDTH, FONT_HEIGHT, FB_BLACK);
}

void fb_console_enable(int on) {
    console_on = on && present;
    if (console_on) {
        fb_clear(FB_BLACK);
        cursor_col = cursor_row = 0;
    }
}

int fb_console_active(void) { return console_on; }

void fb_console_putchar(char c) {
    if (!console_on) return;

    if (c == '\r') { cursor_col = 0; return; }

    if (c == '\n') {
        cursor_col = 0;
        if (++cursor_row >= rows) { fb_scroll(); cursor_row = rows - 1; }
        return;
    }

    if (c == '\b') {
        if (cursor_col > 0) {
            --cursor_col;
            fb_char(cursor_col * FONT_WIDTH, cursor_row * FONT_HEIGHT,
                    ' ', FB_WHITE, FB_BLACK);
        }
        return;
    }

    if (c == '\t') {
        cursor_col = (cursor_col + 8) & ~7u;
        if (cursor_col >= cols) { cursor_col = 0; ++cursor_row; }
        return;
    }

    fb_char(cursor_col * FONT_WIDTH, cursor_row * FONT_HEIGHT,
            c, FB_WHITE, FB_BLACK);

    if (++cursor_col >= cols) {
        cursor_col = 0;
        if (++cursor_row >= rows) { fb_scroll(); cursor_row = rows - 1; }
    }
}

/* Known colours at known coordinates, so a screenshot can be checked pixel
 * by pixel instead of "something appeared". */
void fb_demo(void) {
    if (!present) return;

    fb_clear(FB_BLACK);

    /* Title bar. */
    fb_rect(0, 0, FB_WIDTH, 40, RGB(0x20, 0x28, 0x38));
    fb_text(16, 12, "MaxOS graphics", FB_WHITE, RGB(0x20, 0x28, 0x38));

    /* Six swatches, 100x100, 20px apart starting at (40, 80). The test
     * checks the centre pixel of each. */
    static const uint32_t swatch[6] = {
        FB_RED, FB_GREEN, FB_BLUE, FB_YELLOW, FB_CYAN, FB_WHITE
    };
    for (uint32_t i = 0; i < 6; ++i) {
        uint32_t x = 40 + i * 120;
        fb_rect(x, 80, 100, 100, swatch[i]);
        fb_frame(x - 2, 78, 104, 104, FB_GREY);
    }

    /* A gradient: red ramps across, green ramps down. Checking a couple of
     * points here catches an off-by-one in the pitch that a flat fill
     * wouldn't. */
    for (uint32_t y = 0; y < 128; ++y) {
        for (uint32_t x = 0; x < 256; ++x) {
            fb_pixel(40 + x, 220 + y, RGB(x, y * 2, 0x40));
        }
    }

    fb_text(40, 370, "framebuffer: 1024x768, 32bpp, rasterized 8x16 font",
            FB_GREEN, FB_BLACK);
    fb_text(40, 390, "swatches above; gradient left; text is real glyph data",
            FB_GREY, FB_BLACK);

    /* Every printable character, so a broken glyph is visible at a glance. */
    char line[96];
    uint32_t n = 0;
    for (char c = 32; c < 127; ++c) line[n++] = c;
    line[n] = '\0';
    fb_text(40, 420, line, FB_YELLOW, FB_BLACK);
}

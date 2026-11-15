#include "console.h"

typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned long long u64;

#include "font8x16_basic.h"

#define CELL_WIDTH 8U
#define CELL_HEIGHT 16U
#define LINE_GAP 4U
#define MARGIN 24U

static struct byter_framebuffer console_fb;
static u32 cursor_x;
static u32 cursor_y;
static u32 foreground;
static u32 background;
static int ready;

static u32 color(u8 red, u8 green, u8 blue) {
    return ((u32)red << console_fb.red_mask_shift) |
           ((u32)green << console_fb.green_mask_shift) |
           ((u32)blue << console_fb.blue_mask_shift);
}

static void fill(u32 x, u32 y, u32 width, u32 height, u32 value) {
    volatile u32 *pixels = console_fb.address;
    u32 stride = (u32)(console_fb.pitch / sizeof(u32));
    for (u32 row = 0; row < height; ++row)
        for (u32 column = 0; column < width; ++column)
            pixels[(y + row) * stride + x + column] = value;
}

static void scroll(void) {
    volatile u32 *pixels = console_fb.address;
    u32 stride = (u32)(console_fb.pitch / sizeof(u32));
    u32 distance = CELL_HEIGHT + LINE_GAP;
    u32 rows = (u32)console_fb.height;
    for (u32 y = MARGIN; y + distance < rows; ++y)
        for (u32 x = 0; x < console_fb.width; ++x)
            pixels[y * stride + x] = pixels[(y + distance) * stride + x];
    fill(0, rows - distance, (u32)console_fb.width, distance, background);
    cursor_y = rows - distance;
}

static void next_line(void) {
    cursor_x = MARGIN;
    cursor_y += CELL_HEIGHT + LINE_GAP;
    if (cursor_y + CELL_HEIGHT > console_fb.height) scroll();
}

static void draw_glyph(u8 character) {
    if (character >= 128) character = '?';
    if (cursor_x + CELL_WIDTH > console_fb.width) next_line();
    if (cursor_y + CELL_HEIGHT > console_fb.height) scroll();

    const u8 *glyph = font8x16_basic[character];
    volatile u32 *pixels = console_fb.address;
    u32 stride = (u32)(console_fb.pitch / sizeof(u32));
    for (u32 y = 0; y < CELL_HEIGHT; ++y) {
        for (u32 x = 0; x < CELL_WIDTH; ++x) {
            u32 pixel = (glyph[y] & (1U << (7 - x))) ? foreground : background;
            pixels[(cursor_y + y) * stride + cursor_x + x] = pixel;
        }
    }
    cursor_x += CELL_WIDTH;
}

void byter_console_set_framebuffer(const struct byter_framebuffer *framebuffer) {
    if (!framebuffer || !framebuffer->address || framebuffer->memory_model != BYTER_FRAMEBUFFER_RGB ||
        framebuffer->bpp != 32 || framebuffer->width < MARGIN * 2 + CELL_WIDTH ||
        framebuffer->height < MARGIN * 2 + CELL_HEIGHT || framebuffer->width > 16384 ||
        framebuffer->height > 16384 || framebuffer->pitch < framebuffer->width * sizeof(u32) ||
        framebuffer->pitch % sizeof(u32) || framebuffer->height > ~0ULL / framebuffer->pitch ||
        framebuffer->red_mask_shift > 24 || framebuffer->green_mask_shift > 24 ||
        framebuffer->blue_mask_shift > 24)
        return;

    int changed = !ready || console_fb.address != framebuffer->address;
    console_fb = *framebuffer;
    foreground = color(0xe6, 0xed, 0xf3);
    background = color(0x10, 0x14, 0x1c);
    ready = 1;
    if (changed) {
        cursor_x = MARGIN;
        cursor_y = MARGIN;
    }
}

void byter_console_write(const char *text) {
    if (!ready || !text) return;
    while (*text) {
        u8 character = (u8)*text++;
        if (character == '\n') next_line();
        else if (character == '\r') cursor_x = MARGIN;
        else if (character == '\t') {
            for (u32 i = 0; i < 4; ++i) draw_glyph(' ');
        } else {
            draw_glyph(character);
        }
    }
    __asm__ volatile("dsb sy" ::: "memory");
}

int byter_console_available(void) {
    return ready;
}

void byter_console_get_cursor(u32 *x, u32 *y) {
    if (!ready) return;
    if (x) *x = cursor_x;
    if (y) *y = cursor_y;
}

int byter_console_set_cursor(u32 x, u32 y) {
    if (!ready || x > console_fb.width || y > console_fb.height ||
        console_fb.width - x < CELL_WIDTH || console_fb.height - y < CELL_HEIGHT)
        return 0;
    cursor_x = x;
    cursor_y = y;
    return 1;
}

void byter_console_get_size(u32 *width, u32 *height) {
    if (!ready) return;
    if (width) *width = (u32)console_fb.width;
    if (height) *height = (u32)console_fb.height;
}

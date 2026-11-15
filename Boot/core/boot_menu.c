#include "boot_menu.h"
#include "console.h"
#include "../platform/a059/buttons.h"

typedef unsigned long long u64;
typedef unsigned int u32;

#define MENU_TIMEOUT_SECONDS 5U
#define POLL_INTERVAL_MS 20U
#define MARGIN 24U
#define CELL_WIDTH 8U
#define CELL_HEIGHT 20U
#define MAX_LINE_CHARS 96U

static u64 counter(void) {
    u64 value;
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(value));
    return value;
}

static u64 counter_frequency(void) {
    u64 value;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(value));
    return value;
}

static void wait_ms(u32 milliseconds, u64 frequency) {
    u64 ticks = frequency / 1000 * milliseconds;
    u64 start = counter();
    if (frequency && ticks) {
        while (counter() - start < ticks) __asm__ volatile("yield");
    } else {
        for (volatile u32 i = 0; i < milliseconds * 20000U; ++i)
            __asm__ volatile("yield");
    }
}

static u32 centered_x(u32 characters, u32 width) {
    u32 pixels = characters * CELL_WIDTH;
    return pixels < width ? (width - pixels) / 2 : MARGIN;
}

static u32 text_length(const char *text) {
    u32 length = 0;
    while (text[length]) ++length;
    return length;
}

static void draw_menu(const struct byter_boot_option *options, u32 option_count,
                      u32 *row_x, u32 *row_y, u32 *countdown_x, u32 *countdown_y) {
    u32 width, height;
    byter_console_get_size(&width, &height);
    u32 lines = option_count + 7;
    u32 block_height = lines * CELL_HEIGHT;
    u32 y = height / 2 > block_height + 120 ? height / 2 - block_height - 120 : MARGIN;

    byter_console_set_cursor(centered_x(15, width), y);
    byter_console_write("BYTER BOOT MENU\n");
    byter_console_set_cursor(centered_x(text_length("Nothing Phone (3a)"), width),
                             y + CELL_HEIGHT * 2);
    byter_console_write("Nothing Phone (3a)\n\n");
    for (u32 i = 0; i < option_count; ++i) {
        u32 name_length = 0;
        while (name_length < MAX_LINE_CHARS - 2 && options[i].name[name_length]) ++name_length;
        row_x[i] = centered_x(name_length + 2, width);
        row_y[i] = y + CELL_HEIGHT * (3 + i);
        byter_console_set_cursor(row_x[i], row_y[i]);
        byter_console_write(i ? "  " : "> ");
        byter_console_write(options[i].name);
        byter_console_write("\n");
    }
    u32 footer_y = y + CELL_HEIGHT * (3 + option_count + 1);
    byter_console_set_cursor(centered_x(text_length("VOL +/- select   POWER boot   ESSENTIAL Fastboot"), width),
                             footer_y);
    byter_console_write("VOL +/- select   POWER boot   ESSENTIAL Fastboot\n");
    *countdown_x = centered_x(text_length("Automatic boot in 5 seconds"), width);
    *countdown_y = footer_y + CELL_HEIGHT;
    byter_console_set_cursor(*countdown_x, *countdown_y);
    byter_console_write("Automatic boot in 5 seconds");
}

static void show_countdown(u32 seconds_left, u32 x, u32 y) {
    byter_console_set_cursor(x, y);
    byter_console_write("Automatic boot in ");
    char digit[2] = {(char)('0' + seconds_left), 0};
    byter_console_write(digit);
    byter_console_write(" seconds");
}

static void update_selection(u32 previous, u32 selected, const u32 *row_x, const u32 *row_y,
                             u32 countdown_x, u32 countdown_y) {
    if (previous != selected) {
        if (byter_console_set_cursor(row_x[previous], row_y[previous]))
            byter_console_write("  ");
        if (byter_console_set_cursor(row_x[selected], row_y[selected]))
            byter_console_write("> ");
        byter_console_set_cursor(countdown_x, countdown_y);
    }
}

unsigned int byter_boot_menu_wait(const struct byter_boot_option *options,
                                  unsigned int option_count) {
    if (!options || !option_count || option_count > BYTER_BOOT_MAX_OPTIONS) return 0;
    if (!byter_console_available()) return 0;
    byter_console_write("Checking phone buttons...\n");
    if (!byter_a059_buttons_init()) {
        byter_console_write("Buttons unavailable; continuing boot.\n");
        return 0;
    }

    u64 frequency = counter_frequency();
    if (!frequency || frequency > ~0ULL / MENU_TIMEOUT_SECONDS) {
        byter_console_write("Timer unavailable; continuing boot.\n");
        return 0;
    }
    u64 timeout_ticks = frequency * MENU_TIMEOUT_SECONDS;
    u64 start = counter();
    unsigned previous = 0;
    u32 selected = 0;
    u32 row_x[BYTER_BOOT_MAX_OPTIONS];
    u32 row_y[BYTER_BOOT_MAX_OPTIONS];
    u32 countdown_x, countdown_y;
    u32 shown_seconds = MENU_TIMEOUT_SECONDS;
    draw_menu(options, option_count, row_x, row_y, &countdown_x, &countdown_y);

    for (;;) {
        unsigned pressed;
        if (!byter_a059_buttons_read(&pressed)) {
            byter_console_write("Button read failed; continuing boot.\n");
            return 0;
        }
        unsigned down = pressed & ~previous;
        previous = pressed;
        if (down & BYTER_BUTTON_POWER) {
            byter_console_write("\nPower selected the boot option.\n");
            return selected;
        }
        if (down & BYTER_BUTTON_ESSENTIAL) {
            byter_console_write("\nReturning to Fastboot...\n");
            if (!byter_a059_reboot_fastboot())
                byter_console_write("Fastboot reboot request failed.\n");
        }
        u32 old_selection = selected;
        if (down & BYTER_BUTTON_VOLUME_UP)
            selected = selected ? selected - 1 : option_count - 1;
        else if (down & BYTER_BUTTON_VOLUME_DOWN)
            selected = selected + 1 == option_count ? 0 : selected + 1;
        if (down & (BYTER_BUTTON_VOLUME_UP | BYTER_BUTTON_VOLUME_DOWN)) {
            start = counter();
            shown_seconds = MENU_TIMEOUT_SECONDS + 1;
            update_selection(old_selection, selected, row_x, row_y, countdown_x, countdown_y);
        }

        u64 elapsed = counter() - start;
        if (elapsed >= timeout_ticks) return selected;
        u32 left = MENU_TIMEOUT_SECONDS;
        if (frequency) {
            u64 remaining = timeout_ticks - elapsed;
            left = (u32)((remaining + frequency - 1) / frequency);
        }
        if (left != shown_seconds) {
            show_countdown(left, countdown_x, countdown_y);
            shown_seconds = left;
        }
        wait_ms(POLL_INTERVAL_MS, frequency);
    }
}

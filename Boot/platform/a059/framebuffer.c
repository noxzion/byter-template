#include "framebuffer.h"

int byter_a059_framebuffer_fallback(struct byter_framebuffer *framebuffer) {
    if (!framebuffer) return 0;
    *framebuffer = (struct byter_framebuffer){
        .address = (void *)0xe3940000ULL,
        .width = 1080,
        .height = 2392,
        .pitch = 4320,
        .bpp = 32,
        .memory_model = BYTER_FRAMEBUFFER_RGB,
        .red_mask_size = 8,
        .red_mask_shift = 16,
        .green_mask_size = 8,
        .green_mask_shift = 8,
        .blue_mask_size = 8,
        .blue_mask_shift = 0,
    };
    return 1;
}

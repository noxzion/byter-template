#include "byter.h"

#define BYTER_REQUEST __attribute__((used, section(".byter_requests"), aligned(8)))

static volatile uint64_t request_start[4]
    __attribute__((used, section(".byter_requests_start"), aligned(8))) = BYTER_REQUESTS_START_MARKER;
static volatile struct byter_framebuffer_request framebuffer_request BYTER_REQUEST = {
    .id = BYTER_FRAMEBUFFER_REQUEST_ID,
    .revision = 0,
};
static volatile uint64_t request_end[2]
    __attribute__((used, section(".byter_requests_end"), aligned(8))) = BYTER_REQUESTS_END_MARKER;

static void stop(void) {
    for (;;) __asm__ volatile("wfe");
}

void _start(void) {
    struct byter_framebuffer_response *response = framebuffer_request.response;
    if (!response || response->revision != 0 || !response->framebuffer_count ||
        !response->framebuffers || !response->framebuffers[0])
        stop();

    struct byter_framebuffer *fb = response->framebuffers[0];
    if (!fb->address || fb->memory_model != BYTER_FRAMEBUFFER_RGB || fb->bpp != 32 ||
        !fb->width || !fb->height || fb->width > UINT64_MAX / 4 ||
        fb->pitch < fb->width * 4 || !fb->pitch || fb->height > UINT64_MAX / fb->pitch)
        stop();

    volatile uint8_t *pixels = fb->address;
    uint64_t size = fb->pitch * fb->height;
    for (uint64_t i = 0; i < size; ++i) pixels[i] = 0;
    __asm__ volatile("dsb sy" ::: "memory");
    stop();
}

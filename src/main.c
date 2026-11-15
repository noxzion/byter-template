#include "byter.h"

#define BYTER_REQUEST __attribute__((used, section(".byter_requests"), aligned(8)))

static volatile uint64_t request_start[4]
    __attribute__((used, section(".byter_requests_start"), aligned(8))) = BYTER_REQUESTS_START_MARKER;
static volatile uint64_t base_revision[3]
    __attribute__((used, section(".byter_requests"), aligned(8))) = BYTER_BASE_REVISION(0);
static volatile struct byter_hhdm_request hhdm_request BYTER_REQUEST = {
    .id = BYTER_HHDM_REQUEST_ID,
    .revision = 0,
};
static volatile struct byter_module_request module_request BYTER_REQUEST = {
    .id = BYTER_MODULE_REQUEST_ID,
    .revision = 0,
};
static volatile struct byter_framebuffer_request framebuffer_request BYTER_REQUEST = {
    .id = BYTER_FRAMEBUFFER_REQUEST_ID,
    .revision = 0,
};
static volatile uint64_t request_end[2]
    __attribute__((used, section(".byter_requests_end"), aligned(8))) = BYTER_REQUESTS_END_MARKER;

static const uint8_t expected_module[] = {
    0x42, 0x59, 0x54, 0x45, 0x52, 0x2d, 0x4d, 0x4f,
    0x44, 0x55, 0x4c, 0x45, 0x00, 0x00, 0x01, 0xff,
    0x7e, 0x5a, 0xa5, 0x10
};

static int path_matches(const char *path) {
    static const char expected[] = "assets/probe.bin";
    if (!path) return 0;
    for (uint64_t i = 0; i < sizeof(expected); ++i)
        if (path[i] != expected[i]) return 0;
    return 1;
}

static int module_is_valid(void) {
    struct byter_hhdm_response *hhdm = hhdm_request.response;
    struct byter_module_response *response = module_request.response;
    if (!hhdm || hhdm->revision != 0 || !hhdm->offset ||
        !response || response->revision != 0 || response->module_count != 1 ||
        !response->modules)
        return 0;

    struct byter_file *file = response->modules[0];
    if (!file || file->revision != 0 || file->size != sizeof(expected_module) ||
        !path_matches(file->path) || !file->address)
        return 0;

    uintptr_t hhdm_offset = (uintptr_t)hhdm->offset;
    uintptr_t address = (uintptr_t)file->address;
    if (address < hhdm_offset) return 0;
    const volatile uint8_t *bytes = file->address;
    for (uint64_t i = 0; i < sizeof(expected_module); ++i)
        if (bytes[i] != expected_module[i]) return 0;
    return 1;
}

static void stop(void) {
    for (;;) __asm__ volatile("wfe");
}

void _start(void) {
    struct byter_framebuffer_response *response = framebuffer_request.response;
    if (!response || response->revision != 0 || !response->framebuffer_count ||
        !response->framebuffers || !response->framebuffers[0])
        stop();

    struct byter_framebuffer *fb = response->framebuffers[0];
    if (!fb->address || fb->memory_model != BYTER_FRAMEBUFFER_RGB ||
        !fb->width || !fb->height || fb->width > UINT64_MAX / 4 ||
        fb->pitch < fb->width * 4 || !fb->pitch || fb->height > UINT64_MAX / fb->pitch)
        stop();

    uint32_t color = module_is_valid() ? 0x0000ff00 : 0x00ff0000;
    volatile uint32_t *pixels = fb->address;
    uint64_t size = fb->pitch * fb->height / sizeof(*pixels);
    for (uint64_t i = 0; i < size; ++i) pixels[i] = color;
    __asm__ volatile("dsb sy" ::: "memory");
    stop();
}
